// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
#ifndef YB_TSERVER_REMOTE_BOOTSTRAP_CLIENT_H
#define YB_TSERVER_REMOTE_BOOTSTRAP_CLIENT_H

#include <atomic>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

#include <gtest/gtest_prod.h>

#include "yb/consensus/consensus_fwd.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/tserver/remote_bootstrap.pb.h"
#include "yb/tserver/remote_bootstrap_file_downloader.h"
#include "yb/util/status.h"

namespace yb {

class BlockIdPB;
class Env;
class FsManager;
class HostPort;
class RemoteBootstrapITest;

namespace consensus {
class ConsensusMetadata;
class ConsensusStatePB;
class RaftConfigPB;
class RaftPeerPB;
} // namespace consensus

namespace tablet {
class RaftGroupMetadata;
class TabletPeer;
class TabletStatusListener;
class RaftGroupReplicaSuperBlockPB;
} // namespace tablet

namespace tserver {
class DataIdPB;
class DataChunkPB;
class RemoteBootstrapServiceProxy;
class TSTabletManager;

class RemoteBootstrapComponent {
 public:
  virtual CHECKED_STATUS CreateDirectories(const string& db_dir, FsManager* fs) = 0;
  virtual CHECKED_STATUS Download() = 0;

  virtual ~RemoteBootstrapComponent() = default;
};

// Client class for using remote bootstrap to copy a tablet from another host.
// This class is not thread-safe.
//
// TODO:
// * Parallelize download of blocks and WAL segments.
//
class RemoteBootstrapClient {
 public:

  // Construct the remote bootstrap client.
  // 'fs_manager' and 'messenger' must remain valid until this object is destroyed.
  RemoteBootstrapClient(std::string tablet_id, FsManager* fs_manager);

  // Attempt to clean up resources on the remote end by sending an
  // EndRemoteBootstrapSession() RPC
  ~RemoteBootstrapClient();

  // Pass in the existing metadata for a tombstoned tablet, which will be
  // replaced if validation checks pass in Start().
  // 'meta' is the metadata for the tombstoned tablet and 'caller_term' is the
  // term provided by the caller (assumed to be the current leader of the
  // consensus config) for validation purposes.
  // If the consensus metadata exists on disk for this tablet, and if
  // 'caller_term' is lower than the current term stored in that consensus
  // metadata, then this method will fail with a Status::InvalidArgument error.
  CHECKED_STATUS SetTabletToReplace(const scoped_refptr<tablet::RaftGroupMetadata>& meta,
                                    int64_t caller_term);

  // Start up a remote bootstrap session to bootstrap from the specified
  // bootstrap peer. Place a new superblock indicating that remote bootstrap is
  // in progress. If the 'metadata' pointer is passed as NULL, it is ignored,
  // otherwise the RaftGroupMetadata object resulting from the initial remote
  // bootstrap response is returned.
  // ts_manager pointer allows the bootstrap function to assign non-random
  // data and wal directories for the bootstrapped tablets.
  // TODO: Rename these parameters to bootstrap_source_*.
  CHECKED_STATUS Start(const std::string& bootstrap_peer_uuid,
                       rpc::ProxyCache* proxy_cache,
                       const HostPort& bootstrap_peer_addr,
                       scoped_refptr<tablet::RaftGroupMetadata>* metadata,
                       TSTabletManager* ts_manager = nullptr);

  // Runs a "full" remote bootstrap, copying the physical layout of a tablet
  // from the leader of the specified consensus configuration.
  CHECKED_STATUS FetchAll(tablet::TabletStatusListener* status_listener);

  // After downloading all files successfully, write out the completed
  // replacement superblock.
  CHECKED_STATUS Finish();

  // Verify that the remote bootstrap was completed successfully by verifying that the ChangeConfig
  // request was propagated.
  CHECKED_STATUS VerifyChangeRoleSucceeded(
      const std::shared_ptr<consensus::Consensus>& shared_consensus);

  // Removes session at server.
  CHECKED_STATUS Remove();

 private:
  FRIEND_TEST(RemoteBootstrapRocksDBClientTest, TestBeginEndSession);
  friend class yb::RemoteBootstrapITest;

  template <class Component>
  void AddComponent() {
    components_.push_back(std::make_unique<Component>(&downloader_, &new_superblock_));
  }

  // Update the bootstrap StatusListener with a message.
  // The string "RemoteBootstrap: " will be prepended to each message.
  void UpdateStatusMessage(const std::string& message);

  // Download all WAL files sequentially.
  CHECKED_STATUS DownloadWALs();

  // Download a single WAL file.
  // Assumes the WAL directories have already been created.
  // WAL file is opened with options so that it will fsync() on close.
  CHECKED_STATUS DownloadWAL(uint64_t wal_segment_seqno);

  // Write out the Consensus Metadata file based on the ConsensusStatePB
  // downloaded as part of initiating the remote bootstrap session.
  CHECKED_STATUS WriteConsensusMetadata();

  CHECKED_STATUS CreateTabletDirectories(const string& db_dir, FsManager* fs);

  CHECKED_STATUS DownloadRocksDBFiles();

  // End the remote bootstrap session.
  CHECKED_STATUS EndRemoteSession();

  // Return standard log prefix.
  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  const std::string& session_id() const {
    return downloader_.session_id();
  }

  FsManager& fs_manager() const {
    return downloader_.fs_manager();
  }

  Env& env() const;

  const std::string& permanent_uuid() const;

  // Set-once members.
  const std::string tablet_id_;

  // State flags that enforce the progress of remote bootstrap.
  bool started_ = false;            // Session started.
  // Total number of remote bootstrap sessions. Used to calculate the transmission rate across all
  // the sessions.
  bool downloaded_wal_ = false;     // WAL segments downloaded.
  bool downloaded_blocks_ = false;  // Data blocks downloaded.
  bool downloaded_rocksdb_files_ = false;

  // Session-specific data items.
  bool replace_tombstoned_tablet_ = false;

  bool remove_required_ = false;

  // Local tablet metadata file.
  scoped_refptr<tablet::RaftGroupMetadata> meta_;

  // Local Consensus metadata file. This may initially be NULL if this is
  // bootstrapping a new replica (rather than replacing an old one).
  std::unique_ptr<consensus::ConsensusMetadata> cmeta_;

  tablet::TabletStatusListener* status_listener_ = nullptr;
  std::shared_ptr<RemoteBootstrapServiceProxy> proxy_;
  gscoped_ptr<tablet::RaftGroupReplicaSuperBlockPB> superblock_;
  tablet::RaftGroupReplicaSuperBlockPB new_superblock_;
  gscoped_ptr<consensus::ConsensusStatePB> remote_committed_cstate_;
  tablet::TabletDataState remote_tablet_data_state_;

  std::vector<uint64_t> wal_seqnos_;

  // Components of this remote bootstrap client.
  std::vector<std::unique_ptr<RemoteBootstrapComponent>> components_;

  // First available WAL segment.
  uint64_t first_wal_seqno_ = 0;

  int64_t start_time_micros_ = 0;

  // We track whether this session succeeded and send this information as part of the
  // EndRemoteBootstrapSessionRequestPB request.
  bool succeeded_ = false;

  const std::string log_prefix_;
  RemoteBootstrapFileDownloader downloader_;

  DISALLOW_COPY_AND_ASSIGN(RemoteBootstrapClient);
};

} // namespace tserver
} // namespace yb

#endif // YB_TSERVER_REMOTE_BOOTSTRAP_CLIENT_H

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
#ifndef YB_TSERVER_REMOTE_BOOTSTRAP_SESSION_H_
#define YB_TSERVER_REMOTE_BOOTSTRAP_SESSION_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/log_util.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/consensus/opid_util.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/stl_util.h"
#include "yb/tserver/remote_bootstrap.pb.h"
#include "yb/util/env_util.h"
#include "yb/util/net/rate_limiter.h"
#include "yb/util/locks.h"
#include "yb/util/status.h"

namespace yb {

class FsManager;

namespace tablet {
class TabletPeer;
} // namespace tablet

namespace tserver {

class TabletPeerLookupIf;

struct GetDataPieceInfo {
  // Input
  uint64_t offset;
  int64_t client_maxlen;

  // Output
  std::string data;
  int64_t data_size;
  RemoteBootstrapErrorPB::Code error_code;

  int64_t bytes_remaining() const {
    return data_size - offset;
  }
};

class RemoteBootstrapSource {
 public:
  virtual CHECKED_STATUS Init() = 0;
  virtual CHECKED_STATUS ValidateDataId(const DataIdPB& data_id) = 0;
  virtual CHECKED_STATUS GetDataPiece(const DataIdPB& data_id, GetDataPieceInfo* info) = 0;

  virtual ~RemoteBootstrapSource() = default;
};

// A potential Learner must establish a RemoteBootstrapSession with the leader in order
// to fetch the needed superblock, blocks, and log segments.
// This class is refcounted to make it easy to remove it from the session map
// on expiration while it is in use by another thread.
class RemoteBootstrapSession : public RefCountedThreadSafe<RemoteBootstrapSession> {
 public:
  RemoteBootstrapSession(const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                         std::string session_id, std::string requestor_uuid,
                         const std::atomic<int>* nsessions);

  // Initialize the session, including anchoring files (TODO) and fetching the
  // tablet superblock and list of WAL segments.
  CHECKED_STATUS Init();

  // Return ID of tablet corresponding to this session.
  const std::string& tablet_id() const;

  // Return UUID of the requestor that initiated this session.
  const std::string& requestor_uuid() const;

  CHECKED_STATUS GetDataPiece(const DataIdPB& data_id, GetDataPieceInfo* info);

  CHECKED_STATUS ValidateDataId(const DataIdPB& data_id);

  MonoTime start_time() { return start_time_; }

  const tablet::RaftGroupReplicaSuperBlockPB& tablet_superblock() const {
    return tablet_superblock_; }

  const consensus::ConsensusStatePB& initial_committed_cstate() const {
    return initial_committed_cstate_;
  }

  const log::SegmentSequence& log_segments() const { return log_segments_; }

  void SetSuccess();

  bool Succeeded();

  // Change the peer's role to VOTER.
  CHECKED_STATUS ChangeRole();

  void InitRateLimiter();

  void EnsureRateLimiterIsInitialized();

  RateLimiter& rate_limiter() { return rate_limiter_; }

  static const std::string kCheckpointsDir;

  // Get a piece of a RocksDB file.
  // The behavior and params are very similar to GetLogSegmentPiece(), but this one
  // is only for sending rocksdb files.
  static CHECKED_STATUS GetFilePiece(
      const std::string& path, const std::string& file_name, Env* env, GetDataPieceInfo* info);

 private:
  friend class RefCountedThreadSafe<RemoteBootstrapSession>;

  FRIEND_TEST(RemoteBootstrapRocksDBTest, TestCheckpointDirectory);
  FRIEND_TEST(RemoteBootstrapRocksDBTest, CheckSuperBlockHasRocksDBFields);
  FRIEND_TEST(RemoteBootstrapRocksDBTest, CheckSuperBlockHasSnapshotFields);
  FRIEND_TEST(RemoteBootstrapRocksDBTest, TestNonExistentRocksDBFile);

  virtual ~RemoteBootstrapSession();

  template <class Source>
  void AddSource() {
    sources_[Source::id_type()] = std::make_unique<Source>(tablet_peer_, &tablet_superblock_);
  }

  // Snapshot the log segment's length and put it into segment map.
  CHECKED_STATUS OpenLogSegment(uint64_t segment_seqno, RemoteBootstrapErrorPB::Code* error_code)
      REQUIRES(mutex_);

  // Unregister log anchor, if it's registered.
  CHECKED_STATUS UnregisterAnchorIfNeededUnlocked();

  // Helper API to set initial_committed_cstate_.
  CHECKED_STATUS SetInitialCommittedState();

  // Get a piece of a log segment.
  // If maxlen is 0, we use a system-selected length for the data piece.
  // *data is set to a std::string containing the data. Ownership of this object
  // is passed to the caller. A string is used because the RPC interface is
  // sending data serialized as protobuf and we want to minimize copying.
  // On error, Status is set to a non-OK value and error_code is filled in.
  //
  // This method is thread-safe.
  CHECKED_STATUS GetLogSegmentPiece(uint64_t segment_seqno, GetDataPieceInfo* info);

  // Get a piece of a RocksDB checkpoint file.
  CHECKED_STATUS GetRocksDBFilePiece(const std::string& file_name, GetDataPieceInfo* info);

  Env* env() const;

  RemoteBootstrapSource* Source(DataIdPB::IdType id_type) const;

  std::shared_ptr<tablet::TabletPeer> tablet_peer_;
  const std::string session_id_;
  const std::string requestor_uuid_;

  mutable std::mutex mutex_;

  std::shared_ptr<RandomAccessFile> opened_log_segment_file_ GUARDED_BY(mutex_);
  int64_t opened_log_segment_file_size_ GUARDED_BY(mutex_) = -1;
  uint64_t opened_log_segment_seqno_ GUARDED_BY(mutex_) = 0;
  bool opened_log_segment_active_ GUARDED_BY(mutex_) = false;

  tablet::RaftGroupReplicaSuperBlockPB tablet_superblock_;

  consensus::ConsensusStatePB initial_committed_cstate_;

  // The sequence of log segments that will be sent in the course of this
  // session.
  log::SegmentSequence log_segments_;

  log::LogAnchor log_anchor_;
  int64_t log_anchor_index_ GUARDED_BY(mutex_) = 0;

  // We need to know whether this ended succesfully before changing the peer's member type from
  // PRE_VOTER to VOTER.
  bool succeeded_ GUARDED_BY(mutex_) = false;

  // Directory where the checkpoint files are stored for this session (only for rocksdb).
  std::string checkpoint_dir_;

  // Time when this session was initialized.
  MonoTime start_time_;

  // Used to limit the transmission rate.
  RateLimiter rate_limiter_;

  // Pointer to the counter for of the number of sessions in RemoteBootstrapService. Used to
  // calculate the rate for the rate limiter.
  const std::atomic<int>* nsessions_;

  std::array<std::unique_ptr<RemoteBootstrapSource>, DataIdPB::IdType_ARRAYSIZE> sources_;

  DISALLOW_COPY_AND_ASSIGN(RemoteBootstrapSession);
};

} // namespace tserver
} // namespace yb

#endif // YB_TSERVER_REMOTE_BOOTSTRAP_SESSION_H_

// Copyright (c) YugaByte, Inc.
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

#ifndef ENT_SRC_YB_MASTER_CATALOG_ENTITY_INFO_H
#define ENT_SRC_YB_MASTER_CATALOG_ENTITY_INFO_H

#include "../../../../src/yb/master/catalog_entity_info.h"
#include "yb/master/cdc_rpc_tasks.h"
#include "yb/master/master_backup.pb.h"

#include "yb/client/table.h"

#include "yb/common/snapshot.h"

namespace yb {
namespace master {

struct TableDescription {
  scoped_refptr<NamespaceInfo> namespace_info;
  scoped_refptr<TableInfo> table_info;
  TabletInfos tablet_infos;
};

// This wraps around the proto containing CDC stream information. It will be used for
// CowObject managed access.
struct PersistentCDCStreamInfo : public Persistent<SysCDCStreamEntryPB, SysRowEntry::CDC_STREAM> {
  const TableId& table_id() const {
    return pb.table_id();
  }

  bool started_deleting() const {
    return pb.state() == SysCDCStreamEntryPB::DELETING ||
        pb.state() == SysCDCStreamEntryPB::DELETED;
  }

  bool is_deleting() const {
    return pb.state() == SysCDCStreamEntryPB::DELETING;
  }

  bool is_deleted() const {
    return pb.state() == SysCDCStreamEntryPB::DELETED;
  }

  const google::protobuf::RepeatedPtrField<CDCStreamOptionsPB> options() const {
    return pb.options();
  }
};

class CDCStreamInfo : public RefCountedThreadSafe<CDCStreamInfo>,
                      public MetadataCowWrapper<PersistentCDCStreamInfo> {
 public:
  explicit CDCStreamInfo(CDCStreamId stream_id) : stream_id_(std::move(stream_id)) {}

  const CDCStreamId& id() const override { return stream_id_; }

  const TableId& table_id() const;

  std::string ToString() const override;

 private:
  friend class RefCountedThreadSafe<CDCStreamInfo>;
  ~CDCStreamInfo() = default;

  const CDCStreamId stream_id_;

  DISALLOW_COPY_AND_ASSIGN(CDCStreamInfo);
};

// This wraps around the proto containing universe replication information. It will be used for
// CowObject managed access.
struct PersistentUniverseReplicationInfo :
    public Persistent<SysUniverseReplicationEntryPB, SysRowEntry::UNIVERSE_REPLICATION> {

  bool is_deleted_or_failed() const {
    return pb.state() == SysUniverseReplicationEntryPB::DELETED
      || pb.state() == SysUniverseReplicationEntryPB::DELETED_ERROR
      || pb.state() == SysUniverseReplicationEntryPB::FAILED;
  }

  bool is_active() const {
    return pb.state() == SysUniverseReplicationEntryPB::ACTIVE;
  }
};

class UniverseReplicationInfo : public RefCountedThreadSafe<UniverseReplicationInfo>,
                                public MetadataCowWrapper<PersistentUniverseReplicationInfo> {
 public:
  explicit UniverseReplicationInfo(std::string producer_id)
      : producer_id_(std::move(producer_id)) {}

  const std::string& id() const override { return producer_id_; }

  std::string ToString() const override;

  Result<std::shared_ptr<CDCRpcTasks>> GetOrCreateCDCRpcTasks(
      google::protobuf::RepeatedPtrField<HostPortPB> producer_masters);

 private:
  friend class RefCountedThreadSafe<UniverseReplicationInfo>;
  ~UniverseReplicationInfo() = default;

  const std::string producer_id_;

  std::shared_ptr<CDCRpcTasks> cdc_rpc_tasks_;
  std::string master_addrs_;

  // Protects cdc_rpc_tasks_.
  mutable rw_spinlock lock_;

  DISALLOW_COPY_AND_ASSIGN(UniverseReplicationInfo);
};

// The data related to a snapshot which is persisted on disk.
// This portion of SnapshotInfo is managed via CowObject.
// It wraps the underlying protobuf to add useful accessors.
struct PersistentSnapshotInfo : public Persistent<SysSnapshotEntryPB, SysRowEntry::SNAPSHOT> {
  SysSnapshotEntryPB::State state() const {
    return pb.state();
  }

  const std::string& state_name() const {
    return SysSnapshotEntryPB::State_Name(state());
  }

  bool is_creating() const {
    return state() == SysSnapshotEntryPB::CREATING;
  }

  bool started_deleting() const {
    return state() == SysSnapshotEntryPB::DELETING ||
           state() == SysSnapshotEntryPB::DELETED;
  }

  bool is_failed() const {
    return state() == SysSnapshotEntryPB::FAILED;
  }

  bool is_cancelled() const {
    return state() == SysSnapshotEntryPB::CANCELLED;
  }

  bool is_complete() const {
    return state() == SysSnapshotEntryPB::COMPLETE;
  }

  bool is_restoring() const {
    return state() == SysSnapshotEntryPB::RESTORING;
  }

  bool is_deleting() const {
    return state() == SysSnapshotEntryPB::DELETING;
  }
};

// The information about a snapshot.
//
// This object uses copy-on-write techniques similarly to TabletInfo.
// Please see the TabletInfo class doc above for more information.
class SnapshotInfo : public RefCountedThreadSafe<SnapshotInfo>,
                     public MetadataCowWrapper<PersistentSnapshotInfo> {
 public:
  explicit SnapshotInfo(SnapshotId id);

  virtual const std::string& id() const override { return snapshot_id_; };

  SysSnapshotEntryPB::State state() const;

  const std::string& state_name() const;

  std::string ToString() const override;

  // Returns true if the snapshot creation is in-progress.
  bool IsCreateInProgress() const;

  // Returns true if the snapshot restoring is in-progress.
  bool IsRestoreInProgress() const;

  // Returns true if the snapshot deleting is in-progress.
  bool IsDeleteInProgress() const;

  CHECKED_STATUS AddEntries(const TableDescription& table_description);

  static void AddEntries(
      const TableDescription& table_description,
      google::protobuf::RepeatedPtrField<SysRowEntry>* out,
      google::protobuf::RepeatedPtrField<SysSnapshotEntryPB::TabletSnapshotPB>* tablet_infos);

 private:
  friend class RefCountedThreadSafe<SnapshotInfo>;
  ~SnapshotInfo() = default;

  // The ID field is used in the sys_catalog table.
  const SnapshotId snapshot_id_;

  DISALLOW_COPY_AND_ASSIGN(SnapshotInfo);
};

} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_CATALOG_ENTITY_INFO_H

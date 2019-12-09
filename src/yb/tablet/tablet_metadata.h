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
#ifndef YB_TABLET_TABLET_METADATA_H
#define YB_TABLET_TABLET_METADATA_H

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/optional/optional_fwd.hpp>

#include "yb/common/entity_ids.h"
#include "yb/common/index.h"
#include "yb/common/partition.h"
#include "yb/common/schema.h"
#include "yb/consensus/opid_util.h"
#include "yb/fs/fs_manager.h"
#include "yb/gutil/callback.h"
#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/tablet_fwd.h"

#include "yb/util/mutex.h"
#include "yb/util/opid.h"
#include "yb/util/opid.pb.h"
#include "yb/util/status.h"
#include "yb/util/status_callback.h"

namespace yb {
namespace tablet {

extern const int64 kNoDurableMemStore;

  // Table info.
struct TableInfo {
  // Table id, name and type.
  std::string table_id;
  std::string table_name;
  TableType table_type;

  // The table schema, secondary index map, index info (for index table only) and schema version.
  Schema schema;
  IndexMap index_map;
  std::unique_ptr<IndexInfo> index_info;
  uint32_t schema_version = 0;

  // Partition schema of the table.
  PartitionSchema partition_schema;

  // A vector of column IDs that have been deleted, so that the compaction filter can free the
  // associated memory. As of 01/2019, deleted column IDs are persisted forever, even if all the
  // associated data has been discarded. In the future, we can garbage collect such column IDs to
  // make sure this vector doesn't grow too large.
  std::vector<DeletedColumn> deleted_cols;

  // We use the retention time from the primary table.
  uint32_t wal_retention_secs = 0;

  TableInfo() = default;
  TableInfo(std::string table_id,
            std::string table_name,
            TableType table_type,
            const Schema& schema,
            const IndexMap& index_map,
            const boost::optional<IndexInfo>& index_info,
            uint32_t schema_version,
            PartitionSchema partition_schema);
  TableInfo(const TableInfo& other,
            const Schema& schema,
            const IndexMap& index_map,
            const std::vector<DeletedColumn>& deleted_cols,
            uint32_t schema_version);

  CHECKED_STATUS LoadFromPB(const TableInfoPB& pb);
  void ToPB(TableInfoPB* pb) const;

  std::string ToString() const {
    TableInfoPB pb;
    ToPB(&pb);
    return pb.ShortDebugString();
  }
};

// Describes KV-store. Single KV-store is backed by one or two RocksDB instances, depending on
// whether distributed transactions are enabled for the table. KV-store for sys catalog could
// contain multiple tables.
struct KvStoreInfo {
  explicit KvStoreInfo(const KvStoreId& kv_store_id_) : kv_store_id(kv_store_id_) {}

  KvStoreInfo(const KvStoreId& kv_store_id_, const std::string& rocksdb_dir_)
      : kv_store_id(kv_store_id_),
        rocksdb_dir(rocksdb_dir_) {}

  CHECKED_STATUS LoadFromPB(const KvStoreInfoPB& pb, TableId primary_table_id);

  CHECKED_STATUS LoadTablesFromPB(
      google::protobuf::RepeatedPtrField<TableInfoPB> pbs, TableId primary_table_id);

  void ToPB(TableId primary_table_id, KvStoreInfoPB* pb) const;

  KvStoreId kv_store_id;

  // The directory where the regular RocksDB data for this KV-store is stored. For KV-stores having
  // tables with distributed transactions enabled an additional RocksDB is created in directory at
  // `rocksdb_dir + kIntentsDBSuffix` path.
  std::string rocksdb_dir;

  // Optional inclusive lower bound and exclusive upper bound for keys served by this KV-store.
  // See docdb::KeyBounds.
  std::string lower_bound_key;
  std::string upper_bound_key;

  // Map of tables sharing this KV-store indexed by the table id.
  // If pieces of the same table live in the same Raft group they should be located in different
  // KV-stores.
  std::unordered_map<TableId, std::unique_ptr<TableInfo>> tables;

  // Old versions of TableInfo that have since been altered. They are kept alive so that callers of
  // schema() and index_map(), etc don't need to worry about reference counting or locking.
  //
  // TODO: These TableInfo's are currently kept alive forever, under the assumption that a given
  // tablet won't have thousands of "alter table" calls. Replace the raw pointer with shared_ptr and
  // modify the callers to hold the shared_ptr at the top of the calls (e.g. tserver rpc calls).
  std::vector<std::unique_ptr<TableInfo>> old_tables;
};

// Manages the "blocks tracking" for the specified Raft group.
//
// RaftGroupMetadata is owned by the Raft group. As new blocks are written to store
// the Raft group's data, the Tablet calls Flush() to persist the block list
// on disk.
//
// At startup, the TSTabletManager will load a RaftGroupMetadata for each
// super block found in the tablets/ directory, and then instantiate
// Raft groups from this data.
class RaftGroupMetadata : public RefCountedThreadSafe<RaftGroupMetadata> {
 public:
  // Create metadata for a new Raft group. This assumes that the given superblock
  // has not been written before, and writes out the initial superblock with
  // the provided parameters.
  // data_root_dir and wal_root_dir dictates which disk this Raft group will
  // use in the respective directories.
  // If empty string is passed in, it will be randomly chosen.
  static CHECKED_STATUS CreateNew(FsManager* fs_manager,
                                  const std::string& table_id,
                                  const RaftGroupId& raft_group_id,
                                  const std::string& table_name,
                                  const TableType table_type,
                                  const Schema& schema,
                                  const IndexMap& index_map,
                                  const PartitionSchema& partition_schema,
                                  const Partition& partition,
                                  const boost::optional<IndexInfo>& index_info,
                                  const uint32_t schema_version,
                                  const TabletDataState& initial_tablet_data_state,
                                  RaftGroupMetadataPtr* metadata,
                                  const std::string& data_root_dir = std::string(),
                                  const std::string& wal_root_dir = std::string());

  // Load existing metadata from disk.
  static CHECKED_STATUS Load(FsManager* fs_manager,
                             const RaftGroupId& raft_group_id,
                             RaftGroupMetadataPtr* metadata);

  // Try to load an existing Raft group. If it does not exist, create it.
  // If it already existed, verifies that the schema of the Raft group matches the
  // provided 'schema'.
  //
  // This is mostly useful for tests which instantiate Raft groups directly.
  static CHECKED_STATUS LoadOrCreate(FsManager* fs_manager,
                                     const std::string& table_id,
                                     const RaftGroupId& raft_group_id,
                                     const std::string& table_name,
                                     const TableType table_type,
                                     const Schema& schema,
                                     const PartitionSchema& partition_schema,
                                     const Partition& partition,
                                     const boost::optional<IndexInfo>& index_info,
                                     const TabletDataState& initial_tablet_data_state,
                                     RaftGroupMetadataPtr* metadata);

  Result<const TableInfo*> GetTableInfo(const TableId& table_id) const;

  Result<TableInfo*> GetTableInfo(const TableId& table_id);

  const RaftGroupId& raft_group_id() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return raft_group_id_;
  }

  // Returns the partition of the Raft group.
  const Partition& partition() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return partition_;
  }

  // Returns the primary table id. For co-located tables, the primary table is the table this Raft
  // group was first created for. For single-tenant table, it is the primary table.
  const TableId& table_id() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return primary_table_id_;
  }

  // Returns the name, type, schema, index map, schema, etc of the primary table.
  std::string table_name() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return primary_table_info_guarded().first->table_name;
  }

  TableType table_type() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return primary_table_info_guarded().first->table_type;
  }

  const Schema& schema() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return primary_table_info_guarded().first->schema;
  }

  const IndexMap& index_map() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return primary_table_info_guarded().first->index_map;
  }

  uint32_t schema_version() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return primary_table_info_guarded().first->schema_version;
  }

  const std::string& indexed_tablet_id() const {
    DCHECK_NE(state_, kNotLoadedYet);
    static const std::string kEmptyString = "";
    std::lock_guard<MutexType> lock(data_mutex_);
    const auto* index_info = primary_table_info_unlocked()->index_info.get();
    return index_info ? index_info->indexed_table_id() : kEmptyString;
  }

  bool is_local_index() const {
    DCHECK_NE(state_, kNotLoadedYet);
    std::lock_guard<MutexType> lock(data_mutex_);
    const auto* index_info = primary_table_info_unlocked()->index_info.get();
    return index_info && index_info->is_local();
  }

  bool is_unique_index() const {
    DCHECK_NE(state_, kNotLoadedYet);
    std::lock_guard<MutexType> lock(data_mutex_);
    const auto* index_info = primary_table_info_unlocked()->index_info.get();
    return index_info && index_info->is_unique();
  }

  std::vector<ColumnId> index_key_column_ids() const {
    DCHECK_NE(state_, kNotLoadedYet);
    std::lock_guard<MutexType> lock(data_mutex_);
    const auto* index_info = primary_table_info_unlocked()->index_info.get();
    return index_info ? index_info->index_key_column_ids() : std::vector<ColumnId>();
  }

  // Returns the partition schema of the Raft group's tables.
  const PartitionSchema& partition_schema() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return primary_table_info_guarded().first->partition_schema;
  }

  const std::vector<DeletedColumn>& deleted_cols() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return primary_table_info_guarded().first->deleted_cols;
  }

  std::string rocksdb_dir() const { return kv_store_.rocksdb_dir; }

  std::string lower_bound_key() const { return kv_store_.lower_bound_key; }
  std::string upper_bound_key() const { return kv_store_.upper_bound_key; }

  std::string wal_dir() const { return wal_dir_; }

  // Set the WAL retention time for the primary table.
  void set_wal_retention_secs(uint32 wal_retention_secs);

  // Returns the wal retention time for the primary table.
  uint32_t wal_retention_secs() const;

  // Returns the data root dir for this Raft group, for example:
  // /mnt/d0/yb-data/tserver/data
  // TODO(#79): rework when we have more than one KV-store (and data roots) per Raft group.
  std::string data_root_dir() const;

  // Returns the WAL root dir for this Raft group, for example:
  // /mnt/d0/yb-data/tserver/wals
  std::string wal_root_dir() const;

  void SetSchema(const Schema& schema,
                 const IndexMap& index_map,
                 const std::vector<DeletedColumn>& deleted_cols,
                 const uint32_t version);

  void SetPartitionSchema(const PartitionSchema& partition_schema);

  void SetTableName(const std::string& table_name);

  void AddTable(const std::string& table_id,
                const std::string& table_name,
                const TableType table_type,
                const Schema& schema,
                const IndexMap& index_map,
                const PartitionSchema& partition_schema,
                const boost::optional<IndexInfo>& index_info,
                const uint32_t schema_version);

  void RemoveTable(const std::string& table_id);

  // Set / get the remote bootstrap / tablet data state.
  void set_tablet_data_state(TabletDataState state);
  TabletDataState tablet_data_state() const;

  CHECKED_STATUS Flush();

  // Mark the superblock to be in state 'delete_type', sync it to disk, and
  // then delete all of the rowsets in this tablet.
  // The metadata (superblock) is not deleted. For that, call DeleteSuperBlock().
  //
  // 'delete_type' must be one of TABLET_DATA_DELETED or TABLET_DATA_TOMBSTONED.
  // 'last_logged_opid' should be set to the last opid in the log, if any is known.
  // If 'last_logged_opid' is not set, then the current value of
  // last_logged_opid is not modified. This is important for roll-forward of
  // partially-tombstoned tablets during crash recovery.
  //
  // Returns only once all data has been removed.
  // The OUT parameter 'was_deleted' can be used by caller to determine if the tablet data was
  // actually deleted from disk or not. For example, in some cases, the tablet may have been
  // already deleted (and are here on a retry) and this operation essentially ends up being a no-op;
  // in such a case, 'was_deleted' will be set to FALSE.
  CHECKED_STATUS DeleteTabletData(TabletDataState delete_type, const yb::OpId& last_logged_opid);

  // Permanently deletes the superblock from the disk.
  // DeleteTabletData() must first be called and the tablet data state must be
  // TABLET_DATA_DELETED.
  // Returns Status::InvalidArgument if the list of orphaned blocks is not empty.
  // Returns Status::IllegalState if the tablet data state is not TABLET_DATA_DELETED.
  CHECKED_STATUS DeleteSuperBlock();

  FsManager *fs_manager() const { return fs_manager_; }

  yb::OpId tombstone_last_logged_opid() const { return tombstone_last_logged_opid_; }

  // Loads the currently-flushed superblock from disk into the given protobuf.
  CHECKED_STATUS ReadSuperBlockFromDisk(RaftGroupReplicaSuperBlockPB* superblock) const;

  // Sets *superblock to the serialized form of the current metadata.
  void ToSuperBlock(RaftGroupReplicaSuperBlockPB* superblock) const;

  // Fully replace a superblock (used for bootstrap).
  CHECKED_STATUS ReplaceSuperBlock(const RaftGroupReplicaSuperBlockPB &pb);

  // Creates a new Raft group metadata for the part of existing tablet contained in this Raft group.
  // Assigns specified Raft group ID, partition and key bounds for a new tablet.
  Result<RaftGroupMetadataPtr> CreateSubtabletMetadata(
      const RaftGroupId& raft_group_id, const Partition& partition,
      const std::string& lower_bound_key, const std::string& upper_bound_key) const;

 private:
  typedef simple_spinlock MutexType;

  friend class RefCountedThreadSafe<RaftGroupMetadata>;
  friend class MetadataTest;

  // Compile time assert that no one deletes RaftGroupMetadata objects.
  ~RaftGroupMetadata();

  // Constructor for creating a new Raft group.
  //
  // TODO: get rid of this many-arg constructor in favor of just passing in a
  // SuperBlock, which already contains all of these fields.
  RaftGroupMetadata(FsManager* fs_manager,
                    TableId table_id,
                    RaftGroupId raft_group_id,
                    std::string table_name,
                    TableType table_type,
                    const std::string rocksdb_dir,
                    const std::string wal_dir,
                    const Schema& schema,
                    const IndexMap& index_map,
                    PartitionSchema partition_schema,
                    Partition partition,
                    const boost::optional<IndexInfo>& index_info,
                    const uint32_t schema_version,
                    const TabletDataState& tablet_data_state);

  // Constructor for loading an existing Raft group.
  RaftGroupMetadata(FsManager* fs_manager, RaftGroupId raft_group_id);

  CHECKED_STATUS LoadFromDisk();

  // Update state of metadata to that of the given superblock PB.
  CHECKED_STATUS LoadFromSuperBlock(const RaftGroupReplicaSuperBlockPB& superblock);

  CHECKED_STATUS ReadSuperBlock(RaftGroupReplicaSuperBlockPB *pb);

  // Fully replace superblock.
  // Requires 'flush_lock_'.
  CHECKED_STATUS ReplaceSuperBlockUnlocked(const RaftGroupReplicaSuperBlockPB &pb);

  // Requires 'data_mutex_'.
  void ToSuperBlockUnlocked(RaftGroupReplicaSuperBlockPB* superblock) const;

  // Return standard "T xxx P yyy" log prefix.
  std::string LogPrefix() const;

  // Return a pointer to the primary table info. This pointer will be valid until the
  // RaftGroupMetadata is destructed, even if the schema is changed.
  const TableInfo* primary_table_info_unlocked() const {
    const auto& tables = kv_store_.tables;
    const auto itr = tables.find(primary_table_id_);
    DCHECK(itr != tables.end());
    return itr->second.get();
  }

  // Return a pair of a pointer to the primary table info and lock guard. The pointer will be valid
  // until the RaftGroupMetadata is destructed, even if the schema is changed.
  std::pair<const TableInfo*, std::unique_lock<MutexType>> primary_table_info_guarded() const {
    std::unique_lock<MutexType> lock(data_mutex_);
    return { primary_table_info_unlocked(), std::move(lock) };
  }

  enum State {
    kNotLoadedYet,
    kNotWrittenYet,
    kInitialized
  };
  State state_;

  // Lock protecting the underlying data.
  mutable MutexType data_mutex_;

  // Lock protecting flushing the data to disk.
  // If taken together with 'data_mutex_', must be acquired first.
  mutable Mutex flush_lock_;

  RaftGroupId raft_group_id_;
  Partition partition_;

  // The primary table id. Primary table is the first table this Raft group is created for.
  // Additional tables can be added to this Raft group to co-locate with this table.
  TableId primary_table_id_;

  // KV-store for this Raft group.
  KvStoreInfo kv_store_;

  FsManager* const fs_manager_;

  // The directory where the write-ahead log for this Raft group is stored.
  std::string wal_dir_;

  // The current state of remote bootstrap for the tablet.
  TabletDataState tablet_data_state_;

  // Record of the last opid logged by the tablet before it was last tombstoned. Has no meaning for
  // non-tombstoned tablets.
  yb::OpId tombstone_last_logged_opid_;

  DISALLOW_COPY_AND_ASSIGN(RaftGroupMetadata);
};

CHECKED_STATUS MigrateSuperblock(RaftGroupReplicaSuperBlockPB* superblock);

extern const std::string kIntentsSubdir;
extern const std::string kIntentsDBSuffix;

} // namespace tablet
} // namespace yb

#endif /* YB_TABLET_TABLET_METADATA_H */

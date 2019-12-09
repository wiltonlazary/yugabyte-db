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
#ifndef YB_TABLET_TABLET_H_
#define YB_TABLET_TABLET_H_

#include <iosfwd>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/write_batch.h"

#include "yb/client/client.h"
#include "yb/client/meta_data_cache.h"
#include "yb/client/transaction_manager.h"

#include "yb/common/schema.h"
#include "yb/common/transaction.h"
#include "yb/common/ql_storage_interface.h"

#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/docdb/doc_operation.h"
#include "yb/docdb/ql_rocksdb_storage.h"
#include "yb/docdb/shared_lock_manager.h"

#include "yb/gutil/atomicops.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tablet/abstract_tablet.h"
#include "yb/tablet/tablet_options.h"
#include "yb/tablet/mvcc.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/util/locks.h"
#include "yb/util/metrics.h"
#include "yb/util/pending_op_counter.h"
#include "yb/util/semaphore.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/enums.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/tablet/operations/snapshot_operation.h"

namespace rocksdb {
class DB;
}

namespace yb {

class MemTracker;
class MetricEntity;
class RowChangeList;

namespace docdb {
class ConsensusFrontier;
}

namespace log {
class LogAnchorRegistry;
}

namespace server {
class Clock;
}

class MaintenanceManager;
class MaintenanceOp;
class MaintenanceOpStats;

namespace tablet {

class ChangeMetadataOperationState;
class ScopedReadOperation;
struct TabletMetrics;
struct TransactionApplyData;
class TransactionCoordinator;
class TransactionCoordinatorContext;
class TransactionParticipant;
class TruncateOperationState;
class WriteOperationState;

using docdb::LockBatch;

YB_STRONGLY_TYPED_BOOL(IncludeIntents);

YB_DEFINE_ENUM(FlushMode, (kSync)(kAsync));

enum class FlushFlags {
  kNone = 0,

  kRegular = 1,
  kIntents = 2,

  kAll = kRegular | kIntents
};

inline FlushFlags operator|(FlushFlags lhs, FlushFlags rhs) {
  return static_cast<FlushFlags>(to_underlying(lhs) | to_underlying(rhs));
}

inline FlushFlags operator&(FlushFlags lhs, FlushFlags rhs) {
  return static_cast<FlushFlags>(to_underlying(lhs) & to_underlying(rhs));
}

inline bool HasFlags(FlushFlags lhs, FlushFlags rhs) {
  return (lhs & rhs) != FlushFlags::kNone;
}

class WriteOperation;

struct DocDbOpIds {
  OpId regular;
  OpId intents;

  std::string ToString() const;
};

typedef std::function<Status(const TableInfo&)> AddTableListener;

class Tablet : public AbstractTablet, public TransactionIntentApplier {
 public:
  class CompactionFaultHooks;
  class FlushCompactCommonHooks;
  class FlushFaultHooks;

  // A function that returns the current majority-replicated hybrid time leader lease, or waits
  // until a hybrid time leader lease with at least the given microsecond component is acquired
  // (first argument), or a timeout occurs (second argument). HybridTime::kInvalid is returned
  // in case of a timeout.
  using HybridTimeLeaseProvider = std::function<HybridTime(MicrosTime, CoarseTimePoint)>;
  using TransactionIdSet = std::unordered_set<TransactionId, TransactionIdHash>;

  // Create a new tablet.
  //
  // If 'metric_registry' is non-NULL, then this tablet will create a 'tablet' entity
  // within the provided registry. Otherwise, no metrics are collected.
  Tablet(
      const RaftGroupMetadataPtr& metadata,
      const std::shared_future<client::YBClient*> &client_future,
      const scoped_refptr<server::Clock>& clock,
      const std::shared_ptr<MemTracker>& parent_mem_tracker,
      std::shared_ptr<MemTracker> block_based_table_mem_tracker,
      MetricRegistry* metric_registry,
      const scoped_refptr<log::LogAnchorRegistry>& log_anchor_registry,
      const TabletOptions& tablet_options,
      std::string log_prefix_suffix,
      TransactionParticipantContext* transaction_participant_context,
      client::LocalTabletFilter local_tablet_filter,
      TransactionCoordinatorContext* transaction_coordinator_context);

  ~Tablet();

  // Open the tablet.
  // Upon completion, the tablet enters the kBootstrapping state.
  CHECKED_STATUS Open();

  CHECKED_STATUS EnableCompactions();

  // Mark that the tablet has finished bootstrapping.
  // This transitions from kBootstrapping to kOpen state.
  void MarkFinishedBootstrapping();

  // This can be called to proactively prevent new operations from being handled, even before
  // Shutdown() is called.
  void SetShutdownRequestedFlag();
  bool IsShutdownRequested() const {
    return shutdown_requested_.load(std::memory_order::memory_order_acquire);
  }

  void Shutdown(IsDropTable is_drop_table = IsDropTable::kFalse);

  CHECKED_STATUS ImportData(const std::string& source_dir);

  CHECKED_STATUS ApplyIntents(const TransactionApplyData& data) override;

  CHECKED_STATUS RemoveIntents(const RemoveIntentsData& data, const TransactionId& id) override;

  CHECKED_STATUS RemoveIntents(
      const RemoveIntentsData& data, const TransactionIdSet& transactions) override;

  HybridTime ApplierSafeTime(HybridTime min_allowed, CoarseTimePoint deadline) override;

  // Finish the Prepare phase of a write transaction.
  //
  // Starts an MVCC transaction and assigns a timestamp for the transaction.
  //
  // This should always be done _after_ any relevant row locks are acquired
  // (using CreatePreparedInsert/CreatePreparedMutate). This ensures that,
  // within each row, timestamps only move forward. If we took a timestamp before
  // getting the row lock, we could have the following situation:
  //
  //   Thread 1         |  Thread 2
  //   ----------------------
  //   Start tx 1       |
  //                    |  Start tx 2
  //                    |  Obtain row lock
  //                    |  Update row
  //                    |  Commit tx 2
  //   Obtain row lock  |
  //   Delete row       |
  //   Commit tx 1
  //
  // This would cause the mutation list to look like: @t1: DELETE, @t2: UPDATE
  // which is invalid, since we expect to be able to be able to replay mutations
  // in increasing timestamp order on a given row.
  //
  // TODO: rename this to something like "FinishPrepare" or "StartApply", since
  // it's not the first thing in a transaction!
  void StartOperation(WriteOperationState* operation_state);

  // Apply all of the row operations associated with this transaction.
  CHECKED_STATUS ApplyRowOperations(WriteOperationState* operation_state);

  // Apply a set of RocksDB row operations.
  // If rocksdb_write_batch is specified it could contain preencoded RocksDB operations.
  CHECKED_STATUS ApplyKeyValueRowOperations(
      const docdb::KeyValueWriteBatchPB& put_batch,
      const rocksdb::UserFrontiers* frontiers,
      HybridTime hybrid_time);

  void WriteToRocksDB(
      const rocksdb::UserFrontiers* frontiers,
      rocksdb::WriteBatch* write_batch,
      docdb::StorageDbType storage_db_type);

  //------------------------------------------------------------------------------------------------
  // Redis Request Processing.
  // Takes a Redis WriteRequestPB as input with its redis_write_batch.
  // Constructs a WriteRequestPB containing a serialized WriteBatch that will be
  // replicated by Raft. (Makes a copy, it is caller's responsibility to deallocate
  // write_request afterwards if it is no longer needed).
  // The operation acquires the necessary locks required to correctly serialize concurrent write
  // operations to same/conflicting part of the key/sub-key space. The locks acquired are returned
  // via the 'keys_locked' vector, so that they may be unlocked later when the operation has been
  // committed.
  CHECKED_STATUS KeyValueBatchFromRedisWriteBatch(WriteOperation* operation);

  CHECKED_STATUS HandleRedisReadRequest(
      CoarseTimePoint deadline,
      const ReadHybridTime& read_time,
      const RedisReadRequestPB& redis_read_request,
      RedisResponsePB* response) override;

  //------------------------------------------------------------------------------------------------
  // CQL Request Processing.
  CHECKED_STATUS HandleQLReadRequest(
      CoarseTimePoint deadline,
      const ReadHybridTime& read_time,
      const QLReadRequestPB& ql_read_request,
      const TransactionMetadataPB& transaction_metadata,
      QLReadRequestResult* result) override;

  CHECKED_STATUS CreatePagingStateForRead(
      const QLReadRequestPB& ql_read_request, const size_t row_count,
      QLResponsePB* response) const override;

  // The QL equivalent of KeyValueBatchFromRedisWriteBatch, works similarly.
  void KeyValueBatchFromQLWriteBatch(std::unique_ptr<WriteOperation> operation);

  //------------------------------------------------------------------------------------------------
  // Postgres Request Processing.
  CHECKED_STATUS HandlePgsqlReadRequest(
      CoarseTimePoint deadline,
      const ReadHybridTime& read_time,
      const PgsqlReadRequestPB& pgsql_read_request,
      const TransactionMetadataPB& transaction_metadata,
      PgsqlReadRequestResult* result) override;

  CHECKED_STATUS CreatePagingStateForRead(
      const PgsqlReadRequestPB& pgsql_read_request, const size_t row_count,
      PgsqlResponsePB* response) const override;

  CHECKED_STATUS KeyValueBatchFromPgsqlWriteBatch(WriteOperation* operation);

  //------------------------------------------------------------------------------------------------
  // Create a RocksDB checkpoint in the provided directory. Only used when table_type_ ==
  // YQL_TABLE_TYPE.
  CHECKED_STATUS CreateCheckpoint(const std::string& dir);

  // Create a new row iterator which yields the rows as of the current MVCC
  // state of this tablet.
  // The returned iterator is not initialized.
  Result<std::unique_ptr<common::YQLRowwiseIteratorIf>> NewRowIterator(
      const Schema &projection, const boost::optional<TransactionId>& transaction_id,
      const TableId& table_id = "") const;
  Result<std::unique_ptr<common::YQLRowwiseIteratorIf>> NewRowIterator(
      const TableId& table_id) const;

  //------------------------------------------------------------------------------------------------
  // Makes RocksDB Flush.
  CHECKED_STATUS Flush(FlushMode mode,
                       FlushFlags flags = FlushFlags::kAll,
                       int64_t ignore_if_flushed_after_tick = rocksdb::FlushOptions::kNeverIgnore);

  CHECKED_STATUS WaitForFlush();

  // Prepares the transaction context for the alter schema operation.
  // An error will be returned if the specified schema is invalid (e.g.
  // key mismatch, or missing IDs)
  CHECKED_STATUS CreatePreparedChangeMetadata(
      ChangeMetadataOperationState *operation_state,
      const Schema* schema);

  // Apply the Schema of the specified operation.
  CHECKED_STATUS AlterSchema(ChangeMetadataOperationState* operation_state);

  // Change wal_retention_secs in the metadata.
  CHECKED_STATUS AlterWalRetentionSecs(ChangeMetadataOperationState* operation_state);

  // Apply replicated add table operation.
  CHECKED_STATUS AddTable(const TableInfoPB& table_info);

  // Truncate this tablet by resetting the content of RocksDB.
  CHECKED_STATUS Truncate(TruncateOperationState* state);

  // Verbosely dump this entire tablet to the logs. This is only
  // really useful when debugging unit tests failures where the tablet
  // has a very small number of rows.
  CHECKED_STATUS DebugDump(vector<std::string> *lines = NULL);

  const Schema* schema() const {
    return &metadata_->schema();
  }

  // Returns a reference to the key projection of the tablet schema.
  // The schema keys are immutable.
  const Schema& key_schema() const { return key_schema_; }

  // Return the MVCC manager for this tablet.
  MvccManager* mvcc_manager() { return &mvcc_; }

  docdb::SharedLockManager* shared_lock_manager() { return &shared_lock_manager_; }

  std::atomic<int64_t>* monotonic_counter() { return &monotonic_counter_; }

  // Set the conter to at least 'value'.
  void UpdateMonotonicCounter(int64_t value);

  const RaftGroupMetadata *metadata() const { return metadata_.get(); }
  RaftGroupMetadata *metadata() { return metadata_.get(); }

  const std::string& tablet_id() const override { return metadata_->raft_group_id(); }

  // Return the metrics for this tablet.
  // May be NULL in unit tests, etc.
  TabletMetrics* metrics() { return metrics_.get(); }

  // Return handle to the metric entity of this tablet.
  const scoped_refptr<MetricEntity>& GetMetricEntity() const { return metric_entity_; }

  // Returns a reference to this tablet's memory tracker.
  const std::shared_ptr<MemTracker>& mem_tracker() const { return mem_tracker_; }

  TableType table_type() const override { return table_type_; }

  // Returns true if a RocksDB-backed tablet has any SSTables.
  Result<bool> HasSSTables() const;

  // Returns the maximum persistent op id from all SSTables in RocksDB.
  // First for regular records and second for intents.
  // When invalid_if_no_new_data is true then function would return invalid op id when no new
  // data is present in corresponding db.
  Result<DocDbOpIds> MaxPersistentOpId(bool invalid_if_no_new_data = false) const;

  // Returns the maximum persistent hybrid_time across all SSTables in RocksDB.
  Result<HybridTime> MaxPersistentHybridTime() const;

  // Returns oldest mutable memtable write hybrid time in RocksDB or HybridTime::kMax if memtable
  // is empty.
  Result<HybridTime> OldestMutableMemtableWriteHybridTime() const;

  // Returns the location of the last rocksdb checkpoint. Used for tests only.
  std::string TEST_LastRocksDBCheckpointDir() { return last_rocksdb_checkpoint_dir_; }

  // For non-kudu table type fills key-value batch in transaction state request and updates
  // request in state. Due to acquiring locks it can block the thread.
  void AcquireLocksAndPerformDocOperations(std::unique_ptr<WriteOperation> operation);

  // Given a propopsed "history cutoff" timestamp, returns either that value, if possible, or a
  // smaller value corresponding to the oldest active reader, whichever is smaller. This ensures
  // that data needed by active read operations is not compacted away.
  //
  // Also updates the "earliest allowed read time" of the tablet to be equal to the returned value,
  // (if it is still lower than the value about to be returned), so that new readers with timestamps
  // earlier than that will be rejected.
  HybridTime UpdateHistoryCutoff(HybridTime proposed_cutoff);

  const scoped_refptr<server::Clock> &clock() const {
    return clock_;
  }

  const Schema& SchemaRef(const std::string& table_id = "") const override {
    return CHECK_RESULT(metadata_->GetTableInfo(table_id))->schema;
  }

  const common::YQLStorageIf& QLStorage() const override {
    return *ql_storage_;
  }

  // Used from tests
  const std::shared_ptr<rocksdb::Statistics>& rocksdb_statistics() const {
    return rocksdb_statistics_;
  }

  TransactionCoordinator* transaction_coordinator() {
    return transaction_coordinator_.get();
  }

  TransactionParticipant* transaction_participant() const {
    return transaction_participant_.get();
  }

  void ForceRocksDBCompactInTest();

  docdb::DocDB doc_db() const { return { regular_db_.get(), intents_db_.get(), &key_bounds_ }; }

  std::string TEST_DocDBDumpStr(IncludeIntents include_intents = IncludeIntents::kFalse);

  template<class T> void TEST_DocDBDumpToContainer(IncludeIntents include_intents, T* out);

  size_t TEST_CountRegularDBRecords();

  CHECKED_STATUS CreateReadIntents(
      const TransactionMetadataPB& transaction_metadata,
      const google::protobuf::RepeatedPtrField<QLReadRequestPB>& ql_batch,
      const google::protobuf::RepeatedPtrField<PgsqlReadRequestPB>& pgsql_batch,
      docdb::KeyValueWriteBatchPB* out);

  uint64_t GetCurrentVersionSstFilesSize() const;
  uint64_t GetCurrentVersionSstFilesUncompressedSize() const;
  uint64_t GetCurrentVersionNumSSTFiles() const;

  // Returns the number of memtables in intents and regular db-s.
  std::pair<int, int> GetNumMemtables() const;

  void SetHybridTimeLeaseProvider(HybridTimeLeaseProvider provider) {
    ht_lease_provider_ = std::move(provider);
  }

  void SetMemTableFlushFilterFactory(std::function<rocksdb::MemTableFilter()> factory) {
    mem_table_flush_filter_factory_ = std::move(factory);
  }

  // When a compaction starts with a particular "history cutoff" timestamp, it calls this function
  // to disallow reads at a time lower than that history cutoff timestamp, to avoid reading
  // invalid/incomplete data.
  //
  // Returns true if the new history cutoff timestamp was successfully registered, or false if
  // it can't be used because there are pending reads at lower timestamps.
  HybridTime Get(HybridTime lower_bound);

  bool ShouldApplyWrite();

  rocksdb::DB* TEST_db() const {
    return regular_db_.get();
  }

  rocksdb::DB* TEST_intents_db() const {
    return intents_db_.get();
  }

  CHECKED_STATUS TEST_SwitchMemtable();

  // Initialize RocksDB's max persistent op id and hybrid time to that of the operation state.
  // Necessary for cases like truncate or restore snapshot when RocksDB is reset.
  CHECKED_STATUS ModifyFlushedFrontier(
      const docdb::ConsensusFrontier& value,
      rocksdb::FrontierModificationMode mode);

  // Prepares the operation context for a snapshot operation.
  CHECKED_STATUS PrepareForSnapshotOp(SnapshotOperationState* tx_state);

  // Restore the RocksDB checkpoint from the provided directory.
  // Only used when table_type_ == YQL_TABLE_TYPE.
  CHECKED_STATUS RestoreCheckpoint(
      const std::string& dir, const docdb::ConsensusFrontier& frontier);

  // Create snapshot for this tablet.
  virtual CHECKED_STATUS CreateSnapshot(SnapshotOperationState* tx_state);

  // Delete snapshot for this tablet.
  virtual CHECKED_STATUS DeleteSnapshot(SnapshotOperationState* tx_state);

  // Restore snapshot for this tablet. In addition to backup/restore, this is used for initial
  // syscatalog RocksDB creation without the initdb overhead.
  CHECKED_STATUS RestoreSnapshot(SnapshotOperationState* tx_state);

  static std::string SnapshotsDirName(const std::string& rocksdb_dir);

  // Get the isolation level of the given transaction from the metadata stored in the provisional
  // records RocksDB.
  Result<IsolationLevel> GetIsolationLevel(const TransactionMetadataPB& transaction) override;

  // Create an on-disk sub tablet of this tablet with specified ID, partition and key bounds.
  CHECKED_STATUS CreateSubtablet(
      const TabletId& tablet_id, const Partition& partition,
      const docdb::KeyBounds& key_bounds);

  // Scans the intent db. Potentially takes a long time. Used for testing/debugging.
  Result<int64_t> CountIntents();

  // Flushed intents db if necessary.
  void FlushIntentsDbIfNecessary(const yb::OpId& lastest_log_entry_op_id);

  // ==============================================================================================
 protected:

  friend class Iterator;
  friend class TabletPeerTest;
  friend class ScopedReadOperation;
  FRIEND_TEST(TestTablet, TestGetLogRetentionSizeForIndex);

  CHECKED_STATUS StartDocWriteOperation(WriteOperation* operation);

  CHECKED_STATUS OpenKeyValueTablet();
  virtual CHECKED_STATUS CreateTabletDirectories(const string& db_dir, FsManager* fs);

  void DocDBDebugDump(std::vector<std::string> *lines);

  // Register/Unregister a read operation, with an associated timestamp, for the purpose of
  // tracking the oldest read point.
  CHECKED_STATUS RegisterReaderTimestamp(HybridTime read_point) override;
  void UnregisterReader(HybridTime read_point) override;

  CHECKED_STATUS PrepareTransactionWriteBatch(
      const docdb::KeyValueWriteBatchPB& put_batch,
      HybridTime hybrid_time,
      rocksdb::WriteBatch* rocksdb_write_batch);

  Result<TransactionOperationContextOpt> CreateTransactionOperationContext(
      const TransactionMetadataPB& transaction_metadata) const;

  TransactionOperationContextOpt CreateTransactionOperationContext(
      const boost::optional<TransactionId>& transaction_id) const;

  // Pause any new read/write operations and wait for all pending read/write operations to finish.
  util::ScopedPendingOperationPause PauseReadWriteOperations();

  std::string LogPrefix() const;

  std::string LogPrefix(docdb::StorageDbType db_type) const;

  // Lock protecting schema_ and key_schema_.
  //
  // Writers take this lock in shared mode before decoding and projecting
  // their requests. They hold the lock until after APPLY.
  //
  // Readers take this lock in shared mode only long enough to copy the
  // current schema into the iterator, after which all projection is taken
  // care of based on that copy.
  //
  // On an AlterSchema, this is taken in exclusive mode during Prepare() and
  // released after the schema change has been applied.
  mutable rw_semaphore schema_lock_;

  const Schema key_schema_;

  RaftGroupMetadataPtr metadata_;
  TableType table_type_;

  // Used for tests only.
  std::string last_rocksdb_checkpoint_dir_;

  // Lock protecting access to the 'components_' member (i.e the rowsets in the tablet)
  //
  // Shared mode:
  // - Writers take this in shared mode at the same time as they obtain an MVCC hybrid_time
  //   and capture a reference to components_. This ensures that we can use the MVCC hybrid_time
  //   to determine which writers are writing to which components during compaction.
  // - Readers take this in shared mode while capturing their iterators. This ensures that
  //   they see a consistent view when racing against flush/compact.
  //
  // Exclusive mode:
  // - Flushes/compactions take this lock in order to lock out concurrent updates.
  //
  // NOTE: callers should avoid taking this lock for a long time, even in shared mode.
  // This is because the lock has some concept of fairness -- if, while a long reader
  // is active, a writer comes along, then all future short readers will be blocked.
  // TODO: now that this is single-threaded again, we should change it to rw_spinlock
  mutable rw_spinlock component_lock_;

  scoped_refptr<log::LogAnchorRegistry> log_anchor_registry_;
  std::shared_ptr<MemTracker> mem_tracker_;
  std::shared_ptr<MemTracker> block_based_table_mem_tracker_;

  MetricEntityPtr metric_entity_;
  gscoped_ptr<TabletMetrics> metrics_;
  FunctionGaugeDetacher metric_detacher_;

  // A pointer to the server's clock.
  scoped_refptr<server::Clock> clock_;

  MvccManager mvcc_;

  // Maps a timestamp to the number active readers with that timestamp.
  // TODO(ENG-961): Check if this is a point of contention. If so, shard it as suggested in D1219.
  std::map<HybridTime, int64_t> active_readers_cnt_ GUARDED_BY(active_readers_mutex_);
  HybridTime earliest_read_time_allowed_ GUARDED_BY(active_readers_mutex_) {HybridTime::kMin};
  mutable std::mutex active_readers_mutex_;

  // Lock used to serialize the creation of RocksDB checkpoints.
  mutable std::mutex create_checkpoint_lock_;

  enum State {
    kInitialized,
    kBootstrapping,
    kOpen,
    kShutdown
  };
  State state_ = kInitialized;

  // Fault hooks. In production code, these will always be NULL.
  std::shared_ptr<CompactionFaultHooks> compaction_hooks_;
  std::shared_ptr<FlushFaultHooks> flush_hooks_;
  std::shared_ptr<FlushCompactCommonHooks> common_hooks_;

  // Statistics for the RocksDB database.
  std::shared_ptr<rocksdb::Statistics> rocksdb_statistics_;

  // RocksDB database for key-value tables.
  std::unique_ptr<rocksdb::DB> regular_db_;

  std::unique_ptr<rocksdb::DB> intents_db_;

  // Optional key bounds (see docdb::KeyBounds) served by this tablet.
  docdb::KeyBounds key_bounds_;

  std::unique_ptr<common::YQLStorageIf> ql_storage_;

  // This is for docdb fine-grained locking.
  docdb::SharedLockManager shared_lock_manager_;

  // For the block cache and memory manager shared across tablets
  TabletOptions tablet_options_;

  // A lightweight way to reject new operations when the tablet is shutting down. This is used to
  // prevent race conditions between destroying the RocksDB instance and read/write operations.
  std::atomic_bool shutdown_requested_{false};

  // This is a special atomic counter per tablet that increases monotonically.
  // It is like timestamp, but doesn't need locks to read or update.
  // This is raft replicated as well. Each replicate message contains the current number.
  // It is guaranteed to keep increasing for committed entries even across tablet server
  // restarts and leader changes.
  std::atomic<int64_t> monotonic_counter_{0};

  // Number of pending operations. We use this to make sure we don't shut down RocksDB before all
  // pending operations are finished. We don't have a strict definition of an "operation" for the
  // purpose of this counter. We simply wait for this counter to go to zero before shutting down
  // RocksDB.
  //
  // This is marked mutable because read path member functions (which are const) are using this.
  mutable yb::util::PendingOperationCounter pending_op_counter_;

  std::shared_ptr<yb::docdb::HistoryRetentionPolicy> retention_policy_;

  std::unique_ptr<TransactionCoordinator> transaction_coordinator_;

  std::unique_ptr<TransactionParticipant> transaction_participant_;

  std::shared_future<client::YBClient*> client_future_;

  // Created only when secondary indexes are present.
  boost::optional<client::TransactionManager> transaction_manager_;
  boost::optional<client::YBMetaDataCache> metadata_cache_;

  // Created only if it is a unique index tablet.
  boost::optional<Schema> unique_index_key_schema_;

  std::atomic<int64_t> last_committed_write_index_{0};

  HybridTimeLeaseProvider ht_lease_provider_;

  // (end of protected section)
  // ==============================================================================================

 private:
  HybridTime DoGetSafeTime(
      RequireLease require_lease, HybridTime min_allowed, CoarseTimePoint deadline) const override;

  void UpdateQLIndexes(std::unique_ptr<WriteOperation> operation);
  void CompleteQLWriteBatch(std::unique_ptr<WriteOperation> operation, const Status& status);

  Result<bool> IntentsDbFlushFilter(const rocksdb::MemTable& memtable);

  template <class Ids>
  CHECKED_STATUS RemoveIntentsImpl(const RemoveIntentsData& data, const Ids& ids);

  std::function<rocksdb::MemTableFilter()> mem_table_flush_filter_factory_;

  client::LocalTabletFilter local_tablet_filter_;

  std::string log_prefix_suffix_;

  DISALLOW_COPY_AND_ASSIGN(Tablet);
};

// A helper class to manage read transactions. Grabs and registers a read point with the tablet
// when created, and deregisters the read point when this object is destructed.
class ScopedReadOperation {
 public:
  ScopedReadOperation() : tablet_(nullptr) {}
  ScopedReadOperation(ScopedReadOperation&& rhs)
      : tablet_(rhs.tablet_), read_time_(rhs.read_time_) {
    rhs.tablet_ = nullptr;
  }

  static Result<ScopedReadOperation> Create(
      AbstractTablet* tablet,
      RequireLease require_lease,
      ReadHybridTime read_time);

  ScopedReadOperation(const ScopedReadOperation&) = delete;
  void operator=(const ScopedReadOperation&) = delete;

  ~ScopedReadOperation();

  const ReadHybridTime& read_time() const { return read_time_; }

  Status status() const { return status_; }

 private:
  explicit ScopedReadOperation(
      AbstractTablet* tablet, RequireLease require_lease, const ReadHybridTime& read_time);

  AbstractTablet* tablet_;
  ReadHybridTime read_time_;
  Status status_;
};

}  // namespace tablet
}  // namespace yb

#endif  // YB_TABLET_TABLET_H_

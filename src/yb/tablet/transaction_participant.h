//
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
//
//

#ifndef YB_TABLET_TRANSACTION_PARTICIPANT_H
#define YB_TABLET_TRANSACTION_PARTICIPANT_H

#include <future>
#include <memory>

#include <boost/optional/optional.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/consensus/opid_util.h"

#include "yb/docdb/doc_key.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/server/server_fwd.h"

#include "yb/util/async_util.h"
#include "yb/util/opid.pb.h"
#include "yb/util/result.h"

namespace rocksdb {

class DB;
class WriteBatch;

}

namespace yb {

class HybridTime;
class TransactionMetadataPB;

namespace tserver {

class TransactionStatePB;

}

namespace tablet {

class TransactionIntentApplier;
class UpdateTxnOperationState;

struct TransactionApplyData {
  int64_t leader_term;
  TransactionId transaction_id;
  consensus::OpId op_id;
  HybridTime commit_ht;
  HybridTime log_ht;
  TabletId status_tablet;

  std::string ToString() const;
};

struct RemoveIntentsData {
  consensus::OpId op_id;
  HybridTime log_ht;
};

// Interface to object that should apply intents in RocksDB when transaction is applying.
class TransactionIntentApplier {
 public:
  virtual CHECKED_STATUS ApplyIntents(const TransactionApplyData& data) = 0;
  virtual CHECKED_STATUS RemoveIntents(
      const RemoveIntentsData& data, const TransactionId& transaction_id) = 0;
  virtual CHECKED_STATUS RemoveIntents(
      const RemoveIntentsData& data, const TransactionIdSet& transactions) = 0;

  virtual HybridTime ApplierSafeTime(HybridTime min_allowed, CoarseTimePoint deadline) = 0;

  // See TransactionParticipant::WaitMinRunningHybridTime below
  virtual void MinRunningHybridTimeSatisfied() = 0;

 protected:
  ~TransactionIntentApplier() {}
};

class TransactionParticipantContext {
 public:
  virtual const std::string& permanent_uuid() const = 0;
  virtual const std::string& tablet_id() const = 0;
  virtual const std::shared_future<client::YBClient*>& client_future() const = 0;
  virtual const server::ClockPtr& clock_ptr() const = 0;

  // Fills RemoveIntentsData with information about replicated state.
  virtual void GetLastReplicatedData(RemoveIntentsData* data) = 0;

  virtual bool Enqueue(rpc::ThreadPoolTask* task) = 0;
  virtual HybridTime Now() = 0;
  virtual void UpdateClock(HybridTime hybrid_time) = 0;
  virtual bool IsLeader() = 0;
  virtual void SubmitUpdateTransaction(
      std::unique_ptr<UpdateTxnOperationState> state, int64_t term) = 0;

  // Returns hybrid time that lower than any future transaction apply record.
  virtual HybridTime SafeTimeForTransactionParticipant() = 0;

 protected:
  ~TransactionParticipantContext() {}
};

// TransactionParticipant manages running transactions, i.e. transactions that have intents in
// appropriate tablet. Since this class manages transactions of tablet there is separate class
// instance per tablet.
class TransactionParticipant : public TransactionStatusManager {
 public:
  TransactionParticipant(
      TransactionParticipantContext* context, TransactionIntentApplier* applier,
      const scoped_refptr<MetricEntity>& entity);
  virtual ~TransactionParticipant();

  // Notify participant that this context is ready and it could start performing its requests.
  void Start();

  // Adds new running transaction.
  MUST_USE_RESULT bool Add(
      const TransactionMetadataPB& data, rocksdb::WriteBatch *write_batch);

  Result<TransactionMetadata> PrepareMetadata(const TransactionMetadataPB& id) override;

  boost::optional<std::pair<TransactionMetadata, IntraTxnWriteId>> MetadataWithWriteId(
      const TransactionId& id);

  void UpdateLastWriteId(const TransactionId& id, IntraTxnWriteId value);

  HybridTime LocalCommitTime(const TransactionId& id) override;

  void RequestStatusAt(const StatusRequest& request) override;

  void Abort(const TransactionId& id, TransactionStatusCallback callback) override;

  void Handle(std::unique_ptr<tablet::UpdateTxnOperationState> request, int64_t term);

  void Cleanup(TransactionIdSet&& set) override;

  // Used to pass arguments to ProcessReplicated.
  struct ReplicatedData {
    int64_t leader_term;
    const tserver::TransactionStatePB& state;
    const consensus::OpId& op_id;
    HybridTime hybrid_time;
    bool already_applied;
  };

  CHECKED_STATUS ProcessReplicated(const ReplicatedData& data);

  void SetDB(rocksdb::DB* db, const docdb::KeyBounds* key_bounds);

  CHECKED_STATUS CheckAborted(const TransactionId& id);

  void FillPriorities(
      boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) override;

  TransactionParticipantContext* context() const;

  HybridTime MinRunningHybridTime() const;

  // When minimal start hybrid time of running transaction will be at least `ht` applier
  // method `MinRunningHybridTimeSatisfied` will be invoked.
  void WaitMinRunningHybridTime(HybridTime ht);

  size_t TEST_GetNumRunningTransactions() const;

  // Returns pair of number of intents and number of transactions.
  std::pair<size_t, size_t> TEST_CountIntents() const;

 private:
  int64_t RegisterRequest() override;
  void UnregisterRequest(int64_t request) override;

  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace tablet
} // namespace yb

#endif // YB_TABLET_TRANSACTION_PARTICIPANT_H

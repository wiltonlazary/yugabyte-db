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
#ifndef YB_MASTER_ASYNC_RPC_TASKS_H
#define YB_MASTER_ASYNC_RPC_TASKS_H

#include <atomic>
#include <string>

#include <boost/optional/optional.hpp>

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/tserver/tserver_admin.pb.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/common/entity_ids.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/server/monitored_task.h"
#include "yb/util/status.h"
#include "yb/util/memory/memory.h"


namespace yb {

class ThreadPool;

namespace consensus {
class ConsensusServiceProxy;
}

namespace tserver {
class TabletServerAdminServiceProxy;
class TabletServerServiceProxy;
}

namespace master {

class TSDescriptor;
class Master;

class TableInfo;
class TabletInfo;

// Interface used by RetryingTSRpcTask to pick the tablet server to
// send the next RPC to.
class TSPicker {
 public:
  TSPicker() {}
  virtual ~TSPicker() {}

  // Sets *ts_desc to the tablet server to contact for the next RPC.
  //
  // This assumes that TSDescriptors are never deleted by the master,
  // so the caller does not take ownership of the returned pointer.
  virtual Status PickReplica(TSDescriptor** ts_desc) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(TSPicker);
};

// Implementation of TSPicker which sends to a specific tablet server,
// identified by its UUID.
class PickSpecificUUID : public TSPicker {
 public:
  PickSpecificUUID(Master* master, std::string ts_uuid)
      : master_(master), ts_uuid_(std::move(ts_uuid)) {}

  Status PickReplica(TSDescriptor** ts_desc) override;

 private:
  Master* const master_;
  const std::string ts_uuid_;

  DISALLOW_COPY_AND_ASSIGN(PickSpecificUUID);
};

// Implementation of TSPicker which locates the current leader replica,
// and sends the RPC to that server.
class PickLeaderReplica : public TSPicker {
 public:
  explicit PickLeaderReplica(const scoped_refptr<TabletInfo>& tablet)
    : tablet_(tablet) {}

  Status PickReplica(TSDescriptor** ts_desc) override;

 private:
  const scoped_refptr<TabletInfo> tablet_;
};

// A background task which continuously retries sending an RPC to a tablet server.
//
// The target tablet server is refreshed before each RPC by consulting the provided
// TSPicker implementation.
class RetryingTSRpcTask : public MonitoredTask {
 public:
  RetryingTSRpcTask(Master *master,
                    ThreadPool* callback_pool,
                    gscoped_ptr<TSPicker> replica_picker,
                    const scoped_refptr<TableInfo>& table);

  // Send the subclass RPC request.
  Status Run();

  // Abort this task and return its value before it was successfully aborted. If the task entered
  // a different terminal state before we were able to abort it, return that state.
  MonitoredTaskState AbortAndReturnPrevState() override;

  MonitoredTaskState state() const override {
    return state_.load(std::memory_order_acquire);
  }

  MonoTime start_timestamp() const override { return start_ts_; }
  MonoTime completion_timestamp() const override { return end_ts_; }

 protected:
  // Send an RPC request and register a callback.
  // The implementation must return true if the callback was registered, and
  // false if an error occurred and no callback will occur.
  virtual bool SendRequest(int attempt) = 0;

  // Handle the response from the RPC request. On success, MarkSuccess() must
  // be called to mutate the state_ variable. If retry is desired, then
  // no state change is made. Retries will automatically be attempted as long
  // as the state is MonitoredTaskState::kRunning and deadline_ has not yet passed.
  virtual void HandleResponse(int attempt) = 0;

  // Return the id of the tablet that is the subject of the async request.
  virtual TabletId tablet_id() const = 0;

  virtual Status ResetTSProxy();

  // Overridable log prefix with reasonable default.
  std::string LogPrefix() const {
    return strings::Substitute("$0 (task=$1, state=$2): ", description(), this, ToString(state()));
  }

  bool PerformStateTransition(MonitoredTaskState expected, MonitoredTaskState new_state)
      WARN_UNUSED_RESULT {
    return state_.compare_exchange_strong(expected, new_state);
  }

  void TransitionToTerminalState(MonitoredTaskState expected, MonitoredTaskState terminal_state);

  static bool IsStateTerminal(MonitoredTaskState state) {
    return state == MonitoredTaskState::kComplete || state == MonitoredTaskState::kFailed ||
           state == MonitoredTaskState::kAborted;
  }

  void AbortTask();

  // Callback meant to be invoked from asynchronous RPC service proxy calls.
  void RpcCallback();

  auto BindRpcCallback() {
    return std::bind(&RetryingTSRpcTask::RpcCallback, shared_from(this));
  }

  // Handle the actual work of the RPC callback. This is run on the master's worker
  // pool, rather than a reactor thread, so it may do blocking IO operations.
  void DoRpcCallback();

  // Called when the async task unregisters either successfully or unsuccessfully.
  virtual void UnregisterAsyncTaskCallback();

  Master* const master_;
  ThreadPool* const callback_pool_;
  const gscoped_ptr<TSPicker> replica_picker_;
  const scoped_refptr<TableInfo> table_;

  MonoTime start_ts_;
  MonoTime end_ts_;
  MonoTime deadline_;

  int attempt_;
  rpc::RpcController rpc_;
  TSDescriptor* target_ts_desc_ = nullptr;
  std::shared_ptr<tserver::TabletServerServiceProxy> ts_proxy_;
  std::shared_ptr<tserver::TabletServerAdminServiceProxy> ts_admin_proxy_;
  std::shared_ptr<consensus::ConsensusServiceProxy> consensus_proxy_;

  std::atomic<rpc::ScheduledTaskId> reactor_task_id_{rpc::kInvalidTaskId};

 private:
  // Returns true if we should impose a limit in the number of retries for this task type.
  bool RetryLimitTaskType() {
    return type() != ASYNC_CREATE_REPLICA && type() != ASYNC_DELETE_REPLICA;
  }

  // Returns true if we should not retry for this task type.
  bool NoRetryTaskType() {
    return type() == ASYNC_FLUSH_TABLETS;
  }

  // Reschedules the current task after a backoff delay.
  // Returns false if the task was not rescheduled due to reaching the maximum
  // timeout or because the task is no longer in a running state.
  // Returns true if rescheduling the task was successful.
  bool RescheduleWithBackoffDelay();

  // Callback for Reactor delayed task mechanism. Called either when it is time
  // to execute the delayed task (with status == OK) or when the task
  // is cancelled, i.e. when the scheduling timer is shut down (status != OK).
  void RunDelayedTask(const Status& status);

  // Clean up request and release resources. May call 'delete this'.
  void UnregisterAsyncTask();

  // Only abort this task on reactor if it has been scheduled.
  void AbortIfScheduled();

  // Use state() and MarkX() accessors.
  std::atomic<MonitoredTaskState> state_;
};

// RetryingTSRpcTask subclass which always retries the same tablet server,
// identified by its UUID.
class RetrySpecificTSRpcTask : public RetryingTSRpcTask {
 public:
  RetrySpecificTSRpcTask(Master* master,
                         ThreadPool* callback_pool,
                         const std::string& permanent_uuid,
                         const scoped_refptr<TableInfo>& table)
    : RetryingTSRpcTask(master,
                        callback_pool,
                        gscoped_ptr<TSPicker>(new PickSpecificUUID(master, permanent_uuid)),
                        table),
      permanent_uuid_(permanent_uuid) {
  }

 protected:
  const std::string permanent_uuid_;
};

// Fire off the async create tablet.
// This requires that the new tablet info is locked for write, and the
// consensus configuration information has been filled into the 'dirty' data.
class AsyncCreateReplica : public RetrySpecificTSRpcTask {
 public:
  AsyncCreateReplica(Master *master,
                     ThreadPool *callback_pool,
                     const std::string& permanent_uuid,
                     const scoped_refptr<TabletInfo>& tablet);

  Type type() const override { return ASYNC_CREATE_REPLICA; }

  std::string type_name() const override { return "Create Tablet"; }

  std::string description() const override {
    return "CreateTablet RPC for tablet " + tablet_id_ + " on TS " + permanent_uuid_;
  }

 protected:
  TabletId tablet_id() const override { return tablet_id_; }

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

 private:
  const TabletId tablet_id_;
  tserver::CreateTabletRequestPB req_;
  tserver::CreateTabletResponsePB resp_;
};

// Send a DeleteTablet() RPC request.
class AsyncDeleteReplica : public RetrySpecificTSRpcTask {
 public:
  AsyncDeleteReplica(
      Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
      const scoped_refptr<TableInfo>& table, TabletId tablet_id,
      tablet::TabletDataState delete_type,
      boost::optional<int64_t> cas_config_opid_index_less_or_equal,
      std::string reason)
      : RetrySpecificTSRpcTask(master, callback_pool, permanent_uuid, table),
        tablet_id_(std::move(tablet_id)),
        delete_type_(delete_type),
        cas_config_opid_index_less_or_equal_(
            std::move(cas_config_opid_index_less_or_equal)),
        reason_(std::move(reason)) {}

  Type type() const override { return ASYNC_DELETE_REPLICA; }

  std::string type_name() const override { return "Delete Tablet"; }

  std::string description() const override {
    return tablet_id_ + " Delete Tablet RPC for TS=" + permanent_uuid_;
  }

 protected:
  TabletId tablet_id() const override { return tablet_id_; }

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  void UnregisterAsyncTaskCallback() override;

  const TabletId tablet_id_;
  const tablet::TabletDataState delete_type_;
  const boost::optional<int64_t> cas_config_opid_index_less_or_equal_;
  const std::string reason_;
  tserver::DeleteTabletResponsePB resp_;
};

// Send the "Alter Table" with the latest table schema to the leader replica
// for the tablet.
// Keeps retrying until we get an "ok" response.
//  - Alter completed
//  - Tablet has already a newer version
//    (which may happen in case of concurrent alters, or in case a previous attempt timed
//     out but was actually applied).
class AsyncAlterTable : public RetryingTSRpcTask {
 public:
  AsyncAlterTable(Master *master,
                  ThreadPool* callback_pool,
                  const scoped_refptr<TabletInfo>& tablet);

  Type type() const override { return ASYNC_ALTER_TABLE; }

  std::string type_name() const override { return "Alter Table"; }

  std::string description() const override;

 private:
  TabletId tablet_id() const override;

  TabletServerId permanent_uuid() const;

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  uint32_t schema_version_;
  scoped_refptr<TabletInfo> tablet_;
  tserver::ChangeMetadataResponsePB resp_;
};

class AsyncCopartitionTable : public RetryingTSRpcTask {
 public:
  AsyncCopartitionTable(Master *master,
                        ThreadPool* callback_pool,
                        const scoped_refptr<TabletInfo>& tablet,
                        const scoped_refptr<TableInfo>& table);

  Type type() const override { return ASYNC_COPARTITION_TABLE; }

  std::string type_name() const override { return "Copartition Table"; }

  std::string description() const override;

 private:
  TabletId tablet_id() const override;

  TabletServerId permanent_uuid() const;

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  scoped_refptr<TabletInfo> tablet_;
  scoped_refptr<TableInfo> table_;
  tserver::CopartitionTableResponsePB resp_;
};

// Send a Truncate() RPC request.
class AsyncTruncate : public RetryingTSRpcTask {
 public:
  AsyncTruncate(Master* master,
                ThreadPool* callback_pool,
                const scoped_refptr<TabletInfo>& tablet);

  Type type() const override { return ASYNC_TRUNCATE_TABLET; }

  std::string type_name() const override { return "Truncate Tablet"; }

  std::string description() const override;

 protected:
  TabletId tablet_id() const override;

  TabletServerId permanent_uuid() const;

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  scoped_refptr<TabletInfo> tablet_;
  tserver::TruncateResponsePB resp_;
};

class CommonInfoForRaftTask : public RetryingTSRpcTask {
 public:
  CommonInfoForRaftTask(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      const consensus::ConsensusStatePB& cstate, const std::string& change_config_ts_uuid);

  TabletId tablet_id() const override;

  virtual std::string change_config_ts_uuid() const { return change_config_ts_uuid_; }

 protected:
  // Used by SendOrReceiveData. Return's false if RPC should not be sent.
  virtual bool PrepareRequest(int attempt) = 0;

  TabletServerId permanent_uuid() const;

  const scoped_refptr<TabletInfo> tablet_;
  const consensus::ConsensusStatePB cstate_;

  // The uuid of the TabletServer we intend to change in the config, for example, the one we are
  // adding to a new config, or the one we intend to remove from the current config.
  //
  // This is different from the target_ts_desc_, which points to the tablet server to whom we
  // issue the ChangeConfig RPC call, which is the Leader in the case of this class, due to the
  // PickLeaderReplica set in the constructor.
  const std::string change_config_ts_uuid_;
};

class AsyncChangeConfigTask : public CommonInfoForRaftTask {
 public:
  AsyncChangeConfigTask(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      const consensus::ConsensusStatePB& cstate, const std::string& change_config_ts_uuid)
      : CommonInfoForRaftTask(master, callback_pool, tablet, cstate, change_config_ts_uuid) {}

  Type type() const override { return ASYNC_CHANGE_CONFIG; }

  std::string type_name() const override { return "ChangeConfig"; }

  std::string description() const override;

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  consensus::ChangeConfigRequestPB req_;
  consensus::ChangeConfigResponsePB resp_;
};

class AsyncAddServerTask : public AsyncChangeConfigTask {
 public:
  AsyncAddServerTask(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      consensus::RaftPeerPB::MemberType member_type, const consensus::ConsensusStatePB& cstate,
      const std::string& change_config_ts_uuid)
      : AsyncChangeConfigTask(master, callback_pool, tablet, cstate, change_config_ts_uuid),
        member_type_(member_type) {}

  Type type() const override { return ASYNC_ADD_SERVER; }

  std::string type_name() const override { return "AddServer ChangeConfig"; }

 protected:
  bool PrepareRequest(int attempt) override;

 private:
  // PRE_VOTER or PRE_OBSERVER (for async replicas).
  consensus::RaftPeerPB::MemberType member_type_;
};

// Task to remove a tablet server peer from an overly-replicated tablet config.
class AsyncRemoveServerTask : public AsyncChangeConfigTask {
 public:
  AsyncRemoveServerTask(
      Master* master, ThreadPool* callback_pool, const scoped_refptr<TabletInfo>& tablet,
      const consensus::ConsensusStatePB& cstate, const std::string& change_config_ts_uuid)
      : AsyncChangeConfigTask(master, callback_pool, tablet, cstate, change_config_ts_uuid) {}

  Type type() const override { return ASYNC_REMOVE_SERVER; }

  std::string type_name() const override { return "RemoveServer ChangeConfig"; }

 protected:
  bool PrepareRequest(int attempt) override;
};

// Task to step down tablet server leader and optionally to remove it from an overly-replicated
// tablet config.
class AsyncTryStepDown : public CommonInfoForRaftTask {
 public:
  AsyncTryStepDown(
      Master* master,
      ThreadPool* callback_pool,
      const scoped_refptr<TabletInfo>& tablet,
      const consensus::ConsensusStatePB& cstate,
      const std::string& change_config_ts_uuid,
      bool should_remove,
      const std::string& new_leader_uuid = "")
      : CommonInfoForRaftTask(master, callback_pool, tablet, cstate, change_config_ts_uuid),
        should_remove_(should_remove),
        new_leader_uuid_(new_leader_uuid) {}

  Type type() const override { return ASYNC_TRY_STEP_DOWN; }

  std::string type_name() const override { return "Stepdown Leader"; }

  std::string description() const override {
    return "Async Leader Stepdown";
  }

  std::string new_leader_uuid() const { return new_leader_uuid_; }

 protected:
  bool PrepareRequest(int attempt) override;
  bool SendRequest(int attempt) override;
  void HandleResponse(int attempt) override;

  const bool should_remove_;
  const std::string new_leader_uuid_;
  consensus::LeaderStepDownRequestPB stepdown_req_;
  consensus::LeaderStepDownResponsePB stepdown_resp_;
};

} // namespace master
} // namespace yb

#endif // YB_MASTER_ASYNC_RPC_TASKS_H

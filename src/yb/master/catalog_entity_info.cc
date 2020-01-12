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

#include <string>
#include <mutex>

#include "yb/master/catalog_entity_info.h"
#include "yb/util/format.h"
#include "yb/util/locks.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/common/wire_protocol.h"

using std::string;

using strings::Substitute;

DECLARE_int32(tserver_unresponsive_timeout_ms);

namespace yb {
namespace master {

// ================================================================================================
// TabletReplica
// ================================================================================================

string TabletReplica::ToString() const {
  return Format("{ ts_desc: $0 state: $1 role: $2 member_type: $3 time since update: $4ms}",
                ts_desc->permanent_uuid(),
                tablet::RaftGroupStatePB_Name(state),
                consensus::RaftPeerPB_Role_Name(role),
                consensus::RaftPeerPB::MemberType_Name(member_type),
                MonoTime::Now().GetDeltaSince(time_updated).ToMilliseconds());
}

void TabletReplica::UpdateFrom(const TabletReplica& source) {
  state = source.state;
  role = source.role;
  member_type = source.member_type;
  time_updated = MonoTime::Now();
}

bool TabletReplica::IsStale() const {
  MonoTime now(MonoTime::Now());
  if (now.GetDeltaSince(time_updated).ToMilliseconds() >=
      GetAtomicFlag(&FLAGS_tserver_unresponsive_timeout_ms)) {
    return true;
  }
  return false;
}

bool TabletReplica::IsStarting() const {
  return (state == tablet::NOT_STARTED || state == tablet::BOOTSTRAPPING);
}

// ================================================================================================
// TabletInfo
// ================================================================================================

class TabletInfo::LeaderChangeReporter {
 public:
  explicit LeaderChangeReporter(TabletInfo* info)
      : info_(info), old_leader_(info->GetLeaderUnlocked()) {
  }

  ~LeaderChangeReporter() {
    auto new_leader = info_->GetLeaderUnlocked();
    if (old_leader_ != new_leader) {
      LOG(INFO) << "T " << info_->tablet_id() << ": Leader changed from "
                << yb::ToString(old_leader_) << " to " << yb::ToString(new_leader);
    }
  }
 private:
  TabletInfo* info_;
  TSDescriptor* old_leader_;
};


TabletInfo::TabletInfo(const scoped_refptr<TableInfo>& table, TabletId tablet_id)
    : tablet_id_(std::move(tablet_id)),
      table_(table),
      last_update_time_(MonoTime::Now()),
      reported_schema_version_(0) {}

TabletInfo::~TabletInfo() {
}

void TabletInfo::SetReplicaLocations(ReplicaMap replica_locations) {
  std::lock_guard<simple_spinlock> l(lock_);
  LeaderChangeReporter leader_change_reporter(this);
  last_update_time_ = MonoTime::Now();
  replica_locations_ = std::move(replica_locations);
}

Result<TSDescriptor*> TabletInfo::GetLeader() const {
  std::lock_guard<simple_spinlock> l(lock_);
  auto result = GetLeaderUnlocked();
  if (result) {
    return result;
  }

  return STATUS_FORMAT(
      NotFound,
      "No leader found for tablet $0 with $1 replicas: $2.",
      ToString(), replica_locations_.size(), replica_locations_);
}

TSDescriptor* TabletInfo::GetLeaderUnlocked() const {
  for (const auto& pair : replica_locations_) {
    if (pair.second.role == consensus::RaftPeerPB::LEADER) {
      return pair.second.ts_desc;
    }
  }
  return nullptr;
}

void TabletInfo::GetReplicaLocations(ReplicaMap* replica_locations) const {
  std::lock_guard<simple_spinlock> l(lock_);
  *replica_locations = replica_locations_;
}

void TabletInfo::UpdateReplicaLocations(const TabletReplica& replica) {
  std::lock_guard<simple_spinlock> l(lock_);
  LeaderChangeReporter leader_change_reporter(this);
  auto it = replica_locations_.find(replica.ts_desc->permanent_uuid());
  if (it == replica_locations_.end()) {
    replica_locations_.emplace(replica.ts_desc->permanent_uuid(), replica);
    return;
  }
  it->second.UpdateFrom(replica);
}

void TabletInfo::set_last_update_time(const MonoTime& ts) {
  std::lock_guard<simple_spinlock> l(lock_);
  last_update_time_ = ts;
}

MonoTime TabletInfo::last_update_time() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return last_update_time_;
}

bool TabletInfo::set_reported_schema_version(uint32_t version) {
  std::lock_guard<simple_spinlock> l(lock_);
  if (version > reported_schema_version_) {
    reported_schema_version_ = version;
    return true;
  }
  return false;
}

uint32_t TabletInfo::reported_schema_version() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return reported_schema_version_;
}

bool TabletInfo::colocated() const {
  auto l = LockForRead();
  return l->data().pb.colocated();
}

std::string TabletInfo::ToString() const {
  return Substitute("$0 (table $1)", tablet_id_,
                    (table_ != nullptr ? table_->ToString() : "MISSING"));
}

void TabletInfo::RegisterLeaderStepDownFailure(const TabletServerId& dest_leader,
                                               MonoDelta time_since_stepdown_failure) {
  std::lock_guard<simple_spinlock> l(lock_);
  leader_stepdown_failure_times_[dest_leader] = MonoTime::Now() - time_since_stepdown_failure;
}

void TabletInfo::GetLeaderStepDownFailureTimes(MonoTime forget_failures_before,
                                               LeaderStepDownFailureTimes* dest) {
  std::lock_guard<simple_spinlock> l(lock_);
  for (auto iter = leader_stepdown_failure_times_.begin();
       iter != leader_stepdown_failure_times_.end(); ) {
    if (iter->second < forget_failures_before) {
      iter = leader_stepdown_failure_times_.erase(iter);
    } else {
      iter++;
    }
  }
  *dest = leader_stepdown_failure_times_;
}

void PersistentTabletInfo::set_state(SysTabletsEntryPB::State state, const string& msg) {
  pb.set_state(state);
  pb.set_state_msg(msg);
}

// ================================================================================================
// TableInfo
// ================================================================================================

TableInfo::TableInfo(TableId table_id, scoped_refptr<TasksTracker> tasks_tracker)
    : table_id_(std::move(table_id)),
      tasks_tracker_(tasks_tracker) {
}

TableInfo::~TableInfo() {
}

const TableName TableInfo::name() const {
  auto l = LockForRead();
  return l->data().pb.name();
}

bool TableInfo::is_running() const {
  auto l = LockForRead();
  return l->data().is_running();
}

std::string TableInfo::ToString() const {
  auto l = LockForRead();
  return Substitute("$0 [id=$1]", l->data().pb.name(), table_id_);
}

const NamespaceId TableInfo::namespace_id() const {
  auto l = LockForRead();
  return l->data().namespace_id();
}

const Status TableInfo::GetSchema(Schema* schema) const {
  auto l = LockForRead();
  return SchemaFromPB(l->data().schema(), schema);
}

bool TableInfo::colocated() const {
  auto l = LockForRead();
  return l->data().pb.colocated();
}

const std::string TableInfo::indexed_table_id() const {
  auto l = LockForRead();
  return l->data().pb.has_index_info()
             ? l->data().pb.index_info().indexed_table_id()
             : l->data().pb.has_indexed_table_id() ? l->data().pb.indexed_table_id() : "";
}

bool TableInfo::is_local_index() const {
  auto l = LockForRead();
  return l->data().pb.has_index_info() ? l->data().pb.index_info().is_local()
                                       : l->data().pb.is_local_index();
}

bool TableInfo::is_unique_index() const {
  auto l = LockForRead();
  return l->data().pb.has_index_info() ? l->data().pb.index_info().is_unique()
                                       : l->data().pb.is_unique_index();
}

TableType TableInfo::GetTableType() const {
  auto l = LockForRead();
  return l->data().pb.table_type();
}

bool TableInfo::RemoveTablet(const std::string& partition_key_start) {
  std::lock_guard<decltype(lock_)> l(lock_);
  return EraseKeyReturnValuePtr(&tablet_map_, partition_key_start) != NULL;
}

void TableInfo::AddTablet(TabletInfo *tablet) {
  std::lock_guard<decltype(lock_)> l(lock_);
  AddTabletUnlocked(tablet);
}

void TableInfo::AddTablets(const vector<TabletInfo*>& tablets) {
  std::lock_guard<decltype(lock_)> l(lock_);
  for (TabletInfo *tablet : tablets) {
    AddTabletUnlocked(tablet);
  }
}

void TableInfo::AddTabletUnlocked(TabletInfo* tablet) {
  TabletInfo* old = nullptr;
  if (UpdateReturnCopy(&tablet_map_,
                       tablet->metadata().dirty().pb.partition().partition_key_start(),
                       tablet, &old)) {
    VLOG(1) << "Replaced tablet " << old->tablet_id() << " with " << tablet->tablet_id();
    // TODO: can we assert that the replaced tablet is not in Running state?
    // May be a little tricky since we don't know whether to look at its committed or
    // uncommitted state.
  }
}

void TableInfo::GetTabletsInRange(const GetTableLocationsRequestPB* req, TabletInfos* ret) const {
  shared_lock<decltype(lock_)> l(lock_);
  int32_t max_returned_locations = req->max_returned_locations();

  TableInfo::TabletInfoMap::const_iterator it, it_end;
  if (req->has_partition_key_start()) {
    it = tablet_map_.upper_bound(req->partition_key_start());
    if (it != tablet_map_.begin()) {
      --it;
    }
  } else {
    it = tablet_map_.begin();
  }
  if (req->has_partition_key_end()) {
    it_end = tablet_map_.upper_bound(req->partition_key_end());
  } else {
    it_end = tablet_map_.end();
  }

  int32_t count = 0;
  for (; it != it_end && count < max_returned_locations; ++it) {
    ret->push_back(make_scoped_refptr(it->second));
    count++;
  }
}

bool TableInfo::IsAlterInProgress(uint32_t version) const {
  shared_lock<decltype(lock_)> l(lock_);
  for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
    if (e.second->reported_schema_version() < version) {
      VLOG(3) << "Table " << table_id_ << " ALTER in progress due to tablet "
              << e.second->ToString() << " because reported schema "
              << e.second->reported_schema_version() << " < expected " << version;
      return true;
    }
  }
  return false;
}
bool TableInfo::AreAllTabletsDeleted() const {
  shared_lock<decltype(lock_)> l(lock_);
  for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
    auto tablet_lock = e.second->LockForRead();
    if (!tablet_lock->data().is_deleted()) {
      return false;
    }
  }
  return true;
}

bool TableInfo::IsCreateInProgress() const {
  shared_lock<decltype(lock_)> l(lock_);
  for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
    auto tablet_lock = e.second->LockForRead();
    if (!tablet_lock->data().is_running()) {
      return true;
    }
  }
  return false;
}

void TableInfo::SetCreateTableErrorStatus(const Status& status) {
  std::lock_guard<decltype(lock_)> l(lock_);
  create_table_error_ = status;
}

Status TableInfo::GetCreateTableErrorStatus() const {
  shared_lock<decltype(lock_)> l(lock_);
  return create_table_error_;
}

std::size_t TableInfo::NumTasks() const {
  shared_lock<decltype(lock_)> l(lock_);
  return pending_tasks_.size();
}

bool TableInfo::HasTasks() const {
  shared_lock<decltype(lock_)> l(lock_);
  return !pending_tasks_.empty();
}

bool TableInfo::HasTasks(MonitoredTask::Type type) const {
  shared_lock<decltype(lock_)> l(lock_);
  for (auto task : pending_tasks_) {
    if (task->type() == type) {
      return true;
    }
  }
  return false;
}

void TableInfo::AddTask(std::shared_ptr<MonitoredTask> task) {
  bool abort_task = false;
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    if (!closing_) {
      pending_tasks_.insert(task);
      if (tasks_tracker_) {
        tasks_tracker_->AddTask(task);
      }
    } else {
      abort_task = true;
    }
  }
  // We need to abort these tasks without holding the lock because when a task is destroyed it tries
  // to acquire the same lock to remove itself from pending_tasks_.
  if (abort_task) {
    task->AbortAndReturnPrevState();
  }
}

void TableInfo::RemoveTask(const std::shared_ptr<MonitoredTask>& task) {
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    pending_tasks_.erase(task);
  }
  VLOG(1) << __func__ << " Removed task " << task.get() << " " << task->description();
}

// Aborts tasks which have their rpc in progress, rest of them are aborted and also erased
// from the pending list.
void TableInfo::AbortTasks() {
  AbortTasksAndCloseIfRequested( /* close */ false);
}

void TableInfo::AbortTasksAndClose() {
  AbortTasksAndCloseIfRequested( /* close */ true);
}

void TableInfo::AbortTasksAndCloseIfRequested(bool close) {
  std::vector<std::shared_ptr<MonitoredTask>> abort_tasks;
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    if (close) {
      closing_ = true;
    }
    abort_tasks.reserve(pending_tasks_.size());
    abort_tasks.assign(pending_tasks_.cbegin(), pending_tasks_.cend());
  }
  // We need to abort these tasks without holding the lock because when a task is destroyed it tries
  // to acquire the same lock to remove itself from pending_tasks_.
  for (const auto& task : abort_tasks) {
    VLOG(1) << __func__ << " Aborting task " << task.get() << " " << task->description();
    task->AbortAndReturnPrevState();
  }
}

void TableInfo::WaitTasksCompletion() {
  int wait_time = 5;
  while (1) {
    std::vector<std::shared_ptr<MonitoredTask>> waiting_on_for_debug;
    {
      shared_lock<decltype(lock_)> l(lock_);
      if (pending_tasks_.empty()) {
        break;
      } else if (VLOG_IS_ON(1)) {
        waiting_on_for_debug.reserve(pending_tasks_.size());
        waiting_on_for_debug.assign(pending_tasks_.cbegin(), pending_tasks_.cend());
      }
    }
    for (const auto& task : waiting_on_for_debug) {
      VLOG(1) << "Waiting for Aborting task " << task.get() << " " << task->description();
    }
    base::SleepForMilliseconds(wait_time);
    wait_time = std::min(wait_time * 5 / 4, 10000);
  }
}

std::unordered_set<std::shared_ptr<MonitoredTask>> TableInfo::GetTasks() {
  shared_lock<decltype(lock_)> l(lock_);
  return pending_tasks_;
}

void TableInfo::GetAllTablets(TabletInfos *ret) const {
  ret->clear();
  shared_lock<decltype(lock_)> l(lock_);
  for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
    ret->push_back(make_scoped_refptr(e.second));
  }
}

IndexInfo TableInfo::GetIndexInfo(const TableId& index_id) const {
  auto l = LockForRead();
  for (const auto& index_info_pb : l->data().pb.indexes()) {
    if (index_info_pb.table_id() == index_id) {
      return IndexInfo(index_info_pb);
    }
  }
  return IndexInfo();
}

void PersistentTableInfo::set_state(SysTablesEntryPB::State state, const string& msg) {
  pb.set_state(state);
  pb.set_state_msg(msg);
}

// ================================================================================================
// DeletedTableInfo
// ================================================================================================

DeletedTableInfo::DeletedTableInfo(const TableInfo* table) : table_id_(table->id()) {
  vector<scoped_refptr<TabletInfo>> tablets;
  table->GetAllTablets(&tablets);

  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    auto tablet_lock = tablet->LockForRead();
    TabletInfo::ReplicaMap replica_locations;
    tablet->GetReplicaLocations(&replica_locations);

    for (const TabletInfo::ReplicaMap::value_type& r : replica_locations) {
      tablet_set_.insert(TabletSet::value_type(
          r.second.ts_desc->permanent_uuid(), tablet->id()));
    }
  }
}

std::size_t DeletedTableInfo::NumTablets() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return tablet_set_.size();
}

bool DeletedTableInfo::HasTablets() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return !tablet_set_.empty();
}

void DeletedTableInfo::DeleteTablet(const TabletKey& key) {
  std::lock_guard<simple_spinlock> l(lock_);
  tablet_set_.erase(key);
}

void DeletedTableInfo::AddTabletsToMap(DeletedTabletMap* tablet_map) {
  std::lock_guard<simple_spinlock> l(lock_);
  for (const TabletKey& key : tablet_set_) {
    tablet_map->insert(DeletedTabletMap::value_type(key, this));
  }
}

// ================================================================================================
// NamespaceInfo
// ================================================================================================

NamespaceInfo::NamespaceInfo(NamespaceId ns_id) : namespace_id_(std::move(ns_id)) {}

const NamespaceName& NamespaceInfo::name() const {
  auto l = LockForRead();
  return l->data().pb.name();
}

YQLDatabase NamespaceInfo::database_type() const {
  auto l = LockForRead();
  return l->data().pb.database_type();
}

bool NamespaceInfo::colocated() const {
  auto l = LockForRead();
  return l->data().pb.colocated();
}

std::string NamespaceInfo::ToString() const {
  return Substitute("$0 [id=$1]", name(), namespace_id_);
}

// ================================================================================================
// UDTypeInfo
// ================================================================================================

UDTypeInfo::UDTypeInfo(UDTypeId udtype_id) : udtype_id_(std::move(udtype_id)) { }

const UDTypeName& UDTypeInfo::name() const {
  auto l = LockForRead();
  return l->data().pb.name();
}

const NamespaceName& UDTypeInfo::namespace_id() const {
  auto l = LockForRead();
  return l->data().pb.namespace_id();
}

int UDTypeInfo::field_names_size() const {
  auto l = LockForRead();
  return l->data().pb.field_names_size();
}

const string& UDTypeInfo::field_names(int index) const {
  auto l = LockForRead();
  return l->data().pb.field_names(index);
}

int UDTypeInfo::field_types_size() const {
  auto l = LockForRead();
  return l->data().pb.field_types_size();
}

const QLTypePB& UDTypeInfo::field_types(int index) const {
  auto l = LockForRead();
  return l->data().pb.field_types(index);
}

std::string UDTypeInfo::ToString() const {
  auto l = LockForRead();
  return Substitute("$0 [id=$1] {metadata=$2} ", name(), udtype_id_, l->data().pb.DebugString());
}

}  // namespace master
}  // namespace yb

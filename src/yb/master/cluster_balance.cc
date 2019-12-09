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

#include "yb/master/cluster_balance.h"

#include <algorithm>
#include <memory>

#include <boost/thread/locks.hpp>

#include "yb/consensus/quorum_util.h"
#include "yb/master/master.h"
#include "yb/util/flag_tags.h"
#include "yb/util/random_util.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/util/shared_lock.h"

DEFINE_bool(enable_load_balancing,
            true,
            "Choose whether to enable the load balancing algorithm, to move tablets around.");

DEFINE_int32(leader_balance_threshold,
             0,
             "Number of leaders per each tablet server to balance below. If this is configured to "
                 "0 (the default), the leaders will be balanced optimally at extra cost.");

DEFINE_int32(leader_balance_unresponsive_timeout_ms,
             3 * 1000,
             "The period of time that a master can go without receiving a heartbeat from a "
                 "tablet server before considering it unresponsive. Unresponsive servers are "
                 "excluded from leader balancing.");

DEFINE_int32(load_balancer_max_concurrent_tablet_remote_bootstraps,
             2,
             "Maximum number of tablets being remote bootstrapped across the cluster.");

DEFINE_int32(load_balancer_max_over_replicated_tablets,
             1,
             "Maximum number of running tablet replicas that are allowed to be over the configured "
             "replication factor.");

DEFINE_int32(load_balancer_max_concurrent_adds,
             1,
             "Maximum number of tablet peer replicas to add in any one run of the load balancer.");

DEFINE_int32(load_balancer_max_concurrent_removals,
             1,
             "Maximum number of over-replicated tablet peer removals to do in any one run of the "
             "load balancer.");

DEFINE_int32(load_balancer_max_concurrent_moves,
             1,
             "Maximum number of tablet leaders on tablet servers to move in any one run of the "
             "load balancer.");

DEFINE_int32(load_balancer_num_idle_runs,
             5,
             "Number of idle runs of load balancer to deem it idle.");

DEFINE_test_flag(bool, load_balancer_handle_under_replicated_tablets_only, false,
                 "Limit the functionality of the load balancer during tests so tests can make "
                 "progress")

DECLARE_int32(min_leader_stepdown_retry_interval_ms);

namespace yb {
namespace master {

using std::unique_ptr;
using std::make_unique;
using std::string;
using std::set;
using std::vector;
using strings::Substitute;

Status ClusterLoadBalancer::UpdateTabletInfo(TabletInfo* tablet) {
  const auto& table_id = tablet->table()->id();
  // Set the placement information on a per-table basis, only once.
  if (!state_->placement_by_table_.count(table_id)) {
    PlacementInfoPB pb;
    {
      auto l = tablet->table()->LockForRead();
      // If we have a custom per-table placement policy, use that.
      if (l->data().pb.replication_info().has_live_replicas()) {
        pb.CopyFrom(l->data().pb.replication_info().live_replicas());
      } else {
        // Otherwise, default to cluster policy.
        pb.CopyFrom(GetClusterPlacementInfo());
      }
    }
    state_->placement_by_table_[table_id] = std::move(pb);
  }

  return state_->UpdateTablet(tablet);
}

const PlacementInfoPB& ClusterLoadBalancer::GetPlacementByTablet(const TabletId& tablet_id) const {
  const auto& table_id = GetTabletMap().at(tablet_id)->table()->id();
  return state_->placement_by_table_.at(table_id);
}

int ClusterLoadBalancer::get_total_wrong_placement() const {
  return state_->tablets_wrong_placement_.size();
}

int ClusterLoadBalancer::get_total_blacklisted_servers() const {
  return state_->blacklisted_servers_.size();
}

int ClusterLoadBalancer::get_total_leader_blacklisted_servers() const {
  return state_->leader_blacklisted_servers_.size();
}

int ClusterLoadBalancer::get_total_over_replication() const {
  return state_->tablets_over_replicated_.size();
}

int ClusterLoadBalancer::get_total_under_replication() const {
  return state_->tablets_missing_replicas_.size();
}

int ClusterLoadBalancer::get_total_starting_tablets() const { return state_->total_starting_; }

int ClusterLoadBalancer::get_total_running_tablets() const { return state_->total_running_; }

bool ClusterLoadBalancer::IsLoadBalancerEnabled() const {
  return FLAGS_enable_load_balancing && is_enabled_;
}

// Load balancer class.
ClusterLoadBalancer::ClusterLoadBalancer(CatalogManager* cm)
    : random_(GetRandomSeed32()),
      is_enabled_(FLAGS_enable_load_balancing),
      cbuf_activities_(FLAGS_load_balancer_num_idle_runs) {
  ResetState();

  catalog_manager_ = cm;
}

// Reduce remaining_tasks by pending_tasks value, after sanitizing inputs.
void set_remaining(int pending_tasks, int* remaining_tasks) {
  if (pending_tasks > *remaining_tasks) {
    LOG(WARNING) << "Pending tasks > max allowed tasks: " << pending_tasks << " > "
                 << *remaining_tasks;
    *remaining_tasks = 0;
  }
  *remaining_tasks -= pending_tasks;
}

// Needed as we have a unique_ptr to the forward declared ClusterLoadState class.
ClusterLoadBalancer::~ClusterLoadBalancer() = default;

void ClusterLoadBalancer::RunLoadBalancer(Options* options) {
  uint32_t master_errors = 0;

  if (!IsLoadBalancerEnabled()) {
    LOG(INFO) << "Load balancing is not enabled.";
    return;
  }
  std::unique_ptr<enterprise::Options> options_unique_ptr;
  if (options == nullptr) {
    options_unique_ptr = std::make_unique<enterprise::Options>();
    options = options_unique_ptr.get();
  }

  // Lock the CatalogManager maps for the duration of the load balancer run.
  SharedLock<CatalogManager::LockType> l(catalog_manager_->lock_);

  int remaining_adds = options->kMaxConcurrentAdds;
  int remaining_removals = options->kMaxConcurrentRemovals;
  int remaining_leader_moves = options->kMaxConcurrentLeaderMoves;

  // Loop over all tables to get the count of pending tasks.
  int pending_add_replica_tasks = 0;
  int pending_remove_replica_tasks = 0;
  int pending_stepdown_leader_tasks = 0;

  for (const auto& table : GetTableMap()) {
    CountPendingTasks(table.first,
                      &pending_add_replica_tasks,
                      &pending_remove_replica_tasks,
                      &pending_stepdown_leader_tasks);
  }

  if (pending_add_replica_tasks + pending_remove_replica_tasks + pending_stepdown_leader_tasks> 0) {
    LOG(INFO) << "Total pending adds=" << pending_add_replica_tasks << ", total pending removals="
              << pending_remove_replica_tasks << ", total pending leader stepdowns="
              << pending_stepdown_leader_tasks;
  }

  set_remaining(pending_add_replica_tasks, &remaining_adds);
  set_remaining(pending_remove_replica_tasks, &remaining_removals);
  set_remaining(pending_stepdown_leader_tasks, &remaining_leader_moves);

  // At the start of the run, report LB state that might prevent it from running smoothly.
  ReportUnusualLoadBalancerState();

  // Loop over all tables.
  for (const auto& table : GetTableMap()) {

    if (SkipLoadBalancing(*table.second)) {
      continue;
    }

    ResetState();
    state_->options_ = options;

    // Prepare the in-memory structures.
    auto handle_analyze_tablets = AnalyzeTablets(table.first);
    if (!handle_analyze_tablets.ok()) {
      LOG(WARNING) << "Skipping load balancing " << table.first << ": "
        << StatusToString(handle_analyze_tablets);
      master_errors++;
    }

    // Output parameters are unused in the load balancer, but useful in testing.
    TabletId out_tablet_id;
    TabletServerId out_from_ts;
    TabletServerId out_to_ts;

    // Handle adding and moving replicas.
    for ( ; remaining_adds > 0; --remaining_adds) {
      auto handle_add = HandleAddReplicas(&out_tablet_id, &out_from_ts, &out_to_ts);
      if (!handle_add.ok()) {
        LOG(WARNING) << "Skipping add replicas for " << table.first << ": "
                     << StatusToString(handle_add);
        master_errors++;
        break;
      }
      if (!*handle_add) {
        break;
      }
    }
    if (PREDICT_FALSE(FLAGS_load_balancer_handle_under_replicated_tablets_only)) {
      LOG(INFO) << "Skipping remove replicas and leader moves for " << table.first;
      continue;
    }

    // Handle cleanup after over-replication.
    for ( ; remaining_removals > 0; --remaining_removals) {
      auto handle_remove = HandleRemoveReplicas(&out_tablet_id, &out_from_ts);
      if (!handle_remove.ok()) {
        LOG(WARNING) << "Skipping remove replicas for " << table.first << ": "
                     << StatusToString(handle_remove);
        master_errors++;
        break;
      }
      if (!*handle_remove) {
        break;
      }
    }

    // Handle tablet servers with too many leaders.
    for ( ; remaining_leader_moves > 0; --remaining_leader_moves) {
      auto handle_leader = HandleLeaderMoves(&out_tablet_id, &out_from_ts, &out_to_ts);
      if (!handle_leader.ok()) {
        LOG(WARNING) << "Skipping leader moves for " << table.first << ": "
                     << StatusToString(handle_leader);
        master_errors++;
        break;
      }
      if (!*handle_leader) {
        break;
      }
    }

    if (remaining_adds == 0 && remaining_removals == 0 && remaining_leader_moves == 0) {
      break;
    }
  }

  RecordActivity(master_errors);
}

void ClusterLoadBalancer::RecordActivity(uint32_t master_errors) {
  uint32_t table_tasks = 0;
  for (const auto& table : GetTableMap()) {
    table_tasks += table.second->NumTasks();
  }

  uint32_t tserver_tasks = 0;
  TSDescriptorVector ts_descs;
  GetAllReportedDescriptors(&ts_descs);
  for (const auto& ts_desc : ts_descs) {
    tserver_tasks += ts_desc->NumTasks();
  }

  struct ActivityInfo ai {table_tasks, tserver_tasks, master_errors};

  // Update circular buffer summary.

  if (ai.IsIdle()) {
    num_idle_runs_++;
  } else {
    VLOG(1) <<
      Substitute("Load balancer has $0 table tasks, $1 tserver tasks, and $2 master errors",
          table_tasks, tserver_tasks, master_errors);
  }

  if (cbuf_activities_.full()) {
    if (cbuf_activities_.front().IsIdle()) {
      num_idle_runs_--;
    }
  }

  // Mutate circular buffer.
  cbuf_activities_.push_back(std::move(ai));

  // Update state.
  is_idle_.store(num_idle_runs_ == cbuf_activities_.size(), std::memory_order_release);
}

Status ClusterLoadBalancer::IsIdle() const {
  return (IsLoadBalancerEnabled() && !is_idle_.load(std::memory_order_acquire)) ?
    STATUS(IllegalState, "Task or error encountered recently.") : Status::OK();
}

void ClusterLoadBalancer::ReportUnusualLoadBalancerState() const {
  TSDescriptorVector ts_descs;
  GetAllReportedDescriptors(&ts_descs);
  for (const auto& ts_desc : ts_descs) {
    // Report if any ts has a pending delete.
    if (ts_desc->HasTabletDeletePending()) {
      LOG(INFO) << Format("tablet server $0 has a pending delete for tablets $1",
                          ts_desc->permanent_uuid(), ts_desc->PendingTabletDeleteToString());
    }
  }
}

void ClusterLoadBalancer::ResetState() {
  state_ = make_unique<enterprise::ClusterLoadState>();
}

Status ClusterLoadBalancer::AnalyzeTablets(const TableId& table_uuid) {
  // Set the blacklist so we can also mark the tablet servers as we add them up.
  state_->SetBlacklist(GetServerBlacklist());

  // Set the leader blacklist so we can also mark the tablet servers as we add them up.
  state_->SetLeaderBlacklist(GetLeaderBlacklist());

  // Loop over live tablet servers to set empty defaults, so we can also have info on those
  // servers that have yet to receive load (have heartbeated to the master, but have not been
  // assigned any tablets yet).
  TSDescriptorVector ts_descs;
  GetAllReportedDescriptors(&ts_descs);
  for (const auto ts_desc : ts_descs) {
    state_->UpdateTabletServer(ts_desc);
  }

  vector<scoped_refptr<TabletInfo>> tablets;
  Status s = GetTabletsForTable(table_uuid, &tablets);
  YB_RETURN_NOT_OK_PREPEND(s, "Skipping table " + table_uuid + "due to error: ");

  // Loop over tablet map to register the load that is already live in the cluster.
  for (const auto& tablet : tablets) {
    bool tablet_running = false;
    {
      auto tablet_lock = tablet->LockForRead();

      if (!tablet->table()) {
        // Tablet is orphaned or in preparing state, continue.
        continue;
      }
      tablet_running = tablet_lock->data().is_running();
    }

    // This is from the perspective of the CatalogManager and the on-disk, persisted
    // SysCatalogStatePB. What this means is that this tablet was properly created as part of a
    // CreateTable and the information was sent to the initial set of TS and the tablet got to an
    // initial running state.
    //
    // This is different from the individual, per-TS state of the tablet, which can vary based on
    // the TS itself. The tablet can be registered as RUNNING, as far as the CatalogManager is
    // concerned, but just be underreplicated, and have some TS currently bootstrapping instances
    // of the tablet.
    if (tablet_running) {
      RETURN_NOT_OK(UpdateTabletInfo(tablet.get()));
    }
  }

  // After updating the tablets and tablet servers, adjust the configured threshold if it is too
  // low for the given configuration.
  state_->AdjustLeaderBalanceThreshold();

  // Once we've analyzed both the tablet server information as well as the tablets, we can sort the
  // load and are ready to apply the load balancing rules.
  state_->SortLoad();

  // Since leader load is only needed to rebalance leaders, we keep the sorting separate.
  state_->SortLeaderLoad();

  VLOG(1) << Substitute(
      "Total running tablets: $0. Total overreplication: $1. Total starting tablets: $2. "
      "Wrong placement: $3. BlackListed: $4. Total underreplication: $5, Leader BlackListed: $6",
      get_total_running_tablets(), get_total_over_replication(), get_total_starting_tablets(),
      get_total_wrong_placement(), get_total_blacklisted_servers(), get_total_under_replication(),
      get_total_leader_blacklisted_servers());

  for (const auto& tablet : tablets) {
    const auto& tablet_id = tablet->id();
    if (state_->pending_remove_replica_tasks_[table_uuid].count(tablet_id) > 0) {
      RETURN_NOT_OK(state_->RemoveReplica(
          tablet_id, state_->pending_remove_replica_tasks_[table_uuid][tablet_id]));
    }
    if (state_->pending_stepdown_leader_tasks_[table_uuid].count(tablet_id) > 0) {
      const auto& tablet_meta = state_->per_tablet_meta_[tablet_id];
      const auto& from_ts = tablet_meta.leader_uuid;
      const auto& to_ts = state_->pending_stepdown_leader_tasks_[table_uuid][tablet_id];
      RETURN_NOT_OK(state_->MoveLeader(tablet->id(), from_ts, to_ts));
    }
    if (state_->pending_add_replica_tasks_[table_uuid].count(tablet_id) > 0) {
      RETURN_NOT_OK(state_->AddReplica(tablet->id(),
                                       state_->pending_add_replica_tasks_[table_uuid][tablet_id]));
    }
  }

  return Status::OK();
}

Result<bool> ClusterLoadBalancer::HandleAddIfMissingPlacement(
    TabletId* out_tablet_id, TabletServerId* out_to_ts) {
  for (const auto& tablet_id : state_->tablets_missing_replicas_) {
    const auto& tablet_meta = state_->per_tablet_meta_[tablet_id];
    const auto& placement_info = GetPlacementByTablet(tablet_id);
    const auto& missing_placements = tablet_meta.under_replicated_placements;
    // Loop through TSs by load to find a TS that matches the placement needed and does not already
    // host this tablet.
    for (const auto& ts_uuid : state_->sorted_load_) {
      bool can_choose_ts = false;
      // If we had no placement information, it means we are just under-replicated, so just check
      // that we can use this tablet server.
      if (placement_info.placement_blocks().empty()) {
        // No need to check placement info, as there is none.
        can_choose_ts = VERIFY_RESULT(state_->CanAddTabletToTabletServer(tablet_id, ts_uuid));
      } else {
        // We added a tablet to the set with missing replicas both if it is under-replicated, and we
        // added a placement to the tablet_meta under_replicated_placements if the num replicas in
        // that placement is fewer than min_num_replicas. If the under-replicated tablet has a
        // placement that is under-replicated and the ts is not in that placement, then that ts
        // isn't valid.
        const auto& ts_meta = state_->per_ts_meta_[ts_uuid];
        // We have specific placement blocks that are under-replicated, so confirm that this TS
        // matches.
        if (missing_placements.empty() ||
            missing_placements.count(ts_meta.descriptor->placement_id())) {
          // Don't check placement information anymore.
          can_choose_ts = VERIFY_RESULT(state_->CanAddTabletToTabletServer(tablet_id, ts_uuid));
        }
      }
      // If we've passed the checks, then we can choose this TS to add the replica to.
      if (can_choose_ts) {
        *out_tablet_id = tablet_id;
        *out_to_ts = ts_uuid;
        RETURN_NOT_OK(AddReplica(tablet_id, ts_uuid));
        state_->tablets_missing_replicas_.erase(tablet_id);
        return true;
      }
    }
  }
  return false;
}

Result<bool> ClusterLoadBalancer::HandleAddIfWrongPlacement(
    TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts) {
  for (const auto& tablet_id : state_->tablets_wrong_placement_) {
    // Skip this tablet, if it is already over-replicated, as it does not need another replica, it
    // should just have one removed in the removal step.
    if (state_->tablets_over_replicated_.count(tablet_id)) {
      continue;
    }
    if (VERIFY_RESULT(state_->CanSelectWrongReplicaToMove(
            tablet_id, GetPlacementByTablet(tablet_id), out_from_ts, out_to_ts))) {
      *out_tablet_id = tablet_id;
      RETURN_NOT_OK(MoveReplica(tablet_id, *out_from_ts, *out_to_ts));
      return true;
    }
  }
  return false;
}

Result<bool> ClusterLoadBalancer::HandleAddReplicas(
    TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts) {
  if (state_->options_->kAllowLimitStartingTablets &&
      get_total_starting_tablets() >= state_->options_->kMaxTabletRemoteBootstraps) {
    return STATUS_SUBSTITUTE(TryAgain,
        "Cannot add replicas. Currently remote bootstrapping $0 tablets, "
        "when our max allowed is $1",
        get_total_starting_tablets(), state_->options_->kMaxTabletRemoteBootstraps);
  }

  if (state_->options_->kAllowLimitOverReplicatedTablets &&
      get_total_over_replication() >= state_->options_->kMaxOverReplicatedTablets) {
    return STATUS_SUBSTITUTE(TryAgain,
        "Cannot add replicas. Currently have a total overreplication of $0, when max allowed is $1",
        get_total_over_replication(), state_->options_->kMaxOverReplicatedTablets);
  }

  // Handle missing placements with highest priority, as it means we're potentially
  // under-replicated.
  if (VERIFY_RESULT(HandleAddIfMissingPlacement(out_tablet_id, out_to_ts))) {
    return true;
  }

  // Handle wrong placements as next priority, as these could be servers we're moving off of, so
  // we can decommission ASAP.
  if (VERIFY_RESULT(HandleAddIfWrongPlacement(out_tablet_id, out_from_ts, out_to_ts))) {
    return true;
  }

  // Finally, handle normal load balancing.
  if (!VERIFY_RESULT(GetLoadToMove(out_tablet_id, out_from_ts, out_to_ts))) {
    VLOG(1) << "Cannot find any more tablets to move, under current constraints.";
    if (VLOG_IS_ON(1)) {
      DumpSortedLoad();
    }
    return false;
  }

  return true;
}

void ClusterLoadBalancer::DumpSortedLoad() const {
  int last_pos = state_->sorted_load_.size() - 1;
  std::ostringstream out;
  out << "Table load: ";
  for (int left = 0; left <= last_pos; ++left) {
    const TabletServerId& uuid = state_->sorted_load_[left];
    int load = state_->GetLoad(uuid);
    out << uuid << ":" << load << " ";
  }
  VLOG(1) << out.str();
}

Result<bool> ClusterLoadBalancer::GetLoadToMove(
    TabletId* moving_tablet_id, TabletServerId* from_ts, TabletServerId* to_ts) {
  if (state_->sorted_load_.empty()) {
    return false;
  }

  // Start with two indices pointing at left and right most ends of the sorted_load_ structure.
  //
  // We will try to find two TSs that have at least one tablet that can be moved amongst them, from
  // the higher load to the lower load TS. To do this, we will go through comparing the TSs
  // corresponding to our left and right indices, exclude tablets from the right, high loaded TS
  // according to our load balancing rules, such as load variance, starting tablets and not moving
  // already over-replicated tablets. We then compare the remaining set of tablets with the ones
  // hosted by the lower loaded TS and use ReservoirSample to pick a tablet from the set
  // difference. If there were no tablets to pick, we advance our state.
  //
  // The state is defined as the positions of the start and end indices. We always try to move the
  // right index back, until we cannot any more, due to either reaching the left index (cannot
  // rebalance from one TS to itself), or the difference of load between the two TSs is too low to
  // try to rebalance (if load variance is 1, it does not make sense to move tablets between the
  // TSs). When we cannot lower the right index any further, we reset it back to last_pos and
  // increment the left index.
  //
  // We stop the whole algorithm if the left index reaches last_pos, or if we reset the right index
  // and are already breaking the invariance rule, as that means that any further differences in
  // the interval between left and right cannot have load > kMinLoadVarianceToBalance.
  int last_pos = state_->sorted_load_.size() - 1;
  for (int left = 0; left <= last_pos; ++left) {
    for (int right = last_pos; right >= 0; --right) {
      const TabletServerId& low_load_uuid = state_->sorted_load_[left];
      const TabletServerId& high_load_uuid = state_->sorted_load_[right];
      int load_variance = state_->GetLoad(high_load_uuid) - state_->GetLoad(low_load_uuid);

      // Check for state change or end conditions.
      if (left == right || load_variance < state_->options_->kMinLoadVarianceToBalance) {
        // Either both left and right are at the end, or our load_variance is already too small,
        // which means it will be too small for any TSs between left and right, so we can return.
        if (right == last_pos) {
          return false;
        } else {
          break;
        }
      }

      // If we don't find a tablet_id to move between these two TSs, advance the state.
      if (VERIFY_RESULT(GetTabletToMove(high_load_uuid, low_load_uuid, moving_tablet_id))) {
        // If we got this far, we have the candidate we want, so fill in the output params and
        // return. The tablet_id is filled in from GetTabletToMove.
        *from_ts = high_load_uuid;
        *to_ts = low_load_uuid;
        RETURN_NOT_OK(MoveReplica(*moving_tablet_id, high_load_uuid, low_load_uuid));
        return true;
      }
    }
  }

  // Should never get here.
  return STATUS(IllegalState, "Load balancing algorithm reached illegal state.");
}

Result<bool> ClusterLoadBalancer::ShouldSkipLeaderAsVictim(const TabletId& tablet_id) const {
  auto tablet = GetTabletMap().at(tablet_id);
  int num_replicas = 0;
  {
    auto l = tablet->table()->LockForRead();
    // If we have a custom per-table placement policy, use that.
    if (l->data().pb.has_replication_info()) {
      num_replicas = l->data().pb.replication_info().live_replicas().num_replicas();
    } else {
      // Otherwise, default to cluster policy.
      num_replicas = GetClusterPlacementInfo().num_replicas();
    }
  }

  // If replication factor is > 1, skip picking the leader as the victim for the move.
  if (num_replicas > 1) {
    return true;
  }

  return false;
}

Result<bool> ClusterLoadBalancer::GetTabletToMove(
    const TabletServerId& from_ts, const TabletServerId& to_ts, TabletId* moving_tablet_id) {
  const auto& from_ts_meta = state_->per_ts_meta_[from_ts];
  set<TabletId> non_over_replicated_tablets;
  set<TabletId> all_tablets;
  std::merge(
      from_ts_meta.running_tablets.begin(), from_ts_meta.running_tablets.end(),
      from_ts_meta.starting_tablets.begin(), from_ts_meta.starting_tablets.end(),
      std::inserter(all_tablets, all_tablets.begin()));
  for (const TabletId& tablet_id : all_tablets) {
    // We don't want to add a new replica to an already over-replicated tablet.
    //
    // TODO(bogdan): should make sure we pick tablets that this TS is not a leader of, so we
    // can ensure HandleRemoveReplicas removes them from this TS.
    if (state_->tablets_over_replicated_.count(tablet_id)) {
      continue;
    }

    if (VERIFY_RESULT(
        state_->CanAddTabletToTabletServer(tablet_id, to_ts, &GetPlacementByTablet(tablet_id)))) {
      non_over_replicated_tablets.insert(tablet_id);
    }
  }

  bool same_placement = state_->per_ts_meta_[from_ts].descriptor->placement_id() ==
                        state_->per_ts_meta_[to_ts].descriptor->placement_id();
  for (const auto& tablet_id : non_over_replicated_tablets) {
    const auto& placement_info = GetPlacementByTablet(tablet_id);
    // TODO(bogdan): this should be augmented as well to allow dropping by one replica, if still
    // leaving us with more than the minimum.
    //
    // If we have placement information, we want to only pick the tablet if it's moving to the same
    // placement, so we guarantee we're keeping the same type of distribution.
    if (!placement_info.placement_blocks().empty() && !same_placement) {
      continue;
    }
    // Skip this tablet if we are trying to move away from the leader, as we would like to avoid
    // extra leader stepdowns. If table is in RF > 1 universe only, we skip leader as victim here.
    if (state_->per_tablet_meta_[tablet_id].leader_uuid == from_ts &&
        VERIFY_RESULT(ShouldSkipLeaderAsVictim(tablet_id))) {
      continue;
    }
    // If we got here, it means we either have no placement, in which case we can pick any TS, or
    // we have placement and it's valid to move across these two tablet servers, so set the tablet
    // and leave.
    *moving_tablet_id = tablet_id;
    return true;
  }
  // If we couldn't select a tablet above, we have to return failure.
  return false;
}

Result<bool> ClusterLoadBalancer::GetLeaderToMove(
    TabletId* moving_tablet_id, TabletServerId* from_ts, TabletServerId *to_ts) {
  if (state_->sorted_leader_load_.empty()) {
    return false;
  }

  // Find out if there are leaders to be moved.
  for (int right = state_->sorted_leader_load_.size() - 1; right >= 0; --right) {
    const TabletServerId& high_load_uuid = state_->sorted_leader_load_[right];
    auto high_leader_blacklisted = (state_->leader_blacklisted_servers_.find(high_load_uuid) !=
      state_->leader_blacklisted_servers_.end());
    if (high_leader_blacklisted) {
      int high_load = state_->GetLeaderLoad(high_load_uuid);
      if (high_load > 0) {
        // Leader blacklisted tserver with a leader replica.
        break;
      } else {
        // Leader blacklisted tserver without leader replica.
        continue;
      }
    } else {
      if (state_->IsLeaderLoadBelowThreshold(state_->sorted_leader_load_[right])) {
        // Non-leader blacklisted tserver with not too many leader replicas.
        return false;
      } else {
        // Non-leader blacklisted tserver with too many leader replicas.
        break;
      }
    }
  }

  // The algorithm to balance the leaders is very similar to the one for tablets:
  //
  // Start with two indices pointing at left and right most ends of the sorted_leader_load_
  // structure. Note that leader blacklisted tserver is considered as having infinite leader load.
  //
  // We will try to find two TSs that have at least one leader that can be moved amongst them, from
  // the higher load to the lower load TS. To do this, we will go through comparing the TSs
  // corresponding to our left and right indices. We go through leaders on the higher loaded TS
  // and find a running replica on the lower loaded TS to move the leader. If no leader can be
  // be picked, we advance our state.
  //
  // The state is defined as the positions of the start and end indices. We always try to move the
  // right index back, until we cannot any more, due to either reaching the left index (cannot
  // rebalance from one TS to itself), or the difference of load between the two TSs is too low to
  // try to rebalance (if load variance is 1, it does not make sense to move leaders between the
  // TSs). When we cannot lower the right index any further, we reset it back to last_pos and
  // increment the left index.
  //
  // We stop the whole algorithm if the left index reaches last_pos, or if we reset the right index
  // and are already breaking the invariance rule, as that means that any further differences in
  // the interval between left and right cannot have load > kMinLeaderLoadVarianceToBalance.
  const auto current_time = MonoTime::Now();
  int last_pos = state_->sorted_leader_load_.size() - 1;
  for (int left = 0; left <= last_pos; ++left) {
    const TabletServerId& low_load_uuid = state_->sorted_leader_load_[left];
    auto low_leader_blacklisted = (state_->leader_blacklisted_servers_.find(low_load_uuid) !=
        state_->leader_blacklisted_servers_.end());
    if (low_leader_blacklisted) {
      // Left marker has gone beyond non-leader blacklisted tservers.
      return false;
    }

    for (int right = last_pos; right >= 0; --right) {
      const TabletServerId& high_load_uuid = state_->sorted_leader_load_[right];
      auto high_leader_blacklisted = (state_->leader_blacklisted_servers_.find(high_load_uuid) !=
          state_->leader_blacklisted_servers_.end());
      int load_variance =
          state_->GetLeaderLoad(high_load_uuid) - state_->GetLeaderLoad(low_load_uuid);

      // Check for state change or end conditions.
      if (left == right || (load_variance < state_->options_->kMinLeaderLoadVarianceToBalance &&
            !high_leader_blacklisted)) {
        // Either both left and right are at the end, or our load_variance is already too small,
        // which means it will be too small for any TSs between left and right, so we can return.
        if (right == last_pos) {
          return false;
        } else {
          break;
        }
      }

      // Find the leaders on the higher loaded TS that have running peers on the lower loaded TS.
      // If there are, we have a candidate we want, so fill in the output params and return.
      const set<TabletId>& leaders = state_->per_ts_meta_[high_load_uuid].leaders;
      const set<TabletId>& peers = state_->per_ts_meta_[low_load_uuid].running_tablets;
      set<TabletId> intersection;
      const auto& itr = std::inserter(intersection, intersection.begin());
      std::set_intersection(leaders.begin(), leaders.end(), peers.begin(), peers.end(), itr);

      for (const auto& tablet_id : intersection) {
        *moving_tablet_id = tablet_id;
        *from_ts = high_load_uuid;
        *to_ts = low_load_uuid;

        const auto& per_tablet_meta = state_->per_tablet_meta_;
        const auto tablet_meta_iter = per_tablet_meta.find(tablet_id);
        if (PREDICT_TRUE(tablet_meta_iter != per_tablet_meta.end())) {
          const auto& tablet_meta = tablet_meta_iter->second;
          const auto& stepdown_failures = tablet_meta.leader_stepdown_failures;
          const auto stepdown_failure_iter = stepdown_failures.find(low_load_uuid);
          if (stepdown_failure_iter != stepdown_failures.end()) {
            const auto time_since_failure = current_time - stepdown_failure_iter->second;
            if (time_since_failure.ToMilliseconds() < FLAGS_min_leader_stepdown_retry_interval_ms) {
              LOG(INFO) << "Cannot move tablet " << tablet_id << " leader from TS "
                        << *from_ts << " to TS " << *to_ts << " yet: previous attempt with the same"
                        << " intended leader failed only " << ToString(time_since_failure)
                        << " ago (less " << "than " << FLAGS_min_leader_stepdown_retry_interval_ms
                        << "ms).";
            }
            continue;
          }
        } else {
          LOG(WARNING) << "Did not find load balancer metadata for tablet " << *moving_tablet_id;
        }

        // Leader movement solely due to leader blacklist.
        if (load_variance < state_->options_->kMinLeaderLoadVarianceToBalance &&
            high_leader_blacklisted) {
          state_->LogSortedLeaderLoad();
          LOG(INFO) << "Move tablet " << tablet_id << " leader from leader blacklisted TS "
            << *from_ts << " to TS " << *to_ts;
        }
        return true;
      }
    }
  }

  // Should never get here.
  FATAL_ERROR("Load balancing algorithm reached invalid state!");
}

Result<bool> ClusterLoadBalancer::HandleRemoveReplicas(
    TabletId* out_tablet_id, TabletServerId* out_from_ts) {
  // Give high priority to removing tablets that are not respecting the placement policy.
  if (VERIFY_RESULT(HandleRemoveIfWrongPlacement(out_tablet_id, out_from_ts))) {
    return true;
  }

  for (const auto& tablet_id : state_->tablets_over_replicated_) {
    // Skip if there is a pending ADD_SERVER.
    if (VERIFY_RESULT(IsConfigMemberInTransitionMode(tablet_id))) {
      continue;
    }

    const auto& tablet_meta = state_->per_tablet_meta_[tablet_id];
    const auto& tablet_servers = tablet_meta.over_replicated_tablet_servers;
    auto comparator = ClusterLoadState::Comparator(state_.get());
    vector<TabletServerId> sorted_ts(tablet_servers.begin(), tablet_servers.end());
    if (sorted_ts.empty()) {
      return STATUS_SUBSTITUTE(IllegalState, "No tservers to remove from over-replicated "
                                             "tablet $0", tablet_id);
    }
    // Sort in reverse to first try to remove a replica from the highest loaded TS.
    sort(sorted_ts.rbegin(), sorted_ts.rend(), comparator);
    string remove_candidate = sorted_ts[0];
    if (remove_candidate == tablet_meta.leader_uuid &&
        VERIFY_RESULT(ShouldSkipLeaderAsVictim(tablet_id))) {
      // Pick the next (non-leader) tserver for this tablet, if available.
      if (sorted_ts.size() > 1) {
        remove_candidate = sorted_ts[1];
      } else {
        continue;
      }
    }
    *out_tablet_id = tablet_id;
    *out_from_ts = remove_candidate;
    // Do force leader stepdown, as we are either not the leader or we are allowed to step down.
    RETURN_NOT_OK(RemoveReplica(tablet_id, remove_candidate, true));
    return true;
  }
  return false;
}

Result<bool> ClusterLoadBalancer::HandleRemoveIfWrongPlacement(
    TabletId* out_tablet_id, TabletServerId* out_from_ts) {
  for (const auto& tablet_id : state_->tablets_wrong_placement_) {
    // Skip this tablet if it is not over-replicated.
    if (!state_->tablets_over_replicated_.count(tablet_id)) {
      continue;
    }
    // Skip if there is a pending ADD_SERVER
    if (VERIFY_RESULT(IsConfigMemberInTransitionMode(tablet_id))) {
      continue;
    }
    const auto& tablet_meta = state_->per_tablet_meta_[tablet_id];
    TabletServerId target_uuid;
    // Prioritize blacklisted servers, if any.
    if (!tablet_meta.blacklisted_tablet_servers.empty()) {
      target_uuid = *tablet_meta.blacklisted_tablet_servers.begin();
    }
    // If no blacklisted server could be chosen, try the wrong placement ones.
    if (target_uuid.empty()) {
      if (!tablet_meta.wrong_placement_tablet_servers.empty()) {
        target_uuid = *tablet_meta.wrong_placement_tablet_servers.begin();
      }
    }
    // If we found a tablet server, choose it.
    if (!target_uuid.empty()) {
      *out_tablet_id = tablet_id;
      *out_from_ts = std::move(target_uuid);
      // Force leader stepdown if we have wrong placements or blacklisted servers.
      RETURN_NOT_OK(RemoveReplica(tablet_id, *out_from_ts, true));
      return true;
    }
  }
  return false;
}

Result<bool> ClusterLoadBalancer::HandleLeaderMoves(
    TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts) {
  if (VERIFY_RESULT(GetLeaderToMove(out_tablet_id, out_from_ts, out_to_ts))) {
    RETURN_NOT_OK(MoveLeader(*out_tablet_id, *out_from_ts, *out_to_ts));
    return true;
  }
  return false;
}

Status ClusterLoadBalancer::MoveReplica(
    const TabletId& tablet_id, const TabletServerId& from_ts, const TabletServerId& to_ts) {
  LOG(INFO) << Substitute("Moving tablet $0 from $1 to $2", tablet_id, from_ts, to_ts);
  SendReplicaChanges(GetTabletMap().at(tablet_id), to_ts, true /* is_add */,
                     true /* should_remove_leader */);
  RETURN_NOT_OK(state_->AddReplica(tablet_id, to_ts));
  return state_->RemoveReplica(tablet_id, from_ts);
}

Status ClusterLoadBalancer::AddReplica(const TabletId& tablet_id, const TabletServerId& to_ts) {
  LOG(INFO) << Substitute("Adding tablet $0 to $1", tablet_id, to_ts);
  // This is an add operation, so the "should_remove_leader" flag is irrelevant.
  SendReplicaChanges(GetTabletMap().at(tablet_id), to_ts, true /* is_add */,
                     true /* should_remove_leader */);
  return state_->AddReplica(tablet_id, to_ts);
}

Status ClusterLoadBalancer::RemoveReplica(
    const TabletId& tablet_id, const TabletServerId& ts_uuid, const bool stepdown_if_leader) {
  LOG(INFO) << Substitute("Removing replica $0 from tablet $1", ts_uuid, tablet_id);
  SendReplicaChanges(GetTabletMap().at(tablet_id), ts_uuid, false /* is_add */,
                     true /* should_remove_leader */);
  return state_->RemoveReplica(tablet_id, ts_uuid);
}

Status ClusterLoadBalancer::MoveLeader(
    const TabletId& tablet_id, const TabletServerId& from_ts, const TabletServerId& to_ts) {
  LOG(INFO) << Substitute("Moving leader of $0 from TS $1 to $2", tablet_id, from_ts, to_ts);
  SendReplicaChanges(GetTabletMap().at(tablet_id), from_ts, false /* is_add */,
                     false /* should_remove_leader */, to_ts);

  return state_->MoveLeader(tablet_id, from_ts, to_ts);
}

// CatalogManager indirection methods that are set as virtual to be bypassed in testing.
//
void ClusterLoadBalancer::GetAllReportedDescriptors(TSDescriptorVector* ts_descs) const {
  catalog_manager_->master_->ts_manager()->GetAllReportedDescriptors(ts_descs);
}

const TabletInfoMap& ClusterLoadBalancer::GetTabletMap() const {
  return *catalog_manager_->tablet_map_;
}

const scoped_refptr<TableInfo> ClusterLoadBalancer::GetTableInfo(const TableId& table_uuid) const {
  return catalog_manager_->GetTableInfoUnlocked(table_uuid);
}

const Status ClusterLoadBalancer::GetTabletsForTable(
    const TableId& table_uuid, vector<scoped_refptr<TabletInfo>>* tablets) const {
  scoped_refptr<TableInfo> table_info = GetTableInfo(table_uuid);

  if (table_info == nullptr) {
    return STATUS(InvalidArgument,
                  Substitute("Invalid UUID '$0' - no entry found in catalog manager table map.",
                             table_uuid));
  }

  table_info->GetAllTablets(tablets);

  return Status::OK();
}

const TableInfoMap& ClusterLoadBalancer::GetTableMap() const {
  return *catalog_manager_->table_ids_map_;
}

const PlacementInfoPB& ClusterLoadBalancer::GetClusterPlacementInfo() const {
  auto l = catalog_manager_->cluster_config_->LockForRead();
  return l->data().pb.replication_info().live_replicas();
}

const BlacklistPB& ClusterLoadBalancer::GetServerBlacklist() const {
  auto l = catalog_manager_->cluster_config_->LockForRead();
  return l->data().pb.server_blacklist();
}

const BlacklistPB& ClusterLoadBalancer::GetLeaderBlacklist() const {
  auto l = catalog_manager_->cluster_config_->LockForRead();
  return l->data().pb.leader_blacklist();
}

bool ClusterLoadBalancer::SkipLoadBalancing(const TableInfo& table) const {
  // Skip load-balancing of system tables. They are virtual tables not hosted by tservers.
  return catalog_manager_->IsSystemTableUnlocked(table);
}

void ClusterLoadBalancer::CountPendingTasks(const TableId& table_uuid,
                                            int* pending_add_replica_tasks,
                                            int* pending_remove_replica_tasks,
                                            int* pending_stepdown_leader_tasks) {
  GetPendingTasks(table_uuid,
                  &state_->pending_add_replica_tasks_[table_uuid],
                  &state_->pending_remove_replica_tasks_[table_uuid],
                  &state_->pending_stepdown_leader_tasks_[table_uuid]);

  *pending_add_replica_tasks += state_->pending_add_replica_tasks_[table_uuid].size();
  *pending_remove_replica_tasks += state_->pending_remove_replica_tasks_[table_uuid].size();
  *pending_stepdown_leader_tasks += state_->pending_stepdown_leader_tasks_[table_uuid].size();
  state_->total_starting_ += *pending_add_replica_tasks;
}

void ClusterLoadBalancer::GetPendingTasks(const TableId& table_uuid,
                                          TabletToTabletServerMap* add_replica_tasks,
                                          TabletToTabletServerMap* remove_replica_tasks,
                                          TabletToTabletServerMap* stepdown_leader_tasks) {
  catalog_manager_->GetPendingServerTasksUnlocked(
      table_uuid, add_replica_tasks, remove_replica_tasks, stepdown_leader_tasks);
}

void ClusterLoadBalancer::SendReplicaChanges(
    scoped_refptr<TabletInfo> tablet, const TabletServerId& ts_uuid, const bool is_add,
    const bool should_remove_leader, const TabletServerId& new_leader_ts_uuid) {
  auto l = tablet->LockForRead();
  if (is_add) {
    // These checks are temporary. They will be removed once we are confident that the algorithm is
    // always doing the right thing.
    CHECK_EQ(state_->pending_add_replica_tasks_[tablet->table()->id()].count(tablet->tablet_id()),
             0);
    catalog_manager_->SendAddServerRequest(tablet, GetDefaultMemberType(),
        l->data().pb.committed_consensus_state(), ts_uuid);
  } else {
    // If the replica is also the leader, first step it down and then remove.
    if (state_->per_tablet_meta_[tablet->id()].leader_uuid == ts_uuid) {
      CHECK_EQ(
          state_->pending_stepdown_leader_tasks_[tablet->table()->id()].count(tablet->tablet_id()),
          0);
      catalog_manager_->SendLeaderStepDownRequest(tablet,
                                                  l->data().pb.committed_consensus_state(),
                                                  ts_uuid,
                                                  should_remove_leader,
                                                  new_leader_ts_uuid);
    } else {
      CHECK_EQ(
          state_->pending_remove_replica_tasks_[tablet->table()->id()].count(tablet->tablet_id()),
          0);
      catalog_manager_->SendRemoveServerRequest(
          tablet, l->data().pb.committed_consensus_state(), ts_uuid);
    }
  }
}

consensus::RaftPeerPB::MemberType ClusterLoadBalancer::GetDefaultMemberType() {
  return consensus::RaftPeerPB::PRE_VOTER;
}

Result<bool> ClusterLoadBalancer::IsConfigMemberInTransitionMode(const TabletId &tablet_id) const {
  auto tablet = GetTabletMap().at(tablet_id);
  auto l = tablet->LockForRead();
  auto config = l->data().pb.committed_consensus_state().config();
  return CountVotersInTransition(config) != 0;
}

}  // namespace master
}  // namespace yb

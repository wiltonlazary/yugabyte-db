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

#include "yb/consensus/consensus_queue.h"

#include <shared_mutex>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>

#include <boost/container/small_vector.hpp>

#include <gflags/gflags.h>

#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus_context.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/log_util.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/quorum_util.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/replicate_msgs_holder.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/strcat.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/util/fault_injection.h"
#include "yb/util/flag_tags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/size_literals.h"
#include "yb/util/threadpool.h"
#include "yb/util/url-coding.h"
#include "yb/util/enums.h"
#include "yb/util/tostring.h"

using namespace std::literals;
using namespace yb::size_literals;

DECLARE_int32(rpc_max_message_size);

// We expect that consensus_max_batch_size_bytes + 1_KB would be less than rpc_max_message_size.
// Otherwise such batch would be rejected by RPC layer.
DEFINE_int32(consensus_max_batch_size_bytes, 4_MB,
             "The maximum per-tablet RPC batch size when updating peers.");
TAG_FLAG(consensus_max_batch_size_bytes, advanced);
TAG_FLAG(consensus_max_batch_size_bytes, runtime);

DEFINE_int32(follower_unavailable_considered_failed_sec, 900,
             "Seconds that a leader is unable to successfully heartbeat to a "
             "follower after which the follower is considered to be failed and "
             "evicted from the config.");
TAG_FLAG(follower_unavailable_considered_failed_sec, advanced);

DEFINE_int32(consensus_inject_latency_ms_in_notifications, 0,
             "Injects a random sleep between 0 and this many milliseconds into "
             "asynchronous notifications from the consensus queue back to the "
             "consensus implementation.");
TAG_FLAG(consensus_inject_latency_ms_in_notifications, hidden);
TAG_FLAG(consensus_inject_latency_ms_in_notifications, unsafe);

DEFINE_bool(propagate_safe_time, true, "Propagate safe time to read from leader to followers");

DEFINE_int32(cdc_checkpoint_opid_interval_ms, 60 * 1000,
             "Interval up to which CDC consumer's checkpoint is considered for retaining log cache."
             "If we haven't received an updated checkpoint from CDC consumer within the interval "
             "specified by cdc_checkpoint_opid_interval, then log cache does not consider that "
             "consumer while determining which op IDs to evict.");

namespace yb {
namespace consensus {

using log::AsyncLogReader;
using log::Log;
using std::unique_ptr;
using rpc::Messenger;
using strings::Substitute;

METRIC_DEFINE_gauge_int64(tablet, majority_done_ops, "Leader Operations Acked by Majority",
                          MetricUnit::kOperations,
                          "Number of operations in the leader queue ack'd by a majority but "
                          "not all peers.");
METRIC_DEFINE_gauge_int64(tablet, in_progress_ops, "Leader Operations in Progress",
                          MetricUnit::kOperations,
                          "Number of operations in the leader queue ack'd by a minority of "
                          "peers.");

const auto kCDCConsumerCheckpointInterval = FLAGS_cdc_checkpoint_opid_interval_ms * 1ms;

std::string MajorityReplicatedData::ToString() const {
  return Format(
      "{ op_id: $0 leader_lease_expiration: $1 ht_lease_expiration: $2 num_sst_files: $3 }",
      op_id, leader_lease_expiration, ht_lease_expiration, num_sst_files);
}

std::string PeerMessageQueue::TrackedPeer::ToString() const {
  return Substitute("Peer: $0, Is new: $1, Last received: $2, Next index: $3, "
                    "Last known committed idx: $4, Last exchange result: $5, "
                    "Needs remote bootstrap: $6",
                    uuid, is_new, OpIdToString(last_received), next_index,
                    last_known_committed_idx,
                    is_last_exchange_successful ? "SUCCESS" : "ERROR",
                    needs_remote_bootstrap);
}

void PeerMessageQueue::TrackedPeer::ResetLeaderLeases() {
  last_leader_lease_expiration_sent_to_follower = CoarseTimePoint();
  last_leader_lease_expiration_received_by_follower = CoarseTimePoint();
  last_ht_lease_expiration_sent_to_follower = HybridTime::kMin.GetPhysicalValueMicros();
  last_ht_lease_expiration_received_by_follower = HybridTime::kMin.GetPhysicalValueMicros();
}

#define INSTANTIATE_METRIC(x) \
  x.Instantiate(metric_entity, 0)
PeerMessageQueue::Metrics::Metrics(const scoped_refptr<MetricEntity>& metric_entity)
  : num_majority_done_ops(INSTANTIATE_METRIC(METRIC_majority_done_ops)),
    num_in_progress_ops(INSTANTIATE_METRIC(METRIC_in_progress_ops)) {
}
#undef INSTANTIATE_METRIC

PeerMessageQueue::PeerMessageQueue(const scoped_refptr<MetricEntity>& metric_entity,
                                   const scoped_refptr<log::Log>& log,
                                   const MemTrackerPtr& server_tracker,
                                   const MemTrackerPtr& parent_tracker,
                                   const RaftPeerPB& local_peer_pb,
                                   const string& tablet_id,
                                   const server::ClockPtr& clock,
                                   ConsensusContext* context,
                                   unique_ptr<ThreadPoolToken> raft_pool_token)
    : raft_pool_observers_token_(std::move(raft_pool_token)),
      local_peer_pb_(local_peer_pb),
      local_peer_uuid_(local_peer_pb_.has_permanent_uuid() ? local_peer_pb_.permanent_uuid()
                                                           : string()),
      tablet_id_(tablet_id),
      log_cache_(metric_entity, log, server_tracker, local_peer_pb.permanent_uuid(), tablet_id),
      operations_mem_tracker_(
          MemTracker::FindOrCreateTracker("OperationsFromDisk", parent_tracker)),
      metrics_(metric_entity),
      clock_(clock),
      context_(context) {
  DCHECK(local_peer_pb_.has_permanent_uuid());
  DCHECK(!local_peer_pb_.last_known_private_addr().empty());
}

void PeerMessageQueue::Init(const OpId& last_locally_replicated) {
  LockGuard lock(queue_lock_);
  CHECK_EQ(queue_state_.state, State::kQueueConstructed);
  log_cache_.Init(last_locally_replicated);
  queue_state_.last_appended = last_locally_replicated;
  queue_state_.state = State::kQueueOpen;
  local_peer_ = TrackPeerUnlocked(local_peer_uuid_);
}

void PeerMessageQueue::SetLeaderMode(const OpId& committed_index,
                                     int64_t current_term,
                                     const RaftConfigPB& active_config) {
  LockGuard lock(queue_lock_);
  CHECK(committed_index.IsInitialized());
  queue_state_.current_term = current_term;
  queue_state_.committed_index = committed_index;
  queue_state_.majority_replicated_opid = committed_index;
  queue_state_.active_config.reset(new RaftConfigPB(active_config));
  CHECK(IsRaftConfigVoter(local_peer_uuid_, *queue_state_.active_config))
      << local_peer_pb_.ShortDebugString() << " not a voter in config: "
      << queue_state_.active_config->ShortDebugString();
  queue_state_.majority_size_ = MajoritySize(CountVoters(*queue_state_.active_config));
  queue_state_.mode = Mode::LEADER;

  LOG_WITH_PREFIX_UNLOCKED(INFO) << "Queue going to LEADER mode. State: "
      << queue_state_.ToString();
  CheckPeersInActiveConfigIfLeaderUnlocked();

  // Reset last communication time with all peers to reset the clock on the
  // failure timeout.
  MonoTime now(MonoTime::Now());
  for (const PeersMap::value_type& entry : peers_map_) {
    entry.second->ResetLeaderLeases();
    entry.second->last_successful_communication_time = now;
  }
}

void PeerMessageQueue::SetNonLeaderMode() {
  LockGuard lock(queue_lock_);
  queue_state_.active_config.reset();
  queue_state_.mode = Mode::NON_LEADER;
  queue_state_.majority_size_ = -1;
  LOG_WITH_PREFIX_UNLOCKED(INFO) << "Queue going to NON_LEADER mode. State: "
      << queue_state_.ToString();
}

void PeerMessageQueue::TrackPeer(const string& uuid) {
  LockGuard lock(queue_lock_);
  TrackPeerUnlocked(uuid);
}

PeerMessageQueue::TrackedPeer* PeerMessageQueue::TrackPeerUnlocked(const string& uuid) {
  CHECK(!uuid.empty()) << "Got request to track peer with empty UUID";
  DCHECK_EQ(queue_state_.state, State::kQueueOpen);

  TrackedPeer* tracked_peer = new TrackedPeer(uuid);

  // We don't know the last operation received by the peer so, following the Raft protocol, we set
  // next_index to one past the end of our own log. This way, if calling this method is the result
  // of a successful leader election and the logs between the new leader and remote peer match, the
  // peer->next_index will point to the index of the soon-to-be-written NO_OP entry that is used to
  // assert leadership. If we guessed wrong, and the peer does not have a log that matches ours, the
  // normal queue negotiation process will eventually find the right point to resume from.
  tracked_peer->next_index = queue_state_.last_appended.index() + 1;
  InsertOrDie(&peers_map_, uuid, tracked_peer);

  CheckPeersInActiveConfigIfLeaderUnlocked();

  // We don't know how far back this peer is, so set the all replicated watermark to
  // MinimumOpId. We'll advance it when we know how far along the peer is.
  queue_state_.all_replicated_opid = MinimumOpId();
  return tracked_peer;
}

void PeerMessageQueue::UntrackPeer(const string& uuid) {
  LockGuard lock(queue_lock_);
  TrackedPeer* peer = EraseKeyReturnValuePtr(&peers_map_, uuid);
  if (peer != nullptr) {
    delete peer;
  }
}

void PeerMessageQueue::CheckPeersInActiveConfigIfLeaderUnlocked() const {
  if (queue_state_.mode != Mode::LEADER) return;
  unordered_set<string> config_peer_uuids;
  for (const RaftPeerPB& peer_pb : queue_state_.active_config->peers()) {
    InsertOrDie(&config_peer_uuids, peer_pb.permanent_uuid());
  }
  for (const PeersMap::value_type& entry : peers_map_) {
    if (!ContainsKey(config_peer_uuids, entry.first)) {
      LOG_WITH_PREFIX_UNLOCKED(FATAL) << Substitute("Peer $0 is not in the active config. "
                                                    "Queue state: $1",
                                                    entry.first,
                                                    queue_state_.ToString());
    }
  }
}

void PeerMessageQueue::LocalPeerAppendFinished(const OpId& id,
                                               const Status& status) {
  CHECK_OK(status);

  // Fake an RPC response from the local peer.
  // TODO: we should probably refactor the ResponseFromPeer function so that we don't need to
  // construct this fake response, but this seems to work for now.
  ConsensusResponsePB fake_response;
  fake_response.set_responder_uuid(local_peer_uuid_);
  *fake_response.mutable_status()->mutable_last_received() = id;
  *fake_response.mutable_status()->mutable_last_received_current_leader() = id;
  if (context_) {
    fake_response.set_num_sst_files(context_->NumSSTFiles());
  }
  {
    LockGuard lock(queue_lock_);

    // TODO This ugly fix is required because we unlock queue_lock_ while doing AppendOperations.
    // So LocalPeerAppendFinished could be invoked before rest of AppendOperations.
    if (queue_state_.last_appended.index() < id.index()) {
      queue_state_.last_appended = id;
    }
    fake_response.mutable_status()->set_last_committed_idx(queue_state_.committed_index.index());
  }
  bool junk;
  ResponseFromPeer(local_peer_uuid_, fake_response, &junk);
}

Status PeerMessageQueue::TEST_AppendOperation(const ReplicateMsgPtr& msg) {
  return AppendOperations(
      { msg }, yb::OpId::FromPB(msg->committed_op_id()), RestartSafeCoarseMonoClock().Now());
}

Status PeerMessageQueue::AppendOperations(const ReplicateMsgs& msgs,
                                          const yb::OpId& committed_op_id,
                                          RestartSafeCoarseTimePoint batch_mono_time) {
  DFAKE_SCOPED_LOCK(append_fake_lock_);
  OpId last_id;
  if (!msgs.empty()) {
    std::unique_lock<simple_spinlock> lock(queue_lock_);

    last_id = msgs.back()->id();

    if (last_id.term() > queue_state_.current_term) {
      queue_state_.current_term = last_id.term();
    }
  } else {
    std::unique_lock<simple_spinlock> lock(queue_lock_);
    last_id = queue_state_.last_appended;
  }

  // Unlock ourselves during Append to prevent a deadlock: it's possible that the log buffer is
  // full, in which case AppendOperations would block. However, for the log buffer to empty, it may
  // need to call LocalPeerAppendFinished() which also needs queue_lock_.
  //
  // Since we are doing AppendOperations only in one thread, no concurrent AppendOperations could
  // be executed and queue_state_.last_appended will be updated correctly.
  RETURN_NOT_OK(log_cache_.AppendOperations(
      msgs, committed_op_id, batch_mono_time,
      Bind(&PeerMessageQueue::LocalPeerAppendFinished, Unretained(this), last_id)));

  if (!msgs.empty()) {
    std::unique_lock<simple_spinlock> lock(queue_lock_);
    queue_state_.last_appended = last_id;
    UpdateMetrics();
  }

  return Status::OK();
}

Status PeerMessageQueue::RequestForPeer(const string& uuid,
                                        ConsensusRequestPB* request,
                                        ReplicateMsgsHolder* msgs_holder,
                                        bool* needs_remote_bootstrap,
                                        RaftPeerPB::MemberType* member_type,
                                        bool* last_exchange_successful) {
  DCHECK(request->ops().empty());

  OpId preceding_id;
  MonoDelta unreachable_time = MonoDelta::kMin;
  bool is_voter = false;
  bool is_new;
  int64_t next_index;
  HybridTime propagated_safe_time;
  {
    LockGuard lock(queue_lock_);
    DCHECK_EQ(queue_state_.state, State::kQueueOpen);
    DCHECK_NE(uuid, local_peer_uuid_);

    auto peer = FindPtrOrNull(peers_map_, uuid);
    if (PREDICT_FALSE(peer == nullptr || queue_state_.mode == Mode::NON_LEADER)) {
      return STATUS(NotFound, "Peer not tracked or queue not in leader mode.");
    }

    HybridTime now_ht;

    is_new = peer->is_new;
    if (!is_new) {
      // Should be before now_ht, i.e. not greater than propagated_hybrid_time.
      if (context_ && FLAGS_propagate_safe_time) {
        propagated_safe_time = context_->PropagatedSafeTime();
      }

      now_ht = clock_->Now();

      auto ht_lease_expiration_micros = now_ht.GetPhysicalValueMicros() +
                                        FLAGS_ht_lease_duration_ms * 1000;
      auto leader_lease_duration_ms = GetAtomicFlag(&FLAGS_leader_lease_duration_ms);
      request->set_leader_lease_duration_ms(leader_lease_duration_ms);
      request->set_ht_lease_expiration(ht_lease_expiration_micros);

      // As noted here:
      // https://red.ht/2sCSErb
      //
      // The _COARSE variants are faster to read and have a precision (also known as resolution) of
      // one millisecond (ms).
      //
      // Coarse clock precision is 1 millisecond.
      const auto kCoarseClockPrecision = 1ms;

      // Because of coarse clocks we subtract 2ms, to be sure that our local version of lease
      // does not expire after it expires at follower.
      peer->last_leader_lease_expiration_sent_to_follower =
          CoarseMonoClock::Now() + leader_lease_duration_ms * 1ms - kCoarseClockPrecision * 2;
      peer->last_ht_lease_expiration_sent_to_follower = ht_lease_expiration_micros;
    } else {
      now_ht = clock_->Now();
      request->clear_leader_lease_duration_ms();
      request->clear_ht_lease_expiration();
      peer->last_leader_lease_expiration_received_by_follower = CoarseTimePoint();
      peer->last_ht_lease_expiration_sent_to_follower = 0;
    }

    request->set_propagated_hybrid_time(now_ht.ToUint64());

    // This is initialized to the queue's last appended op but gets set to the id of the
    // log entry preceding the first one in 'messages' if messages are found for the peer.
    preceding_id = queue_state_.last_appended;
    request->mutable_committed_index()->CopyFrom(queue_state_.committed_index);
    request->set_caller_term(queue_state_.current_term);
    unreachable_time =
        MonoTime::Now().GetDeltaSince(peer->last_successful_communication_time);
    if (member_type) *member_type = peer->member_type;
    if (last_exchange_successful) *last_exchange_successful = peer->is_last_exchange_successful;
    *needs_remote_bootstrap = peer->needs_remote_bootstrap;
    next_index = peer->next_index;
    if (peer->member_type == RaftPeerPB::VOTER) {
      is_voter = true;
    }
  }

  if (unreachable_time.ToSeconds() > FLAGS_follower_unavailable_considered_failed_sec) {
    if (!is_voter || CountVoters(*queue_state_.active_config) > 2) {
      // We never drop from 2 voters to 1 voter automatically, at least for now (12/4/18). We may
      // want to revisit this later, we're just being cautious with this.
      // We remove unconditionally any failed non-voter replica (PRE_VOTER, PRE_OBSERVER, OBSERVER).
      string msg = Substitute("Leader has been unable to successfully communicate "
                              "with Peer $0 for more than $1 seconds ($2)",
                              uuid,
                              FLAGS_follower_unavailable_considered_failed_sec,
                              unreachable_time.ToString());
      NotifyObserversOfFailedFollower(uuid, queue_state_.current_term, msg);
    }
  }

  if (PREDICT_FALSE(*needs_remote_bootstrap)) {
      YB_LOG_WITH_PREFIX_UNLOCKED_EVERY_N_SECS(INFO, 30)
          << "Peer needs remote bootstrap: " << uuid;
    return Status::OK();
  }
  *needs_remote_bootstrap = false;

  // If we've never communicated with the peer, we don't know what messages to send, so we'll send a
  // status-only request. Otherwise, we grab requests from the log starting at the last_received
  // point.
  if (!is_new) {
    // The batch of messages to send to the peer.
    int max_batch_size = FLAGS_consensus_max_batch_size_bytes - request->ByteSize();

    auto result = ReadFromLogCache(next_index - 1, 0 /* to_index */, max_batch_size, uuid);
    if (PREDICT_FALSE(!result.ok())) {
      if (PREDICT_TRUE(result.status().IsNotFound())) {
        std::string msg = Format("The logs necessary to catch up peer $0 have been "
                                 "garbage collected. The follower will never be able "
                                 "to catch up ($1)", uuid, result.status());
        NotifyObserversOfFailedFollower(uuid, queue_state_.current_term, msg);
      }
      return result.status();
    }

    result->preceding_op.ToPB(&preceding_id);
    // We use AddAllocated rather than copy, because we pin the log cache at the "all replicated"
    // point. At some point we may want to allow partially loading (and not pinning) earlier
    // messages. At that point we'll need to do something smarter here, like copy or ref-count.
    for (const auto& msg : result->messages) {
      request->mutable_ops()->AddAllocated(msg.get());
    }

    ScopedTrackedConsumption consumption;
    if (result->read_from_disk_size) {
      consumption = ScopedTrackedConsumption(operations_mem_tracker_, result->read_from_disk_size);
    }
    *msgs_holder = ReplicateMsgsHolder(
        request->mutable_ops(), std::move(result->messages), std::move(consumption));

    if (propagated_safe_time && !result->have_more_messages) {
      // Get the current local safe time on the leader and propagate it to the follower.
      request->set_propagated_safe_time(propagated_safe_time.ToUint64());
    } else {
      request->clear_propagated_safe_time();
    }
  }

  DCHECK(preceding_id.IsInitialized());
  request->mutable_preceding_id()->CopyFrom(preceding_id);

  if (PREDICT_FALSE(VLOG_IS_ON(2))) {
    if (request->ops_size() > 0) {
      VLOG_WITH_PREFIX_UNLOCKED(2) << "Sending request with operations to Peer: " << uuid
          << ". Size: " << request->ops_size()
          << ". From: " << request->ops(0).id().ShortDebugString() << ". To: "
          << request->ops(request->ops_size() - 1).id().ShortDebugString();
      VLOG_WITH_PREFIX_UNLOCKED(3) << "Operations: " << yb::ToString(request->ops());
    } else {
      VLOG_WITH_PREFIX_UNLOCKED(2)
          << "Sending " << (is_new ? "new " : "") << "status only request to Peer: " << uuid
          << ": " << request->ShortDebugString();
    }
  }

  return Status::OK();
}

Result<ReadOpsResult> PeerMessageQueue::ReadFromLogCache(int64_t from_index,
                                                         int64_t to_index,
                                                         int max_batch_size,
                                                         const std::string& peer_uuid) {
  DCHECK_LT(FLAGS_consensus_max_batch_size_bytes + 1_KB, FLAGS_rpc_max_message_size);

  // We try to get the follower's next_index from our log.
  // Note this is not using "term" and needs to change
  auto result = log_cache_.ReadOps(from_index, to_index, max_batch_size);
  if (PREDICT_FALSE(!result.ok())) {
    auto s = result.status();
    if (PREDICT_TRUE(s.IsNotFound())) {
      return s;
    } else if (s.IsIncomplete()) {
      // IsIncomplete() means that we tried to read beyond the head of the log (in the future).
      // KUDU-1078 points to a fix of this log spew issue that we've ported. This should not
      // happen under normal circumstances.
      LOG_WITH_PREFIX_UNLOCKED(ERROR) << "Error trying to read ahead of the log "
                                      << "while preparing peer request: "
                                      << s.ToString() << ". Destination peer: "
                                      << peer_uuid;
      return s;
    } else {
      LOG_WITH_PREFIX_UNLOCKED(FATAL) << "Error reading the log while preparing peer request: "
                                      << s.ToString() << ". Destination peer: "
                                      << peer_uuid;
      return s;
    }
  }
  return result;
}

// Read majority replicated messages from cache for CDC.
// CDC producer will use this to get the messages to send in response to cdc::GetChanges RPC.
Result<ReadOpsResult> PeerMessageQueue::ReadReplicatedMessagesForCDC(const yb::OpId& last_op_id,
                                                                     int64_t* repl_index) {
  // The batch of messages read from cache.

  int64_t to_index;
  {
    LockGuard lock(queue_lock_);
    to_index = queue_state_.majority_replicated_opid.index();
  }
  if (repl_index) {
    *repl_index = to_index;
  }

  if (last_op_id.index >= to_index) {
    // Nothing to read.
    return ReadOpsResult();
  }

  auto result = ReadFromLogCache(
      last_op_id.index, to_index, FLAGS_consensus_max_batch_size_bytes, local_peer_uuid_);
  if (PREDICT_FALSE(!result.ok()) && PREDICT_TRUE(result.status().IsNotFound())) {
    LOG_WITH_PREFIX_UNLOCKED(INFO) << Format(
        "The logs from index $0 have been garbage collected and cannot be read ($1)",
        last_op_id.index, result.status());
  }

  return result;
}

Status PeerMessageQueue::GetRemoteBootstrapRequestForPeer(const string& uuid,
                                                          StartRemoteBootstrapRequestPB* req) {
  TrackedPeer* peer = nullptr;
  {
    LockGuard lock(queue_lock_);
    DCHECK_EQ(queue_state_.state, State::kQueueOpen);
    DCHECK_NE(uuid, local_peer_uuid_);
    peer = FindPtrOrNull(peers_map_, uuid);
    if (PREDICT_FALSE(peer == nullptr || queue_state_.mode == Mode::NON_LEADER)) {
      return STATUS(NotFound, "Peer not tracked or queue not in leader mode.");
    }
  }

  if (PREDICT_FALSE(!peer->needs_remote_bootstrap)) {
    return STATUS(IllegalState, "Peer does not need to remotely bootstrap", uuid);
  }

  if (peer->member_type == RaftPeerPB::VOTER || peer->member_type == RaftPeerPB::OBSERVER) {
    LOG(INFO) << "Remote bootstrapping peer " << uuid << " with type "
              << RaftPeerPB::MemberType_Name(peer->member_type);
  }

  req->Clear();
  req->set_dest_uuid(uuid);
  req->set_tablet_id(tablet_id_);
  req->set_bootstrap_peer_uuid(local_peer_uuid_);
  *req->mutable_source_private_addr() = local_peer_pb_.last_known_private_addr();
  *req->mutable_source_broadcast_addr() = local_peer_pb_.last_known_broadcast_addr();
  *req->mutable_source_cloud_info() = local_peer_pb_.cloud_info();
  req->set_caller_term(queue_state_.current_term);
  peer->needs_remote_bootstrap = false; // Now reset the flag.
  return Status::OK();
}

void PeerMessageQueue::UpdateCDCConsumerOpId(const yb::OpId& op_id) {
  std::lock_guard<rw_spinlock> l(cdc_consumer_lock_);
  cdc_consumer_op_id_ = op_id;
  cdc_consumer_op_id_last_updated_ = CoarseMonoClock::Now();
}

yb::OpId PeerMessageQueue::GetCDCConsumerOpIdToEvict() {
  std::shared_lock<rw_spinlock> l(cdc_consumer_lock_);
  // For log cache eviction, we only want to include CDC consumers that are actively polling.
  // If CDC consumer checkpoint has not been updated recently, we exclude it.
  if (CoarseMonoClock::Now() - cdc_consumer_op_id_last_updated_ <= kCDCConsumerCheckpointInterval) {
    return cdc_consumer_op_id_;
  } else {
    return yb::OpId::Max();
  }
}

void PeerMessageQueue::UpdateAllReplicatedOpId(OpId* result) {
  OpId new_op_id = MaximumOpId();

  for (const auto& peer : peers_map_) {
    if (!peer.second->is_last_exchange_successful) {
      return;
    }
    if (peer.second->last_received.index() < new_op_id.index()) {
      new_op_id = peer.second->last_received;
    }
  }

  CHECK_NE(MaximumOpId().index(), new_op_id.index());
  *result = new_op_id;
}

template <class Policy>
typename Policy::result_type PeerMessageQueue::GetWatermark() {
  DCHECK(queue_lock_.is_locked());
  const int num_peers_required = queue_state_.majority_size_;
  if (num_peers_required == kUninitializedMajoritySize) {
    // We don't even know the quorum majority size yet.
    return Policy::NotEnoughPeersValue();
  }
  CHECK_GE(num_peers_required, 0);

  const size_t num_peers = peers_map_.size();
  if (num_peers < num_peers_required) {
    return Policy::NotEnoughPeersValue();
  }

  // This flag indicates whether to implicitly assume that the local peer has an "infinite"
  // replicated value of the dimension that we are computing a watermark for. There is a difference
  // in logic between handling of OpIds vs. leader leases:
  // - For OpIds, the local peer might actually be less up-to-date than followers.
  // - For leader leases, we always assume that we've replicated an "infinite" lease to ourselves.
  const bool local_peer_infinite_watermark = Policy::LocalPeerHasInfiniteWatermark();

  if (num_peers_required == 1 && local_peer_infinite_watermark) {
    // We give "infinite lease" to ourselves.
    return Policy::SingleNodeValue();
  }

  constexpr size_t kMaxPracticalReplicationFactor = 5;
  boost::container::small_vector<
      typename Policy::result_type, kMaxPracticalReplicationFactor> watermarks;
  watermarks.reserve(num_peers - 1 + !local_peer_infinite_watermark);

  for (const PeersMap::value_type &peer_map_entry : peers_map_) {
    const TrackedPeer &peer = *peer_map_entry.second;
    if (local_peer_infinite_watermark && peer.uuid == local_peer_uuid_) {
      // Don't even include the local peer in the watermarks array. Assume it has an "infinite"
      // value of the watermark.
      continue;
    }
    if (!IsRaftConfigVoter(peer.uuid, *queue_state_.active_config)) {
      // Only votes from VOTERs in the active config should be taken into consideration
      continue;
    }
    if (peer.is_last_exchange_successful) {
      watermarks.push_back(Policy::ExtractValue(peer));
    }
  }

  // We always assume that local peer has most recent information.
  const size_t num_responsive_peers = watermarks.size() + local_peer_infinite_watermark;

  if (num_responsive_peers < num_peers_required) {
    VLOG_WITH_PREFIX_UNLOCKED(2)
        << Policy::Name() << " watermarks by peer: " << ::yb::ToString(watermarks)
        << ", num_peers_required=" << num_peers_required
        << ", num_responsive_peers=" << num_responsive_peers
        << ", not enough responsive peers";
    // There are not enough peers with which the last message exchange was successful.
    return Policy::NotEnoughPeersValue();
  }

  // If there are 5 peers (and num_peers_required is 3), and we have successfully replicated
  // something to 3 of them and 4th is our local peer, there are two possibilities:
  // - If local_peer_infinite_watermark is false (for OpId): watermarks.size() is 4,
  //   and we want an OpId value such that 3 or more peers have replicated that or greater value.
  //   Then index_of_interest = 1, computed as watermarks.size() - num_peers_required, or
  //   num_responsive_peers - num_peers_required.
  //
  // - If local_peer_infinite_watermark is true (for leader leases): watermarks.size() is 3, and we
  //   are assuming that the local peer (leader) has replicated an infinitely high watermark to
  //   itself. Then watermark.size() is 3 (because we skip the local peer when populating
  //   watermarks), but num_responsive_peers is still 4, and the expression stays the same.

  const size_t index_of_interest = num_responsive_peers - num_peers_required;
  DCHECK_LT(index_of_interest, watermarks.size());

  auto nth = watermarks.begin() + index_of_interest;
  std::nth_element(watermarks.begin(), nth, watermarks.end(), typename Policy::Comparator());
  VLOG_WITH_PREFIX_UNLOCKED(2)
      << Policy::Name() << " watermarks by peer: " << ::yb::ToString(watermarks)
      << ", num_peers_required=" << num_peers_required
      << ", local_peer_infinite_watermark=" << local_peer_infinite_watermark
      << ", watermark: " << yb::ToString(*nth);

  return *nth;
}

CoarseTimePoint PeerMessageQueue::LeaderLeaseExpirationWatermark() {
  struct Policy {
    typedef CoarseTimePoint result_type;
    // Workaround for a gcc bug. That does not understand that Comparator is actually being used.
    __attribute__((unused)) typedef std::less<result_type> Comparator;

    static result_type NotEnoughPeersValue() {
      return result_type::min();
    }

    static result_type SingleNodeValue() {
      return result_type::max();
    }

    static result_type ExtractValue(const TrackedPeer& peer) {
      auto lease_exp = peer.last_leader_lease_expiration_received_by_follower;
      return lease_exp != CoarseTimePoint() ? lease_exp : CoarseTimePoint::min();
    }

    static const char* Name() {
      return "Leader lease expiration";
    }

    static bool LocalPeerHasInfiniteWatermark() {
      return true;
    }
  };

  return GetWatermark<Policy>();
}

MicrosTime PeerMessageQueue::HybridTimeLeaseExpirationWatermark() {
  struct Policy {
    typedef MicrosTime result_type;
    // Workaround for a gcc bug. That does not understand that Comparator is actually being used.
    __attribute__((unused)) typedef std::less<result_type> Comparator;

    static result_type NotEnoughPeersValue() {
      return HybridTime::kMin.GetPhysicalValueMicros();
    }

    static result_type SingleNodeValue() {
      return HybridTime::kMax.GetPhysicalValueMicros();
    }

    static result_type ExtractValue(const TrackedPeer& peer) {
      return peer.last_ht_lease_expiration_received_by_follower;
    }

    static const char* Name() {
      return "Hybrid time leader lease expiration";
    }

    static bool LocalPeerHasInfiniteWatermark() {
      return true;
    }
  };

  return GetWatermark<Policy>();
}

uint64_t PeerMessageQueue::NumSSTFilesWatermark() {
  struct Policy {
    typedef uint64_t result_type;
    // Workaround for a gcc bug. That does not understand that Comparator is actually being used.
    __attribute__((unused)) typedef std::greater<result_type> Comparator;

    static result_type NotEnoughPeersValue() {
      return 0;
    }

    static result_type SingleNodeValue() {
      return std::numeric_limits<result_type>::max();
    }

    static result_type ExtractValue(const TrackedPeer& peer) {
      return peer.num_sst_files;
    }

    static const char* Name() {
      return "Num SST files";
    }

    static bool LocalPeerHasInfiniteWatermark() {
      return false;
    }
  };

  auto watermark = GetWatermark<Policy>();
  return std::max(watermark, local_peer_->num_sst_files);
}

OpId PeerMessageQueue::OpIdWatermark() {
  struct Policy {
    typedef OpId result_type;

    static result_type NotEnoughPeersValue() {
      return MinimumOpId();
    }

    static result_type SingleNodeValue() {
      return MaximumOpId();
    }

    static result_type ExtractValue(const TrackedPeer& peer) {
      return peer.last_received;
    }

    struct Comparator {
      bool operator()(const OpId& lhs, const OpId& rhs) {
        return lhs.index() < rhs.index();
      }
    };

    static const char* Name() {
      return "OpId";
    }

    static bool LocalPeerHasInfiniteWatermark() {
      return false;
    }
  };

  return GetWatermark<Policy>();
}

void PeerMessageQueue::NotifyPeerIsResponsiveDespiteError(const std::string& peer_uuid) {
  LockGuard l(queue_lock_);
  TrackedPeer* peer = FindPtrOrNull(peers_map_, peer_uuid);
  if (!peer) return;
  peer->last_successful_communication_time = MonoTime::Now();
}

void PeerMessageQueue::ResponseFromPeer(const std::string& peer_uuid,
                                        const ConsensusResponsePB& response,
                                        bool* more_pending) {
  DCHECK(response.IsInitialized()) << "Error: Uninitialized: "
      << response.InitializationErrorString() << ". Response: " << response.ShortDebugString();

  MajorityReplicatedData majority_replicated;
  Mode mode_copy;
  {
    LockGuard scoped_lock(queue_lock_);
    DCHECK_NE(State::kQueueConstructed, queue_state_.state);

    TrackedPeer* peer = FindPtrOrNull(peers_map_, peer_uuid);
    if (PREDICT_FALSE(queue_state_.state != State::kQueueOpen || peer == nullptr)) {
      LOG_WITH_PREFIX_UNLOCKED(WARNING) << "Queue is closed or peer was untracked, disregarding "
          "peer response. Response: " << response.ShortDebugString();
      *more_pending = false;
      return;
    }

    // Remotely bootstrap the peer if the tablet is not found or deleted.
    if (response.has_error()) {
      // We only let special types of errors through to this point from the peer.
      CHECK_EQ(tserver::TabletServerErrorPB::TABLET_NOT_FOUND, response.error().code())
          << response.ShortDebugString();

      peer->needs_remote_bootstrap = true;
      // Since we received a response from the peer, we know it is alive. So we need to update
      // peer->last_successful_communication_time, otherwise, we will remove this peer from the
      // configuration if the remote bootstrap is not completed within
      // FLAGS_follower_unavailable_considered_failed_sec seconds.
      peer->last_successful_communication_time = MonoTime::Now();
      YB_LOG_WITH_PREFIX_UNLOCKED_EVERY_N_SECS(INFO, 30)
          << "Marked peer as needing remote bootstrap: " << peer->ToString();
      *more_pending = true;
      return;
    }

    if (queue_state_.active_config) {
      RaftPeerPB peer_pb;
      if (!GetRaftConfigMember(*queue_state_.active_config, peer_uuid, &peer_pb).ok()) {
        LOG(FATAL) << "Peer " << peer_uuid << " not in active config";
      }
      peer->member_type = peer_pb.member_type();
    } else {
      peer->member_type = RaftPeerPB::UNKNOWN_MEMBER_TYPE;
    }

    // Sanity checks.  Some of these can be eventually removed, but they are handy for now.
    DCHECK(response.status().IsInitialized()) << "Error: Uninitialized: "
        << response.InitializationErrorString() << ". Response: " << response.ShortDebugString();
    // TODO: Include uuid in error messages as well.
    DCHECK(response.has_responder_uuid() && !response.responder_uuid().empty())
        << "Got response from peer with empty UUID";

    // Application level errors should be handled elsewhere
    DCHECK(!response.has_error());
    // Responses should always have a status.
    DCHECK(response.has_status());
    // The status must always have a last received op id and a last committed index.
    DCHECK(response.status().has_last_received());
    DCHECK(response.status().has_last_received_current_leader());
    DCHECK(response.status().has_last_committed_idx());

    const ConsensusStatusPB& status = response.status();

    // Take a snapshot of the current peer status.
    TrackedPeer previous = *peer;

    // Update the peer status based on the response.
    peer->is_new = false;
    peer->last_known_committed_idx = status.last_committed_idx();
    peer->last_successful_communication_time = MonoTime::Now();

    // If the reported last-received op for the replica is in our local log, then resume sending
    // entries from that point onward. Otherwise, resume after the last op they received from us. If
    // we've never successfully sent them anything, start after the last-committed op in their log,
    // which is guaranteed by the Raft protocol to be a valid op.

    bool peer_has_prefix_of_log = IsOpInLog(yb::OpId::FromPB(status.last_received()));
    if (peer_has_prefix_of_log) {
      // If the latest thing in their log is in our log, we are in sync.
      peer->last_received = status.last_received();
      peer->next_index = peer->last_received.index() + 1;

    } else if (!OpIdEquals(status.last_received_current_leader(), MinimumOpId())) {
      // Their log may have diverged from ours, however we are in the process of replicating our ops
      // to them, so continue doing so. Eventually, we will cause the divergent entry in their log
      // to be overwritten.
      peer->last_received = status.last_received_current_leader();
      peer->next_index = peer->last_received.index() + 1;

    } else {
      // The peer is divergent and they have not (successfully) received anything from us yet. Start
      // sending from their last committed index.  This logic differs from the Raft spec slightly
      // because instead of stepping back one-by-one from the end until we no longer have an LMP
      // error, we jump back to the last committed op indicated by the peer with the hope that doing
      // so will result in a faster catch-up process.
      DCHECK_GE(peer->last_known_committed_idx, 0);
      peer->next_index = peer->last_known_committed_idx + 1;
    }

    if (PREDICT_FALSE(status.has_error())) {
      peer->is_last_exchange_successful = false;
      switch (status.error().code()) {
        case ConsensusErrorPB::PRECEDING_ENTRY_DIDNT_MATCH: {
          DCHECK(status.has_last_received());
          if (previous.is_new) {
            // That's currently how we can detect that we able to connect to a peer.
            LOG_WITH_PREFIX_UNLOCKED(INFO) << "Connected to new peer: " << peer->ToString();
          } else {
            LOG_WITH_PREFIX_UNLOCKED(INFO) << "Got LMP mismatch error from peer: "
                                           << peer->ToString();
          }
          *more_pending = true;
          return;
        }
        case ConsensusErrorPB::INVALID_TERM: {
          CHECK(response.has_responder_term());
          LOG_WITH_PREFIX_UNLOCKED(INFO) << "Peer responded invalid term: " << peer->ToString()
                                         << ". Peer's new term: " << response.responder_term();
          NotifyObserversOfTermChange(response.responder_term());
          *more_pending = false;
          return;
        }
        default: {
          LOG_WITH_PREFIX_UNLOCKED(FATAL) << "Unexpected consensus error. Code: "
              << ConsensusErrorPB::Code_Name(status.error().code()) << ". Response: "
              << response.ShortDebugString();
        }
      }
    }

    peer->is_last_exchange_successful = true;
    peer->num_sst_files = response.num_sst_files();

    if (response.has_responder_term()) {
      // The peer must have responded with a term that is greater than or equal to the last known
      // term for that peer.
      peer->CheckMonotonicTerms(response.responder_term());

      // If the responder didn't send an error back that must mean that it has a term that is the
      // same or lower than ours.
      CHECK_LE(response.responder_term(), queue_state_.current_term);
    }

    if (PREDICT_FALSE(VLOG_IS_ON(2))) {
      VLOG_WITH_PREFIX_UNLOCKED(2) << "Received Response from Peer (" << peer->ToString() << "). "
          << "Response: " << response.ShortDebugString();
    }

    // If our log has the next request for the peer or if the peer's committed index is lower than
    // our own, set 'more_pending' to true.
    *more_pending = log_cache_.HasOpBeenWritten(peer->next_index) ||
        (peer->last_known_committed_idx < queue_state_.committed_index.index());

    mode_copy = queue_state_.mode;
    if (mode_copy == Mode::LEADER) {
      auto new_majority_replicated_opid = OpIdWatermark();
      if (!OpIdEquals(new_majority_replicated_opid, MinimumOpId())) {
        if (new_majority_replicated_opid.index() == MaximumOpId().index()) {
          queue_state_.majority_replicated_opid = local_peer_->last_received;
        } else {
          queue_state_.majority_replicated_opid = new_majority_replicated_opid;
        }
      }
      majority_replicated.op_id = queue_state_.majority_replicated_opid;

      peer->last_leader_lease_expiration_received_by_follower =
          peer->last_leader_lease_expiration_sent_to_follower;

      peer->last_ht_lease_expiration_received_by_follower =
          peer->last_ht_lease_expiration_sent_to_follower;

      majority_replicated.leader_lease_expiration = LeaderLeaseExpirationWatermark();

      majority_replicated.ht_lease_expiration = HybridTimeLeaseExpirationWatermark();

      majority_replicated.num_sst_files = NumSSTFilesWatermark();
    }

    UpdateAllReplicatedOpId(&queue_state_.all_replicated_opid);

    auto evict_op = std::min(
        queue_state_.all_replicated_opid.index(), GetCDCConsumerOpIdToEvict().index);
    log_cache_.EvictThroughOp(evict_op);

    UpdateMetrics();
  }

  if (mode_copy == Mode::LEADER) {
    NotifyObserversOfMajorityReplOpChange(majority_replicated);
  }
}

PeerMessageQueue::TrackedPeer PeerMessageQueue::GetTrackedPeerForTests(string uuid) {
  LockGuard scoped_lock(queue_lock_);
  TrackedPeer* tracked = FindOrDie(peers_map_, uuid);
  return *tracked;
}

OpId PeerMessageQueue::GetAllReplicatedIndexForTests() const {
  LockGuard lock(queue_lock_);
  return queue_state_.all_replicated_opid;
}

OpId PeerMessageQueue::GetCommittedIndexForTests() const {
  LockGuard lock(queue_lock_);
  return queue_state_.committed_index;
}

OpId PeerMessageQueue::GetMajorityReplicatedOpIdForTests() const {
  LockGuard lock(queue_lock_);
  return queue_state_.majority_replicated_opid;
}

void PeerMessageQueue::UpdateMetrics() {
  // Since operations have consecutive indices we can update the metrics based on simple index math.
  metrics_.num_majority_done_ops->set_value(
      queue_state_.committed_index.index() -
      queue_state_.all_replicated_opid.index());
  metrics_.num_in_progress_ops->set_value(
      queue_state_.last_appended.index() -
      queue_state_.committed_index.index());
}

void PeerMessageQueue::DumpToHtml(std::ostream& out) const {
  using std::endl;

  LockGuard lock(queue_lock_);
  out << "<h3>Watermarks</h3>" << endl;
  out << "<table>" << endl;;
  out << "  <tr><th>Peer</th><th>Watermark</th></tr>" << endl;
  for (const PeersMap::value_type& entry : peers_map_) {
    out << Substitute("  <tr><td>$0</td><td>$1</td></tr>",
                      EscapeForHtmlToString(entry.first),
                      EscapeForHtmlToString(entry.second->ToString())) << endl;
  }
  out << "</table>" << endl;

  log_cache_.DumpToHtml(out);
}

void PeerMessageQueue::ClearUnlocked() {
  STLDeleteValues(&peers_map_);
  queue_state_.state = State::kQueueClosed;
}

void PeerMessageQueue::Close() {
  raft_pool_observers_token_->Shutdown();
  LockGuard lock(queue_lock_);
  ClearUnlocked();
}

string PeerMessageQueue::ToString() const {
  // Even though metrics are thread-safe obtain the lock so that we get a "consistent" snapshot of
  // the metrics.
  LockGuard lock(queue_lock_);
  return ToStringUnlocked();
}

string PeerMessageQueue::ToStringUnlocked() const {
  return Substitute("Consensus queue metrics:"
                    "Only Majority Done Ops: $0, In Progress Ops: $1, Cache: $2",
                    metrics_.num_majority_done_ops->value(), metrics_.num_in_progress_ops->value(),
                    log_cache_.StatsString());
}

void PeerMessageQueue::RegisterObserver(PeerMessageQueueObserver* observer) {
  LockGuard lock(queue_lock_);
  auto iter = std::find(observers_.begin(), observers_.end(), observer);
  if (iter == observers_.end()) {
    observers_.push_back(observer);
  }
}

Status PeerMessageQueue::UnRegisterObserver(PeerMessageQueueObserver* observer) {
  LockGuard lock(queue_lock_);
  auto iter = std::find(observers_.begin(), observers_.end(), observer);
  if (iter == observers_.end()) {
    return STATUS(NotFound, "Can't find observer.");
  }
  observers_.erase(iter);
  return Status::OK();
}

const char* PeerMessageQueue::ModeToStr(Mode mode) {
  switch (mode) {
    case Mode::LEADER: return "LEADER";
    case Mode::NON_LEADER: return "NON_LEADER";
  }
  FATAL_INVALID_ENUM_VALUE(PeerMessageQueue::Mode, mode);
}

const char* PeerMessageQueue::StateToStr(State state) {
  switch (state) {
    case State::kQueueConstructed:
      return "QUEUE_CONSTRUCTED";
    case State::kQueueOpen:
      return "QUEUE_OPEN";
    case State::kQueueClosed:
      return "QUEUE_CLOSED";

  }
  FATAL_INVALID_ENUM_VALUE(PeerMessageQueue::State, state);
}

bool PeerMessageQueue::IsOpInLog(const yb::OpId& desired_op) const {
  auto result = log_cache_.LookupOpId(desired_op.index);
  if (PREDICT_TRUE(result.ok())) {
    return desired_op == *result;
  }
  if (PREDICT_TRUE(result.status().IsNotFound() || result.status().IsIncomplete())) {
    return false;
  }
  LOG_WITH_PREFIX_UNLOCKED(FATAL) << "Error while reading the log: " << result.status();
  return false; // Unreachable; here to squelch GCC warning.
}

void PeerMessageQueue::NotifyObserversOfMajorityReplOpChange(
    const MajorityReplicatedData& majority_replicated_data) {
  WARN_NOT_OK(raft_pool_observers_token_->SubmitClosure(
      Bind(&PeerMessageQueue::NotifyObserversOfMajorityReplOpChangeTask,
           Unretained(this),
           majority_replicated_data)),
      LogPrefixUnlocked() + "Unable to notify RaftConsensus of "
                           "majority replicated op change.");
}

void PeerMessageQueue::NotifyObserversOfTermChange(int64_t term) {
  WARN_NOT_OK(raft_pool_observers_token_->SubmitClosure(
      Bind(&PeerMessageQueue::NotifyObserversOfTermChangeTask,
           Unretained(this), term)),
              LogPrefixUnlocked() + "Unable to notify RaftConsensus of term change.");
}

void PeerMessageQueue::NotifyObserversOfMajorityReplOpChangeTask(
    const MajorityReplicatedData& majority_replicated_data) {
  std::vector<PeerMessageQueueObserver*> copy;
  {
    LockGuard lock(queue_lock_);
    copy = observers_;
  }

  // TODO move commit index advancement here so that the queue is not dependent on consensus at all,
  // but that requires a bit more work.
  OpId new_committed_index;
  for (PeerMessageQueueObserver* observer : copy) {
    observer->UpdateMajorityReplicated(majority_replicated_data, &new_committed_index);
  }

  {
    LockGuard lock(queue_lock_);
    if (new_committed_index.IsInitialized() &&
        new_committed_index.index() > queue_state_.committed_index.index()) {
      queue_state_.committed_index.CopyFrom(new_committed_index);
    }
  }
}

void PeerMessageQueue::NotifyObserversOfTermChangeTask(int64_t term) {
  MAYBE_INJECT_RANDOM_LATENCY(FLAGS_consensus_inject_latency_ms_in_notifications);
  std::vector<PeerMessageQueueObserver*> copy;
  {
    LockGuard lock(queue_lock_);
    copy = observers_;
  }
  for (PeerMessageQueueObserver* observer : copy) {
    observer->NotifyTermChange(term);
  }
}

void PeerMessageQueue::NotifyObserversOfFailedFollower(const string& uuid,
                                                       const string& reason) {
  int64_t current_term;
  {
    LockGuard lock(queue_lock_);
    current_term = queue_state_.current_term;
  }
  NotifyObserversOfFailedFollower(uuid, current_term, reason);
}

void PeerMessageQueue::NotifyObserversOfFailedFollower(const string& uuid,
                                                       int64_t term,
                                                       const string& reason) {
  WARN_NOT_OK(raft_pool_observers_token_->SubmitClosure(
      Bind(&PeerMessageQueue::NotifyObserversOfFailedFollowerTask,
           Unretained(this), uuid, term, reason)),
              LogPrefixUnlocked() + "Unable to notify RaftConsensus of abandoned follower.");
}

void PeerMessageQueue::NotifyObserversOfFailedFollowerTask(const string& uuid,
                                                           int64_t term,
                                                           const string& reason) {
  MAYBE_INJECT_RANDOM_LATENCY(FLAGS_consensus_inject_latency_ms_in_notifications);
  std::vector<PeerMessageQueueObserver*> observers_copy;
  {
    LockGuard lock(queue_lock_);
    observers_copy = observers_;
  }
  for (PeerMessageQueueObserver* observer : observers_copy) {
    observer->NotifyFailedFollower(uuid, term, reason);
  }
}

bool PeerMessageQueue::PeerAcceptedOurLease(const std::string& uuid) const {
  std::lock_guard<simple_spinlock> lock(queue_lock_);
  TrackedPeer* peer = FindPtrOrNull(peers_map_, uuid);
  if (peer == nullptr) {
    return false;
  }

  return peer->last_leader_lease_expiration_received_by_follower != CoarseTimePoint();
}

bool PeerMessageQueue::CanPeerBecomeLeader(const std::string& peer_uuid) const {
  std::lock_guard<simple_spinlock> lock(queue_lock_);
  TrackedPeer* peer = FindPtrOrNull(peers_map_, peer_uuid);
  if (peer == nullptr) {
    LOG(ERROR) << "Invalid peer UUID: " << peer_uuid;
    return false;
  }
  const bool peer_can_be_leader =
      !OpIdLessThan(peer->last_received, queue_state_.majority_replicated_opid);
  if (!peer_can_be_leader) {
    LOG(INFO) << Substitute(
        "Peer $0 cannot become Leader as it is not caught up: Majority OpId $1, Peer OpId $2",
        peer_uuid, OpIdToString(queue_state_.majority_replicated_opid),
        OpIdToString(peer->last_received));
  }
  return peer_can_be_leader;
}

PeerMessageQueue::~PeerMessageQueue() {
  Close();
}

string PeerMessageQueue::LogPrefixUnlocked() const {
  // TODO: we should probably use an atomic here. We'll just annotate away the TSAN error for now,
  // since the worst case is a slightly out-of-date log message, and not very likely.
  Mode mode = ANNOTATE_UNPROTECTED_READ(queue_state_.mode);
  return Substitute("T $0 P $1 [$2]: ",
                    tablet_id_,
                    local_peer_uuid_,
                    ModeToStr(mode));
}

string PeerMessageQueue::QueueState::ToString() const {
  return Substitute("All replicated op: $0, Majority replicated op: $1, "
      "Committed index: $2, Last appended: $3, Current term: $4, Majority size: $5, "
      "State: $6, Mode: $7$8",
      /* 0 */ OpIdToString(all_replicated_opid),
      /* 1 */ OpIdToString(majority_replicated_opid),
      /* 2 */ OpIdToString(committed_index),
      /* 3 */ OpIdToString(last_appended),
      /* 4 */ current_term,
      /* 5 */ majority_size_,
      /* 6 */ StateToStr(state),
      /* 7 */ ModeToStr(mode),
      /* 8 */ active_config ? ", active raft config: " + active_config->ShortDebugString() : "");
}

size_t PeerMessageQueue::LogCacheSize() {
  return log_cache_.BytesUsed();
}

size_t PeerMessageQueue::EvictLogCache(size_t bytes_to_evict) {
  return log_cache_.EvictThroughOp(std::numeric_limits<int64_t>::max(), bytes_to_evict);
}

void PeerMessageQueue::TrackOperationsMemory(const OpIds& op_ids) {
  log_cache_.TrackOperationsMemory(op_ids);
}

}  // namespace consensus
}  // namespace yb

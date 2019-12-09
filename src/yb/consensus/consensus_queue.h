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

#ifndef YB_CONSENSUS_CONSENSUS_QUEUE_H_
#define YB_CONSENSUS_CONSENSUS_QUEUE_H_

#include <iosfwd>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/log_cache.h"
#include "yb/consensus/log_util.h"
#include "yb/consensus/opid_util.h"

#include "yb/server/clock.h"

#include "yb/gutil/ref_counted.h"

#include "yb/util/locks.h"
#include "yb/util/status.h"
#include "yb/util/result.h"

namespace yb {
template<class T>
class AtomicGauge;
class MemTracker;
class MetricEntity;
class ThreadPoolToken;

namespace log {
class Log;
class AsyncLogReader;
}

namespace consensus {
class PeerMessageQueueObserver;
struct MajorityReplicatedData;

// The id for the server-wide consensus queue MemTracker.
extern const char kConsensusQueueParentTrackerId[];

// Tracks the state of the peers and which transactions they have replicated.  Owns the LogCache
// which actually holds the replicate messages which are en route to the various peers.
//
// This also takes care of pushing requests to peers as new operations are added, and notifying
// RaftConsensus when the commit index advances.
//
// This class is used only on the LEADER side.
//
// TODO Currently this class is able to track one outstanding operation per peer. If we want to have
// more than one outstanding RPC we need to modify it.
class PeerMessageQueue {
 public:
  struct TrackedPeer {
    explicit TrackedPeer(std::string uuid)
        : uuid(std::move(uuid)),
          last_received(MinimumOpId()),
          last_known_committed_idx(MinimumOpId().index()),
          last_successful_communication_time(MonoTime::Now()) {}

    // Check that the terms seen from a given peer only increase monotonically.
    void CheckMonotonicTerms(int64_t term) {
      DCHECK_GE(term, last_seen_term_);
      last_seen_term_ = term;
    }

    std::string ToString() const;

    void ResetLeaderLeases();

    // UUID of the peer.
    const std::string uuid;

    // Whether this is a newly tracked peer.
    bool is_new = true;

    // Next index to send to the peer.  This corresponds to "nextIndex" as specified in Raft.
    int64_t next_index = kInvalidOpIdIndex;

    // The last operation that we've sent to this peer and that it acked. Used for watermark
    // movement.
    OpId last_received;

    // The last committed index this peer knows about.
    int64_t last_known_committed_idx;

    // Whether the last exchange with this peer was successful.
    bool is_last_exchange_successful = false;

    // The time of the last communication with the peer.
    // Defaults to the time of construction, so does not necessarily mean that
    // successful communication ever took place.
    MonoTime last_successful_communication_time;

    // The leader sets this field to the time of sending request + leader lease duration, and uses
    // it when handling the response to establish the majority replicated lease expiration
    // timestamp. This is not actually sent to the follower: what we're sending is the lease
    // duration, not an expiration timestamp, because the timestamp is specific to the leader's
    // monotonic clock.
    CoarseTimePoint last_leader_lease_expiration_sent_to_follower;

    // The last leader lease expiration timestamp received by the follower described by this
    // TrackedPeer. We set this to the value of what we sent to that follower
    // (last_leader_lease_expiration_sent_to_follower) when we receive the follower's response.
    CoarseTimePoint last_leader_lease_expiration_received_by_follower;

    MicrosTime last_ht_lease_expiration_sent_to_follower =
        HybridTime::kMin.GetPhysicalValueMicros();

    MicrosTime last_ht_lease_expiration_received_by_follower =
        HybridTime::kMin.GetPhysicalValueMicros();

    // Whether the follower was detected to need remote bootstrap.
    bool needs_remote_bootstrap = false;

    // Member type of this peer in the config.
    RaftPeerPB::MemberType member_type = RaftPeerPB::UNKNOWN_MEMBER_TYPE;

    uint64_t num_sst_files = 0;

   private:
    // The last term we saw from a given peer.
    // This is only used for sanity checking that a peer doesn't
    // go backwards in time.
    int64_t last_seen_term_ = 0;
  };

  PeerMessageQueue(const scoped_refptr<MetricEntity>& metric_entity,
                   const scoped_refptr<log::Log>& log,
                   const std::shared_ptr<MemTracker>& server_tracker,
                   const std::shared_ptr<MemTracker>& parent_tracker,
                   const RaftPeerPB& local_peer_pb,
                   const std::string& tablet_id,
                   const server::ClockPtr& clock,
                   ConsensusContext* context,
                   std::unique_ptr<ThreadPoolToken> raft_pool_observers_token);

  // Initialize the queue.
  virtual void Init(const OpId& last_locally_replicated);

  // Changes the queue to leader mode, meaning it tracks majority replicated operations and notifies
  // observers when those change.
  //
  // 'committed_index' corresponds to the id of the last committed operation, i.e. operations with
  // ids <= 'committed_index' should be considered committed.
  //
  // 'current_term' corresponds to the leader's current term, this is different from
  // 'committed_index.term()' if the leader has not yet committed an operation in the current term.
  //
  // 'active_config' is the currently-active Raft config. This must always be a superset of the
  // tracked peers, and that is enforced with runtime CHECKs.
  virtual void SetLeaderMode(const OpId& committed_index,
                             int64_t current_term,
                             const RaftConfigPB& active_config);

  // Changes the queue to non-leader mode. Currently tracked peers will still be tracked so that the
  // cache is only evicted when the peers no longer need the operations but the queue will no longer
  // advance the majority replicated index or notify observers of its advancement.
  virtual void SetNonLeaderMode();

  // Makes the queue track this peer.
  virtual void TrackPeer(const std::string& peer_uuid);

  // Makes the queue untrack this peer.
  virtual void UntrackPeer(const std::string& peer_uuid);

  // Appends a single message to be replicated to the peers.  Returns OK unless the message could
  // not be added to the queue for some reason (e.g. the queue reached max size).
  //
  // If it returns OK the queue takes ownership of 'msg'.
  //
  // This is thread-safe against all of the read methods, but not thread-safe with concurrent Append
  // calls.
  CHECKED_STATUS TEST_AppendOperation(const ReplicateMsgPtr& msg);

  // Appends a vector of messages to be replicated to the peers.  Returns OK unless the message
  // could not be added to the queue for some reason (e.g. the queue reached max size). Calls
  // 'log_append_callback' when the messages are durable in the local Log.
  //
  // If it returns OK the queue takes ownership of 'msgs'.
  //
  // This is thread-safe against all of the read methods, but not thread-safe with concurrent Append
  // calls.
  //
  // It is possible that this method will be invoked with empty list of messages, when
  // we update committed op id.
  virtual CHECKED_STATUS AppendOperations(
      const ReplicateMsgs& msgs, const yb::OpId& committed_op_id,
      RestartSafeCoarseTimePoint batch_mono_time);

  // Assembles a request for a peer, adding entries past 'op_id' up to
  // 'consensus_max_batch_size_bytes'.
  //
  // Returns OK if the request was assembled, or STATUS(NotFound, "") if the peer with 'uuid' was
  // not tracked, or if the queue is not in leader mode.
  //
  // Returns STATUS(Incomplete, "") if we try to read an operation index from the log that has not
  // been written.
  //
  // WARNING: In order to avoid copying the same messages to every peer, entries are added to
  // 'request' via AddAllocated() methods.  The owner of 'request' is expected not to delete the
  // request prior to removing the entries through ExtractSubRange() or any other method that does
  // not delete the entries. The simplest way is to pass the same instance of ConsensusRequestPB to
  // RequestForPeer(): the buffer will replace the old entries with new ones without de-allocating
  // the old ones if they are still required.
  virtual CHECKED_STATUS RequestForPeer(
      const std::string& uuid,
      ConsensusRequestPB* request,
      ReplicateMsgsHolder* msgs_holder,
      bool* needs_remote_bootstrap,
      RaftPeerPB::MemberType* member_type = nullptr,
      bool* last_exchange_successful = nullptr);

  // Fill in a StartRemoteBootstrapRequest for the specified peer.  If that peer should not remotely
  // bootstrap, returns a non-OK status.  On success, also internally resets
  // peer->needs_remote_bootstrap to false.
  CHECKED_STATUS GetRemoteBootstrapRequestForPeer(
      const std::string& uuid,
      StartRemoteBootstrapRequestPB* req);

  // Update the last successful communication timestamp for the given peer to the current time. This
  // should be called when a non-network related error is received from the peer, indicating that it
  // is alive, even if it may not be fully up and running or able to accept updates.
  void NotifyPeerIsResponsiveDespiteError(const std::string& peer_uuid);

  // Updates the request queue with the latest response of a peer, returns whether this peer has
  // more requests pending.
  virtual void ResponseFromPeer(const std::string& peer_uuid,
                                const ConsensusResponsePB& response,
                                bool* more_pending);

  // Closes the queue, peers are still allowed to call UntrackPeer() and ResponseFromPeer() but no
  // additional peers can be tracked or messages queued.
  virtual void Close();

  // Returns the last message replicated by all peers, for tests.
  OpId GetAllReplicatedIndexForTests() const;

  OpId GetCommittedIndexForTests() const;

  // Returns the current majority replicated OpId, for tests.
  OpId GetMajorityReplicatedOpIdForTests() const;

  // Returns true if specified peer accepted our lease request.
  bool PeerAcceptedOurLease(const std::string& uuid) const;

  // Returns a copy of the TrackedPeer with 'uuid' or crashes if the peer is not being tracked.
  TrackedPeer GetTrackedPeerForTests(std::string uuid);

  std::string ToString() const;

  void DumpToHtml(std::ostream& out) const;

  void RegisterObserver(PeerMessageQueueObserver* observer);

  CHECKED_STATUS UnRegisterObserver(PeerMessageQueueObserver* observer);

  bool CanPeerBecomeLeader(const std::string& peer_uuid) const;

  struct Metrics {
    // Keeps track of the number of ops. that are completed by a majority but still need
    // to be replicated to a minority (IsDone() is true, IsAllDone() is false).
    scoped_refptr<AtomicGauge<int64_t> > num_majority_done_ops;
    // Keeps track of the number of ops. that are still in progress (IsDone() returns false).
    scoped_refptr<AtomicGauge<int64_t> > num_in_progress_ops;

    explicit Metrics(const scoped_refptr<MetricEntity>& metric_entity);
  };

  virtual ~PeerMessageQueue();

  void NotifyObserversOfFailedFollower(const std::string& uuid,
                                       const std::string& reason);

  void SetContext(ConsensusContext* context) {
    context_ = context;
  }

  const CloudInfoPB& local_cloud_info() const {
    return local_peer_pb_.cloud_info();
  }

  // Read replicated log records starting from the OpId immediately after last_op_id.
  Result<ReadOpsResult> ReadReplicatedMessagesForCDC(const yb::OpId& last_op_id,
                                                     int64_t* last_replicated_opid_index = nullptr);

  void UpdateCDCConsumerOpId(const yb::OpId& op_id);

  // Get the maximum op ID that can be evicted for CDC consumer from log cache.
  yb::OpId GetCDCConsumerOpIdToEvict();

  size_t LogCacheSize();
  size_t EvictLogCache(size_t bytes_to_evict);

  // Start memory tracking of following operations in case they are still present in our caches.
  void TrackOperationsMemory(const OpIds& op_ids);

  const server::ClockPtr& clock() const {
    return clock_;
  }

 private:
  FRIEND_TEST(ConsensusQueueTest, TestQueueAdvancesCommittedIndex);
  FRIEND_TEST(ConsensusQueueTest, TestReadReplicatedMessagesForCDC);

  // Mode specifies how the queue currently behaves:
  //
  // LEADER - Means the queue tracks remote peers and replicates whatever messages are appended.
  //          Observers are notified of changes.
  //
  // NON_LEADER - Means the queue only tracks the local peer (remote peers are ignored).  Observers
  //              are not notified of changes.
  enum class Mode {
    LEADER,
    NON_LEADER
  };

  static const char* ModeToStr(Mode mode);
  friend std::ostream& operator <<(std::ostream& out, Mode mode);

  enum class State {
    kQueueConstructed,
    kQueueOpen,
    kQueueClosed
  };

  static const char* StateToStr(State state);
  friend std::ostream& operator <<(std::ostream& out, State mode);

  static constexpr int kUninitializedMajoritySize = -1;

  struct QueueState {

    // The first operation that has been replicated to all currently tracked peers.
    OpId all_replicated_opid = MinimumOpId();

    // The index of the last operation replicated to a majority.  This is usually the same as
    // 'committed_index' but might not be if the terms changed.
    OpId majority_replicated_opid = MinimumOpId();

    // The index of the last operation to be considered committed.
    OpId committed_index = MinimumOpId();

    // The opid of the last operation appended to the queue.
    OpId last_appended = MinimumOpId();

    // The queue's owner current_term.  Set by the last appended operation.  If the queue owner's
    // term is less than the term observed from another peer the queue owner must step down.
    // TODO: it is likely to be cleaner to get this from the ConsensusMetadata rather than by
    // snooping on what operations are appended to the queue.
    int64_t current_term = MinimumOpId().term();

    // The size of the majority for the queue.
    int majority_size_ = kUninitializedMajoritySize;

    State state = State::kQueueConstructed;

    // The current mode of the queue.
    Mode mode = Mode::NON_LEADER;

    // The currently-active raft config. Only set if in LEADER mode.
    gscoped_ptr<RaftConfigPB> active_config;

    std::string ToString() const;
  };

  // Returns true iff given 'desired_op' is found in the local WAL.
  // If the op is not found, returns false.
  // If the log cache returns some error other than NotFound, crashes with a fatal error.
  bool IsOpInLog(const yb::OpId& desired_op) const;

  void NotifyObserversOfMajorityReplOpChange(const MajorityReplicatedData& data);

  void NotifyObserversOfMajorityReplOpChangeTask(const MajorityReplicatedData& data);

  void NotifyObserversOfTermChange(int64_t term);
  void NotifyObserversOfTermChangeTask(int64_t term);

  void NotifyObserversOfFailedFollower(const std::string& uuid,
                                       int64_t term,
                                       const std::string& reason);
  void NotifyObserversOfFailedFollowerTask(const std::string& uuid,
                                           int64_t term,
                                           const std::string& reason);

  typedef std::unordered_map<std::string, TrackedPeer*> PeersMap;

  std::string ToStringUnlocked() const;

  std::string LogPrefixUnlocked() const;

  // Updates the metrics based on index math.
  void UpdateMetrics();

  void ClearUnlocked();

  // Returns the last operation in the message queue, or 'preceding_first_op_in_queue_' if the queue
  // is empty.
  const OpId& GetLastOp() const;

  TrackedPeer* TrackPeerUnlocked(const std::string& uuid);

  // Checks that if the queue is in LEADER mode then all registered peers are in the active config.
  // Crashes with a FATAL log message if this invariant does not hold. If the queue is in NON_LEADER
  // mode, does nothing.
  void CheckPeersInActiveConfigIfLeaderUnlocked() const;

  // Callback when a REPLICATE message has finished appending to the local log.
  void LocalPeerAppendFinished(const OpId& id,
                               const Status& status);

  // Updates op id replicated on each node.
  void UpdateAllReplicatedOpId(OpId* result);

  // Policy is responsible for tuning of watermark calculation.
  // I.e. simple leader lease or hybrid time leader lease etc.
  // It should provide result type and a function for extracting a value from a peer.
  template <class Policy>
  typename Policy::result_type GetWatermark();

  CoarseTimePoint LeaderLeaseExpirationWatermark();
  MicrosTime HybridTimeLeaseExpirationWatermark();
  OpId OpIdWatermark();
  uint64_t NumSSTFilesWatermark();

  Result<ReadOpsResult> ReadFromLogCache(int64_t from_index,
                                         int64_t to_index,
                                         int max_batch_size,
                                         const std::string& peer_uuid);

  std::vector<PeerMessageQueueObserver*> observers_;

  // The pool token which executes observer notifications.
  std::unique_ptr<ThreadPoolToken> raft_pool_observers_token_;

  // PB containing identifying information about the local peer.
  const RaftPeerPB local_peer_pb_;
  const yb::PeerId local_peer_uuid_;

  const TabletId tablet_id_;

  QueueState queue_state_;

  // The currently tracked peers.
  PeersMap peers_map_;
  TrackedPeer* local_peer_ = nullptr;

  using LockType = simple_spinlock;
  using LockGuard = std::lock_guard<LockType>;
  mutable LockType queue_lock_; // TODO: rename

  // We assume that we never have multiple threads racing to append to the queue.  This fake mutex
  // adds some extra assurance that this implementation property doesn't change.
  DFAKE_MUTEX(append_fake_lock_);

  LogCache log_cache_;

  std::shared_ptr<MemTracker> operations_mem_tracker_;

  Metrics metrics_;

  server::ClockPtr clock_;

  ConsensusContext* context_ = nullptr;

  // Used to protect cdc_consumer_op_id_ and cdc_consumer_op_id_last_updated_.
  mutable rw_spinlock cdc_consumer_lock_;
  yb::OpId cdc_consumer_op_id_ = yb::OpId::Max();
  CoarseTimePoint cdc_consumer_op_id_last_updated_ = ToCoarse(MonoTime::kMin);
};

inline std::ostream& operator <<(std::ostream& out, PeerMessageQueue::Mode mode) {
  return out << PeerMessageQueue::ModeToStr(mode);
}

inline std::ostream& operator <<(std::ostream& out, PeerMessageQueue::State state) {
  return out << PeerMessageQueue::StateToStr(state);
}

struct MajorityReplicatedData {
  OpId op_id;
  CoarseTimePoint leader_lease_expiration;
  MicrosTime ht_lease_expiration;
  uint64_t num_sst_files;

  std::string ToString() const;
};

// The interface between RaftConsensus and the PeerMessageQueue.
class PeerMessageQueueObserver {
 public:
  // Called by the queue each time the response for a peer is handled with the resulting majority
  // replicated index.  The consensus implementation decides the commit index based on that and
  // triggers the apply for pending transactions.
  //
  // 'committed_index' is set to the id of the last operation considered committed by consensus.
  //
  // The implementation is idempotent, i.e. independently of the ordering of calls to this method
  // only non-triggered applys will be started.
  virtual void UpdateMajorityReplicated(const MajorityReplicatedData& data,
                                        OpId* committed_index) = 0;

  // Notify the Consensus implementation that a follower replied with a term higher than that
  // established in the queue.
  virtual void NotifyTermChange(int64_t term) = 0;

  // Notify Consensus that a peer is unable to catch up due to falling behind the leader's log GC
  // threshold.
  virtual void NotifyFailedFollower(const std::string& peer_uuid,
                                    int64_t term,
                                    const std::string& reason) = 0;

  virtual ~PeerMessageQueueObserver() {}
};

}  // namespace consensus
}  // namespace yb

#endif // YB_CONSENSUS_CONSENSUS_QUEUE_H_

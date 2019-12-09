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

#ifndef YB_DOCDB_CONSENSUS_FRONTIER_H
#define YB_DOCDB_CONSENSUS_FRONTIER_H

#include "yb/rocksdb/metadata.h"

namespace yb {
namespace docdb {

inline HybridTime NormalizeHistoryCutoff(HybridTime history_cutoff) {
  if (history_cutoff == HybridTime::kMin) {
    return HybridTime::kInvalid;
  }
  return history_cutoff;
}

// DocDB implementation of RocksDB UserFrontier. Contains an op id and a hybrid time. The difference
// between this and user boundary values is that here hybrid time is taken from committed Raft log
// entries, whereas user boundary values extract hybrid time from keys in a memtable. This is
// important for transactions, because boundary values would have the commit time of a transaction,
// but e.g. "apply intent" Raft log entries will have a later hybrid time, which would be reflected
// here.
class ConsensusFrontier : public rocksdb::UserFrontier {
 public:
  std::unique_ptr<UserFrontier> Clone() const override {
    return std::make_unique<ConsensusFrontier>(*this);
  }
  ConsensusFrontier() {}
  ConsensusFrontier(const OpId& op_id, HybridTime ht, HybridTime history_cutoff)
      : op_id_(op_id),
        ht_(ht),
        history_cutoff_(NormalizeHistoryCutoff(history_cutoff)) {}

  virtual ~ConsensusFrontier();

  bool Equals(const UserFrontier& rhs) const override;
  std::string ToString() const override;
  void ToPB(google::protobuf::Any* pb) const override;
  void Update(const rocksdb::UserFrontier& rhs, rocksdb::UpdateUserValueType type) override;
  bool IsUpdateValid(const rocksdb::UserFrontier& rhs, rocksdb::UpdateUserValueType type) const
      override;
  void FromPB(const google::protobuf::Any& pb) override;
  void FromOpIdPBDeprecated(const OpIdPB& pb) override;

  const OpId& op_id() const { return op_id_; }
  void set_op_id(const OpId& value) { op_id_ = value; }

  template <class PB>
  void set_op_id(const PB& pb) { op_id_ = OpId::FromPB(pb); }

  HybridTime hybrid_time() const { return ht_; }
  void set_hybrid_time(HybridTime ht) { ht_ = ht; }

  HybridTime history_cutoff() const { return history_cutoff_; }
  void set_history_cutoff(HybridTime history_cutoff) {
    history_cutoff_ = NormalizeHistoryCutoff(history_cutoff);
  }

 private:
  OpId op_id_;
  HybridTime ht_;

  // We use this to keep track of the maximum history cutoff hybrid time used in any compaction, and
  // refuse to perform reads at a hybrid time at which we don't have a valid snapshot anymore. Only
  // the largest frontier of this parameter is being used.
  HybridTime history_cutoff_;
};

typedef rocksdb::UserFrontiersBase<ConsensusFrontier> ConsensusFrontiers;

inline void set_op_id(const OpId& op_id, ConsensusFrontiers* frontiers) {
  frontiers->Smallest().set_op_id(op_id);
  frontiers->Largest().set_op_id(op_id);
}

inline void set_hybrid_time(HybridTime hybrid_time, ConsensusFrontiers* frontiers) {
  frontiers->Smallest().set_hybrid_time(hybrid_time);
  frontiers->Largest().set_hybrid_time(hybrid_time);
}

inline void set_history_cutoff(HybridTime history_cutoff, ConsensusFrontiers* frontiers) {
  frontiers->Smallest().set_history_cutoff(history_cutoff);
  frontiers->Largest().set_history_cutoff(history_cutoff);
}
} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_CONSENSUS_FRONTIER_H

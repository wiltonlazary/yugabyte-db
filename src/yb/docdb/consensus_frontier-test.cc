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

#include "yb/docdb/consensus_frontier.h"

#include "yb/util/test_util.h"
#include "yb/rocksdb/metadata.h"

#include "yb/docdb/docdb.pb.h"

using rocksdb::UpdateUserValueType;

namespace yb {
namespace docdb {

class ConsensusFrontierTest : public YBTest {
};

namespace {

std::string PbToString(const ConsensusFrontierPB& pb) {
  google::protobuf::Any any;
  any.PackFrom(pb);
  ConsensusFrontier frontier;
  frontier.FromPB(any);
  return frontier.ToString();
}

}  // anonymous namespace

TEST_F(ConsensusFrontierTest, TestUpdates) {
  {
    ConsensusFrontier frontier;
    EXPECT_TRUE(frontier.Equals(frontier));
    EXPECT_EQ(
        "{ op_id: 0.0 hybrid_time: <invalid> history_cutoff: <invalid> }",
        frontier.ToString());
    EXPECT_TRUE(frontier.IsUpdateValid(frontier, UpdateUserValueType::kLargest));
    EXPECT_TRUE(frontier.IsUpdateValid(frontier, UpdateUserValueType::kSmallest));

    ConsensusFrontier opid1{{0, 1}, HybridTime::kInvalid, HybridTime::kInvalid};
    EXPECT_TRUE(frontier.IsUpdateValid(opid1, UpdateUserValueType::kLargest));
  }

  {
    ConsensusFrontier frontier{{1, 1}, 1000_usec_ht, 500_usec_ht};
    EXPECT_EQ(
        "{ op_id: 1.1 hybrid_time: { physical: 1000 } history_cutoff: { physical: 500 } }",
        frontier.ToString());
    ConsensusFrontier higher_idx{{1, 2}, 1000_usec_ht, 500_usec_ht};
    ConsensusFrontier higher_ht{{1, 1}, 1001_usec_ht, 500_usec_ht};
    ConsensusFrontier higher_cutoff{{1, 1}, 1000_usec_ht, 501_usec_ht};
    ConsensusFrontier higher_idx_lower_ht{{1, 2}, 999_usec_ht, 500_usec_ht};

    EXPECT_TRUE(higher_idx.Dominates(frontier, UpdateUserValueType::kLargest));
    EXPECT_TRUE(higher_ht.Dominates(frontier, UpdateUserValueType::kLargest));
    EXPECT_TRUE(higher_cutoff.Dominates(frontier, UpdateUserValueType::kLargest));
    EXPECT_FALSE(higher_idx.Dominates(frontier, UpdateUserValueType::kSmallest));
    EXPECT_FALSE(higher_ht.Dominates(frontier, UpdateUserValueType::kSmallest));
    EXPECT_FALSE(higher_cutoff.Dominates(frontier, UpdateUserValueType::kSmallest));
    EXPECT_FALSE(frontier.Dominates(higher_idx, UpdateUserValueType::kLargest));
    EXPECT_FALSE(frontier.Dominates(higher_ht, UpdateUserValueType::kLargest));
    EXPECT_FALSE(frontier.Dominates(higher_cutoff, UpdateUserValueType::kLargest));
    EXPECT_TRUE(frontier.Dominates(higher_idx, UpdateUserValueType::kSmallest));
    EXPECT_TRUE(frontier.Dominates(higher_ht, UpdateUserValueType::kSmallest));
    EXPECT_TRUE(frontier.Dominates(higher_cutoff, UpdateUserValueType::kSmallest));

    // frontier and higher_idx_lower_ht are "incomparable" according to the "dominates" ordering.
    EXPECT_FALSE(frontier.Dominates(higher_idx_lower_ht, UpdateUserValueType::kSmallest));
    EXPECT_FALSE(frontier.Dominates(higher_idx_lower_ht, UpdateUserValueType::kLargest));
    EXPECT_FALSE(higher_idx_lower_ht.Dominates(frontier, UpdateUserValueType::kSmallest));
    EXPECT_FALSE(higher_idx_lower_ht.Dominates(frontier, UpdateUserValueType::kLargest));

    EXPECT_TRUE(frontier.IsUpdateValid(higher_idx, UpdateUserValueType::kLargest));
    EXPECT_TRUE(frontier.IsUpdateValid(higher_ht, UpdateUserValueType::kLargest));
    EXPECT_FALSE(higher_idx.IsUpdateValid(frontier, UpdateUserValueType::kLargest));
    EXPECT_FALSE(higher_ht.IsUpdateValid(frontier, UpdateUserValueType::kLargest));
    EXPECT_FALSE(frontier.IsUpdateValid(higher_idx, UpdateUserValueType::kSmallest));
    EXPECT_FALSE(frontier.IsUpdateValid(higher_ht, UpdateUserValueType::kSmallest));
    EXPECT_TRUE(higher_idx.IsUpdateValid(frontier, UpdateUserValueType::kSmallest));
    EXPECT_TRUE(higher_ht.IsUpdateValid(frontier, UpdateUserValueType::kSmallest));

    EXPECT_FALSE(higher_idx_lower_ht.IsUpdateValid(frontier, UpdateUserValueType::kLargest));
    EXPECT_FALSE(frontier.IsUpdateValid(higher_idx_lower_ht, UpdateUserValueType::kSmallest));

    // It is OK if a later compaction runs at a lower history_cutoff.
    EXPECT_TRUE(frontier.IsUpdateValid(higher_cutoff, UpdateUserValueType::kLargest));
    EXPECT_TRUE(frontier.IsUpdateValid(higher_cutoff, UpdateUserValueType::kSmallest));
    EXPECT_TRUE(higher_cutoff.IsUpdateValid(frontier, UpdateUserValueType::kLargest));
    EXPECT_TRUE(higher_cutoff.IsUpdateValid(frontier, UpdateUserValueType::kSmallest));

    // Zero OpId should be considered as an undefined value, not causing any errors.
    ConsensusFrontier zero_op_id{{0, 0}, HybridTime::kInvalid, HybridTime::kInvalid};
    EXPECT_TRUE(frontier.IsUpdateValid(zero_op_id, UpdateUserValueType::kLargest));
    EXPECT_TRUE(frontier.IsUpdateValid(zero_op_id, UpdateUserValueType::kSmallest));
    EXPECT_TRUE(zero_op_id.IsUpdateValid(frontier, UpdateUserValueType::kLargest));
    EXPECT_TRUE(zero_op_id.IsUpdateValid(frontier, UpdateUserValueType::kSmallest));
  }

  ConsensusFrontierPB pb;
  pb.mutable_op_id()->set_term(0);
  pb.mutable_op_id()->set_index(0);
  EXPECT_EQ(
      PbToString(pb),
      "{ op_id: 0.0 hybrid_time: <min> history_cutoff: <invalid> }");

  pb.mutable_op_id()->set_term(2);
  pb.mutable_op_id()->set_index(3);
  EXPECT_EQ(
      PbToString(pb),
      "{ op_id: 2.3 hybrid_time: <min> history_cutoff: <invalid> }");

  pb.set_hybrid_time(100000);
  EXPECT_EQ(
      PbToString(pb),
      "{ op_id: 2.3 hybrid_time: { physical: 24 logical: 1696 } history_cutoff: <invalid> }");

  pb.set_history_cutoff(200000);
  EXPECT_EQ(
        PbToString(pb),
        "{ op_id: 2.3 hybrid_time: { physical: 24 logical: 1696 } "
            "history_cutoff: { physical: 48 logical: 3392 } }");
}

}  // namespace docdb
}  // namespace yb

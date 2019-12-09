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

#ifndef YB_CONSENSUS_CONSENSUS_TYPES_H
#define YB_CONSENSUS_CONSENSUS_TYPES_H

#include "yb/common/hybrid_time.h"

#include "yb/consensus/consensus_fwd.h"

namespace yb {
namespace consensus {

// Used for a callback that sets a transaction's timestamp and starts the MVCC transaction for
// YB tables. In YB tables, we assign timestamp at the time of appending an entry to the Raft
// log, so that timestamps always keep increasing in the log, unless entries are being overwritten.
class ConsensusAppendCallback {
 public:
  virtual void HandleConsensusAppend() = 0;
  virtual ~ConsensusAppendCallback() {}
};

struct ConsensusOptions {
  std::string tablet_id;
};

} // namespace consensus
} // namespace yb

#endif // YB_CONSENSUS_CONSENSUS_TYPES_H

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

#include "yb/docdb/lock_batch.h"
#include "yb/docdb/shared_lock_manager.h"
#include "yb/util/trace.h"
#include "yb/util/debug-util.h"
#include "yb/util/tostring.h"
#include "yb/util/shared_lock.h"

namespace yb {
namespace docdb {

LockBatch::LockBatch(SharedLockManager* lock_manager, LockBatchEntries&& key_to_intent_type,
                     CoarseTimePoint deadline)
    : data_(std::move(key_to_intent_type), lock_manager) {
  if (!empty() && !lock_manager->Lock(&data_.key_to_type, deadline)) {
    data_.shared_lock_manager = nullptr;
    data_.key_to_type.clear();
    data_.status = STATUS_FORMAT(
        TryAgain, "Failed to obtain locks until deadline: $0", deadline);
  }
}

LockBatch::~LockBatch() {
  Reset();
}

void LockBatch::Reset() {
  if (!empty()) {
    VLOG(1) << "Auto-unlocking a LockBatch with " << size() << " keys";
    DCHECK_NOTNULL(data_.shared_lock_manager)->Unlock(data_.key_to_type);
    data_.key_to_type.clear();
  }
}

void LockBatch::MoveFrom(LockBatch* other) {
  Reset();
  data_ = std::move(other->data_);
  // Explicitly clear other key_to_type to avoid extra unlock when it is destructed. We use
  // key_to_type emptiness to mark that it does not hold a lock.
  other->data_.key_to_type.clear();
}


}  // namespace docdb
}  // namespace yb

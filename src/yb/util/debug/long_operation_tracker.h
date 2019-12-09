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

#ifndef YB_UTIL_DEBUG_LONG_OPERATION_TRACKER_H
#define YB_UTIL_DEBUG_LONG_OPERATION_TRACKER_H

#include <memory>

#include "yb/util/monotime.h"

namespace yb {

// Tracks long running operation.
// If it does not complete within specified duration warning is added to log.
// Warning contains stack trace of thread that created this tracker.
class LongOperationTracker {
 public:
  LongOperationTracker(const char* message, MonoDelta duration);
  ~LongOperationTracker();

  LongOperationTracker(const LongOperationTracker&) = delete;
  void operator=(const LongOperationTracker&) = delete;

  struct TrackedOperation;

 private:
  std::shared_ptr<TrackedOperation> tracked_operation_;
};

} // namespace yb

#endif // YB_UTIL_DEBUG_LONG_OPERATION_TRACKER_H

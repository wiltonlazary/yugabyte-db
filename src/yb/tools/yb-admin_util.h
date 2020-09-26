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

#ifndef YB_TOOLS_YB_ADMIN_UTIL_H
#define YB_TOOLS_YB_ADMIN_UTIL_H

#include "yb/common/entity_ids.h"

namespace yb {
namespace tools {

std::string SnapshotIdToString(const SnapshotId& snapshot_id);

SnapshotId StringToSnapshotId(const std::string& str);

}  // namespace tools
}  // namespace yb

#endif // YB_TOOLS_YB_ADMIN_UTIL_H

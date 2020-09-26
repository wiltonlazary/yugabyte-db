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

#ifndef YB_INTEGRATION_TESTS_MINI_CLUSTER_UTILS_H
#define YB_INTEGRATION_TESTS_MINI_CLUSTER_UTILS_H

#include <stddef.h>

namespace yb {

class MiniCluster;

size_t CountRunningTransactions(MiniCluster* cluster);
void AssertNoRunningTransactions(MiniCluster* cluster);
void AssertRunningTransactionsCountLessOrEqualTo(MiniCluster* cluster, size_t limit_per_tablet);

} // namespace yb

#endif // YB_INTEGRATION_TESTS_MINI_CLUSTER_UTILS_H

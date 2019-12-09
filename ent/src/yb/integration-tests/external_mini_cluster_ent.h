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

#ifndef ENT_SRC_YB_INTEGRATION_TESTS_EXTERNAL_MINI_CLUSTER_ENT_H
#define ENT_SRC_YB_INTEGRATION_TESTS_EXTERNAL_MINI_CLUSTER_ENT_H

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/rpc/rpc_fwd.h"

namespace yb {

namespace master {
class MasterBackupServiceProxy;
}  // namespace master

namespace rpc {
class SecureContext;
}

// If the cluster is configured for a single non-distributed master, return a backup proxy
// to that master. Requires that the single master is running.
std::shared_ptr<master::MasterBackupServiceProxy> master_backup_proxy(
    ExternalMiniCluster* cluster);

// Returns an RPC backup proxy to the master at 'idx'.
// Requires that the master at 'idx' is running.
std::shared_ptr<master::MasterBackupServiceProxy> master_backup_proxy(
    ExternalMiniCluster* cluster, int idx);

void StartSecure(
  std::unique_ptr<ExternalMiniCluster>* cluster,
  std::unique_ptr<rpc::SecureContext>* secure_context,
  std::unique_ptr<rpc::Messenger>* messenger,
  const std::vector<std::string>& master_flags = std::vector<std::string>());

} // namespace yb

#endif // ENT_SRC_YB_INTEGRATION_TESTS_EXTERNAL_MINI_CLUSTER_ENT_H

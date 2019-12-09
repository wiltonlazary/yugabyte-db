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

#ifndef YB_YQL_PGWRAPPER_PG_WRAPPER_TEST_BASE_H
#define YB_YQL_PGWRAPPER_PG_WRAPPER_TEST_BASE_H

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

namespace yb {
namespace pgwrapper {

class PgWrapperTestBase : public YBMiniClusterTestBase<ExternalMiniCluster> {
 protected:
  void SetUp() override;

  virtual int GetNumMasters() const { return 1; }

  virtual int GetNumTabletServers() const {
    // Test that we can start PostgreSQL servers on non-colliding ports within each tablet server.
    return 3;
  }

  virtual void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) {}

  // Tablet server to use to perform PostgreSQL operations.
  ExternalTabletServer* pg_ts = nullptr;
};

} // namespace pgwrapper
} // namespace yb

#endif // YB_YQL_PGWRAPPER_PG_WRAPPER_TEST_BASE_H

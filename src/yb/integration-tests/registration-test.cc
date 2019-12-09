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

#include <memory>
#include <string>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "yb/common/schema.h"
#include "yb/common/wire_protocol-test-util.h"
#include "yb/fs/fs_manager.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/mini_master.h"
#include "yb/master/master.h"
#include "yb/master/master.pb.h"
#include "yb/master/master-test-util.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ts_descriptor.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/curl_util.h"
#include "yb/util/faststring.h"
#include "yb/util/metrics.h"
#include "yb/util/test_util.h"
#include "yb/util/stopwatch.h"

DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(yb_num_shards_per_tserver);

METRIC_DECLARE_counter(rows_inserted);

namespace yb {

using std::vector;
using std::shared_ptr;
using master::MiniMaster;
using master::TSDescriptor;
using master::TabletLocationsPB;
using tserver::MiniTabletServer;
using client::YBTableName;

// Tests for the Tablet Server registering with the Master,
// and the master maintaining the tablet descriptor.
class RegistrationTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  RegistrationTest()
      : schema_({ ColumnSchema("c1", UINT32, /* is_nullable */ false, /* is_hash_key */ true)}, 1) {
  }

  void SetUp() override {
    // Make heartbeats faster to speed test runtime.
    FLAGS_heartbeat_interval_ms = 10;

    YBMiniClusterTestBase::SetUp();

    cluster_.reset(new MiniCluster(env_.get(), MiniClusterOptions()));
    ASSERT_OK(cluster_->Start());
  }

  void DoTearDown() override {
    cluster_->Shutdown();
  }

  void CheckTabletServersPage() {
    EasyCurl c;
    faststring buf;
    std::string addr = yb::ToString(cluster_->mini_master()->bound_http_addr());
    ASSERT_OK(c.FetchURL(strings::Substitute("http://$0/tablet-servers", addr), &buf));

    // Should include the TS UUID
    string expected_uuid =
      cluster_->mini_tablet_server(0)->server()->instance_pb().permanent_uuid();
    ASSERT_STR_CONTAINS(buf.ToString(), expected_uuid);
  }

  void CheckTabletReports(bool co_partition = false) {
    string tablet_id_1;
    string tablet_id_2;
    string table_id_1;
    FLAGS_yb_num_shards_per_tserver = 10;

    ASSERT_OK(cluster_->WaitForTabletServerCount(1));

    MiniTabletServer* ts = cluster_->mini_tablet_server(0);

    auto GetCatalogMetric = [&](CounterPrototype& prototype) -> int64_t {
      auto metrics = cluster_->mini_master()->master()->catalog_manager()->sys_catalog()
          ->tablet_peer()->shared_tablet()->GetMetricEntity();
      return prototype.Instantiate(metrics)->value();
    };
    int before_rows_inserted = GetCatalogMetric(METRIC_rows_inserted);

    // Add a tablet, make sure it reports itself.
    CreateTabletForTesting(cluster_->mini_master(),
                           YBTableName(YQL_DATABASE_CQL, "my_keyspace", "fake-table"),
                           schema_,
                           &tablet_id_1,
                           &table_id_1);

    TabletLocationsPB locs;
    ASSERT_OK(cluster_->WaitForReplicaCount(tablet_id_1, 1, &locs));
    ASSERT_EQ(1, locs.replicas_size());
    LOG(INFO) << "Tablet successfully reported on " <<
              locs.replicas(0).ts_info().permanent_uuid();

    // TODO(bogdan): why do namespaces/tables report 2 writes?
    // Check that we inserted the right number of rows for the first table:
    // - 2 for the namespace
    // - 2 for the table
    // - 4 * FLAGS_yb_num_shards_per_tserver for the tablets:
    //    CREATING, PREPARING, first heartbeat, leader election heartbeat
    int after_create_rows_inserted = GetCatalogMetric(METRIC_rows_inserted);
    int expected_rows = 2 + 2 + FLAGS_yb_num_shards_per_tserver * 4;
    EXPECT_EQ(expected_rows, after_create_rows_inserted - before_rows_inserted)
        << "Expected 2 writes for the table and 4 per each tablet";

    // Add another tablet, make sure it is reported via incremental.
    Schema schema_copy = Schema(schema_);
    if (co_partition) {
      schema_copy.SetCopartitionTableId(table_id_1);
    }

    // Record the number of rows before the new table.
    before_rows_inserted = GetCatalogMetric(METRIC_rows_inserted);
    CreateTabletForTesting(cluster_->mini_master(),
                           YBTableName(YQL_DATABASE_CQL, "my_keyspace", "fake-table2"),
                           schema_copy,
                           &tablet_id_2);
    // Sleep for enough to make sure the TS has plenty of time to re-heartbeat.
    SleepFor(MonoDelta::FromSeconds(2));
    after_create_rows_inserted = GetCatalogMetric(METRIC_rows_inserted);
    // For a normal table, we expect 4 writes per tablet.
    // For a copartitioned table, we expect just 1 write per tablet.
    expected_rows = 2 + FLAGS_yb_num_shards_per_tserver * (co_partition ? 1 : 4);
    EXPECT_EQ(expected_rows, after_create_rows_inserted - before_rows_inserted);
    ASSERT_OK(cluster_->WaitForReplicaCount(tablet_id_2, 1, &locs));

    if (co_partition) {
      ASSERT_EQ(tablet_id_1, tablet_id_2);
    }

    // Shut down the whole system, bring it back up, and make sure the tablets
    // are reported.
    ts->Shutdown();
    ASSERT_OK(cluster_->mini_master()->Restart());
    ASSERT_OK(ts->Start());
    ASSERT_OK(cluster_->WaitForTabletServerCount(1));

    ASSERT_OK(cluster_->WaitForReplicaCount(tablet_id_1, 1, &locs));
    ASSERT_OK(cluster_->WaitForReplicaCount(tablet_id_2, 1, &locs));
    // Sleep for enough to make sure the TS has plenty of time to re-heartbeat.
    SleepFor(MonoDelta::FromSeconds(2));

    // After restart, check that the tablet reports produced the expected number of
    // writes to the catalog table:
    // - If no copartitions, then two updates per tablet, since both replicas should have increased
    //   their term on restart.
    // - If copartitioned, then one update per tablet.
    EXPECT_EQ(
        (co_partition ? 1 : 2) * FLAGS_yb_num_shards_per_tserver,
        GetCatalogMetric(METRIC_rows_inserted));

    // If we restart just the master, it should not write any data to the catalog, since the
    // tablets themselves are not changing term, etc.
    ASSERT_OK(cluster_->mini_master()->Restart());
    // Sleep for enough to make sure the TS has plenty of time to re-heartbeat.
    ASSERT_OK(cluster_->WaitForTabletServerCount(1));
    SleepFor(MonoDelta::FromSeconds(2));
    EXPECT_EQ(0, GetCatalogMetric(METRIC_rows_inserted));

    // TODO: KUDU-870: once the master supports detecting failed/lost replicas,
    // we should add a test case here which removes or corrupts metadata, restarts
    // the TS, and verifies that the master notices the issue.
  }

 protected:
  Schema schema_;
};

TEST_F(RegistrationTest, TestTSRegisters) {
  DontVerifyClusterBeforeNextTearDown();
  // Wait for the TS to register.
  vector<shared_ptr<TSDescriptor> > descs;
  ASSERT_OK(cluster_->WaitForTabletServerCount(1, &descs));
  ASSERT_EQ(1, descs.size());

  // Verify that the registration is sane.
  master::TSRegistrationPB reg = descs[0]->GetRegistration();
  {
    SCOPED_TRACE(reg.ShortDebugString());
    ASSERT_EQ(reg.ShortDebugString().find("0.0.0.0"), string::npos)
      << "Should not include wildcards in registration";
  }

  ASSERT_NO_FATALS(CheckTabletServersPage());

  // Restart the master, so it loses the descriptor, and ensure that the
  // heartbeater thread handles re-registering.
  ASSERT_OK(cluster_->mini_master()->Restart());

  ASSERT_OK(cluster_->WaitForTabletServerCount(1));

  // TODO: when the instance ID / sequence number stuff is implemented,
  // restart the TS and ensure that it re-registers with the newer sequence
  // number.
}

// Test starting multiple tablet servers and ensuring they both register with the master.
TEST_F(RegistrationTest, TestMultipleTS) {
  DontVerifyClusterBeforeNextTearDown();
  ASSERT_OK(cluster_->WaitForTabletServerCount(1));
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(2));
}

// TODO: this doesn't belong under "RegistrationTest" - rename this file
// to something more appropriate - doesn't seem worth having separate
// whole test suites for registration, tablet reports, etc.
TEST_F(RegistrationTest, TestTabletReports) {
  CheckTabletReports();
}

TEST_F(RegistrationTest, TestCopartitionedTables) {
  CheckTabletReports(true);
}

} // namespace yb

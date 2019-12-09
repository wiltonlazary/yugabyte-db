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

#include <fstream>
#include <memory>
#include <thread>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/table_creator.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/fs/fs_manager.h"
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/master.h"
#include "yb/master/master.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/master-test-util.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_test_util.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/hdr_histogram.h"
#include "yb/util/metrics.h"
#include "yb/util/spinlock_profiling.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"

using yb::client::YBClient;
using yb::client::YBClientBuilder;
using yb::client::YBColumnSchema;
using yb::client::YBSchema;
using yb::client::YBSchemaBuilder;
using yb::client::YBTableCreator;
using yb::client::YBTableName;
using yb::itest::CreateTabletServerMap;
using yb::itest::TabletServerMap;
using yb::master::MasterServiceProxy;
using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;

DECLARE_int32(heartbeat_interval_ms);
DECLARE_bool(log_preallocate_segments);
DECLARE_bool(enable_remote_bootstrap);
DECLARE_int32(tserver_unresponsive_timeout_ms);
DECLARE_int32(max_create_tablets_per_ts);

DEFINE_int32(num_test_tablets, 60, "Number of tablets for stress test");
DEFINE_int32(benchmark_runtime_secs, 5, "Number of seconds to run the benchmark");
DEFINE_int32(benchmark_num_threads, 16, "Number of threads to run the benchmark");
DEFINE_int32(benchmark_num_tablets, 60, "Number of tablets to create");

METRIC_DECLARE_histogram(handler_latency_yb_master_MasterService_GetTableLocations);

using std::string;
using std::vector;
using std::thread;
using std::unique_ptr;
using strings::Substitute;

namespace yb {

class CreateTableStressTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  CreateTableStressTest() {
    YBSchemaBuilder b;
    b.AddColumn("key")->Type(INT32)->NotNull()->HashPrimaryKey();
    b.AddColumn("v1")->Type(INT64)->NotNull();
    b.AddColumn("v2")->Type(STRING)->NotNull();
    CHECK_OK(b.Build(&schema_));
  }

  void SetUp() override {
    // Make heartbeats faster to speed test runtime.
    FLAGS_heartbeat_interval_ms = 10;

    // Don't preallocate log segments, since we're creating thousands
    // of tablets here. If each preallocates 64M or so, we use
    // a ton of disk space in this test, and it fails on normal
    // sized /tmp dirs.
    // TODO: once we collapse multiple tablets into shared WAL files,
    // this won't be necessary.
    FLAGS_log_preallocate_segments = false;

    // Workaround KUDU-941: without this, it's likely that while shutting
    // down tablets, they'll get resuscitated by their existing leaders.
    FLAGS_enable_remote_bootstrap = false;

    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());

    client_ = ASSERT_RESULT(YBClientBuilder()
        .add_master_server_addr(cluster_->mini_master()->bound_rpc_addr_str())
        .Build());

    messenger_ = ASSERT_RESULT(
        MessengerBuilder("stress-test-msgr").set_num_reactors(1).Build());
    rpc::ProxyCache proxy_cache(messenger_.get());
    master_proxy_.reset(new MasterServiceProxy(&proxy_cache,
                                               cluster_->mini_master()->bound_rpc_addr()));
    ASSERT_OK(CreateTabletServerMap(master_proxy_.get(), &proxy_cache, &ts_map_));
  }

  void DoTearDown() override {
    messenger_->Shutdown();
    client_.reset();
    cluster_->Shutdown();
    ts_map_.clear();
  }

  void CreateBigTable(const YBTableName& table_name, int num_tablets);

 protected:
  std::unique_ptr<YBClient> client_;
  YBSchema schema_;
  std::unique_ptr<Messenger> messenger_;
  gscoped_ptr<MasterServiceProxy> master_proxy_;
  TabletServerMap ts_map_;
};

void CreateTableStressTest::CreateBigTable(const YBTableName& table_name, int num_tablets) {
  ASSERT_OK(client_->CreateNamespaceIfNotExists(table_name.namespace_name(),
                                                table_name.namespace_type()));
  gscoped_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
  ASSERT_OK(table_creator->table_name(table_name)
            .schema(&schema_)
            .num_tablets(num_tablets)
            .wait(false)
            .Create());
}

TEST_F(CreateTableStressTest, GetTableLocationsBenchmark) {
  FLAGS_max_create_tablets_per_ts = FLAGS_benchmark_num_tablets;
  DontVerifyClusterBeforeNextTearDown();
  YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 1. Creating big table "
            << table_name.ToString() << " ...";
  LOG_TIMING(INFO, "creating big table") {
    ASSERT_NO_FATALS(CreateBigTable(table_name, FLAGS_benchmark_num_tablets));
  }

  // Make sure the table is completely created before we start poking.
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 2. Waiting for creation of big table "
            << table_name.ToString() << " to complete...";
  master::GetTableLocationsResponsePB create_resp;
  LOG_TIMING(INFO, "waiting for creation of big table") {
    ASSERT_OK(WaitForRunningTabletCount(cluster_->mini_master(), table_name,
                                       FLAGS_benchmark_num_tablets, &create_resp));
  }
  // Sleep for a while to let all TS heartbeat to master.
  SleepFor(MonoDelta::FromSeconds(10));
  const int kNumThreads = FLAGS_benchmark_num_threads;
  const auto kRuntime = MonoDelta::FromSeconds(FLAGS_benchmark_runtime_secs);

  // Make one proxy per thread, so each thread gets its own messenger and
  // reactor. If there were only one messenger, then only one reactor thread
  // would be used for the connection to the master, so this benchmark would
  // probably be testing the serialization and network code rather than the
  // master GTL code.
  vector<rpc::AutoShutdownMessengerHolder> messengers;
  vector<unique_ptr<MasterServiceProxy>> proxies;
  vector<unique_ptr<rpc::ProxyCache>> caches;
  messengers.reserve(kNumThreads);
  proxies.reserve(kNumThreads);
  caches.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    messengers.emplace_back(
        ASSERT_RESULT(MessengerBuilder("Client").set_num_reactors(1).Build()).release());
    caches.emplace_back(new rpc::ProxyCache(messengers.back().get()));
    proxies.emplace_back(new MasterServiceProxy(
          caches.back().get(), cluster_->mini_master()->bound_rpc_addr()));
  }

  std::atomic<bool> stop { false };
  vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back([&, i]() {
        while (!stop) {
          master::GetTableLocationsRequestPB req;
          master::GetTableLocationsResponsePB resp;
          RpcController controller;
          // Silence errors.
          controller.set_timeout(MonoDelta::FromSeconds(10));
          table_name.SetIntoTableIdentifierPB(req.mutable_table());
          req.set_max_returned_locations(1000);
          CHECK_OK(proxies[i]->GetTableLocations(req, &resp, &controller));
          CHECK_EQ(resp.tablet_locations_size(), FLAGS_benchmark_num_tablets);
        }
      });
  }

  std::stringstream profile;
  StartSynchronizationProfiling();
  SleepFor(kRuntime);
  stop = true;
  for (auto& t : threads) {
    t.join();
  }
  StopSynchronizationProfiling();
  int64_t discarded_samples = 0;
  FlushSynchronizationProfile(&profile, &discarded_samples);

  const auto& ent = cluster_->mini_master()->master()->metric_entity();
  auto hist = METRIC_handler_latency_yb_master_MasterService_GetTableLocations
      .Instantiate(ent);

  cluster_->Shutdown();

  LOG(INFO) << "LOCK PROFILE\n" << profile.str();
  LOG(INFO) << "BENCHMARK HISTOGRAM:";
  hist->histogram()->DumpHumanReadable(&LOG(INFO));
}

TEST_F(CreateTableStressTest, CreateAndDeleteBigTable) {
  DontVerifyClusterBeforeNextTearDown();
  if (!AllowSlowTests()) {
    LOG(INFO) << "Skipping slow test";
    return;
  }
  YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  ASSERT_NO_FATALS(CreateBigTable(table_name, FLAGS_num_test_tablets));
  master::GetTableLocationsResponsePB resp;
  ASSERT_OK(WaitForRunningTabletCount(cluster_->mini_master(), table_name,
                                      FLAGS_num_test_tablets, &resp));
  LOG(INFO) << "Created table successfully!";
  // Use std::cout instead of log, since these responses are large and log
  // messages have a max size.
  std::cout << "Response:\n" << resp.DebugString();
  std::cout << "CatalogManager state:\n";
  cluster_->mini_master()->master()->catalog_manager()->DumpState(&std::cerr);

  LOG(INFO) << "Deleting table...";
  ASSERT_OK(client_->DeleteTable(table_name));

  // The actual removal of the tablets is asynchronous, so we loop for a bit
  // waiting for them to get removed.
  LOG(INFO) << "Waiting for tablets to be removed";
  vector<string> tablet_ids;
  for (int i = 0; i < 1000; i++) {
    ASSERT_OK(itest::ListRunningTabletIds(ts_map_.begin()->second.get(),
                                          MonoDelta::FromSeconds(10),
                                          &tablet_ids));
    if (tablet_ids.empty()) break;
    SleepFor(MonoDelta::FromMilliseconds(100));
  }
  ASSERT_TRUE(tablet_ids.empty()) << "Tablets remained: " << tablet_ids;
}

TEST_F(CreateTableStressTest, RestartMasterDuringCreation) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "Skipping slow test";
    DontVerifyClusterBeforeNextTearDown();
    return;
  }

  YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  ASSERT_NO_FATALS(CreateBigTable(table_name, FLAGS_num_test_tablets));

  for (int i = 0; i < 3; i++) {
    SleepFor(MonoDelta::FromMicroseconds(500));
    LOG(INFO) << "Restarting master...";
    ASSERT_OK(cluster_->mini_master()->Restart());
    ASSERT_OK(cluster_->mini_master()->master()->
        WaitUntilCatalogManagerIsLeaderAndReadyForTests());
    LOG(INFO) << "Master restarted.";
  }

  master::GetTableLocationsResponsePB resp;
  Status s = WaitForRunningTabletCount(cluster_->mini_master(), table_name,
                                       FLAGS_num_test_tablets, &resp);
  if (!s.ok()) {
    cluster_->mini_master()->master()->catalog_manager()->DumpState(&std::cerr);
    CHECK_OK(s);
  }
}

TEST_F(CreateTableStressTest, TestGetTableLocationsOptions) {
  DontVerifyClusterBeforeNextTearDown();
  if (!AllowSlowTests()) {
    LOG(INFO) << "Skipping slow test";
    return;
  }

  YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 1. Creating big table "
            << table_name.ToString() << " ...";
  LOG_TIMING(INFO, "creating big table") {
    ASSERT_NO_FATALS(CreateBigTable(table_name, FLAGS_num_test_tablets));
  }

  master::GetTableLocationsRequestPB req;
  master::GetTableLocationsResponsePB resp;

  // Make sure the table is completely created before we start poking.
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 2. Waiting for creation of big table "
            << table_name.ToString() << " to complete...";
  LOG_TIMING(INFO, "waiting for creation of big table") {
    ASSERT_OK(WaitForRunningTabletCount(cluster_->mini_master(), table_name,
                                       FLAGS_num_test_tablets, &resp));
  }

  // Test asking for 0 tablets, should fail
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 3. Asking for zero tablets...";
  LOG_TIMING(INFO, "asking for zero tablets") {
    req.Clear();
    resp.Clear();
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(0);
    Status s = cluster_->mini_master()->master()->catalog_manager()->GetTableLocations(&req, &resp);
    ASSERT_STR_CONTAINS(s.ToString(), "must be greater than 0");
  }

  // Ask for one, get one, verify
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 4. Asking for one tablet...";
  LOG_TIMING(INFO, "asking for one tablet") {
    req.Clear();
    resp.Clear();
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(1);
    ASSERT_OK(cluster_->mini_master()->master()->catalog_manager()->GetTableLocations(&req, &resp));
    ASSERT_EQ(resp.tablet_locations_size(), 1);
    // empty since it's the first
    ASSERT_EQ(resp.tablet_locations(0).partition().partition_key_start(), "");
    ASSERT_EQ(resp.tablet_locations(0).partition().partition_key_end(), string("\x80\0\0\1", 4));
  }

  int half_tablets = FLAGS_num_test_tablets / 2;
  // Ask for half of them, get that number back
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 5. Asking for half the tablets...";
  LOG_TIMING(INFO, "asking for half the tablets") {
    req.Clear();
    resp.Clear();
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(half_tablets);
    ASSERT_OK(cluster_->mini_master()->master()->catalog_manager()->GetTableLocations(&req, &resp));
    ASSERT_EQ(half_tablets, resp.tablet_locations_size());
  }

  // Ask for all of them, get that number back
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 6. Asking for all the tablets...";
  LOG_TIMING(INFO, "asking for all the tablets") {
    req.Clear();
    resp.Clear();
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(FLAGS_num_test_tablets);
    ASSERT_OK(cluster_->mini_master()->master()->catalog_manager()->GetTableLocations(&req, &resp));
    ASSERT_EQ(FLAGS_num_test_tablets, resp.tablet_locations_size());
  }

  LOG(INFO) << "========================================================";
  LOG(INFO) << "Tables and tablets:";
  LOG(INFO) << "========================================================";
  std::vector<scoped_refptr<master::TableInfo> > tables;
  cluster_->mini_master()->master()->catalog_manager()->GetAllTables(&tables);
  for (const scoped_refptr<master::TableInfo>& table_info : tables) {
    LOG(INFO) << "Table: " << table_info->ToString();
    std::vector<scoped_refptr<master::TabletInfo> > tablets;
    table_info->GetAllTablets(&tablets);
    for (const scoped_refptr<master::TabletInfo>& tablet_info : tablets) {
      auto l_tablet = tablet_info->LockForRead();
      const master::SysTabletsEntryPB& metadata = l_tablet->data().pb;
      LOG(INFO) << "  Tablet: " << tablet_info->ToString()
                << " { start_key: "
                << ((metadata.partition().has_partition_key_start())
                    ? metadata.partition().partition_key_start() : "<< none >>")
                << ", end_key: "
                << ((metadata.partition().has_partition_key_end())
                    ? metadata.partition().partition_key_end() : "<< none >>")
                << ", running = " << tablet_info->metadata().state().is_running() << " }";
    }
    ASSERT_EQ(FLAGS_num_test_tablets, tablets.size());
  }
  LOG(INFO) << "========================================================";

  // Get a single tablet in the middle, make sure we get that one back

  gscoped_ptr<YBPartialRow> row(schema_.NewRow());
  ASSERT_OK(row->SetInt32(0, half_tablets - 1));
  string start_key_middle;
  ASSERT_OK(row->EncodeRowKey(&start_key_middle));

  LOG(INFO) << "Start key middle: " << start_key_middle;
  LOG(INFO) << CURRENT_TEST_NAME() << ": Step 7. Asking for single middle tablet...";
  LOG_TIMING(INFO, "asking for single middle tablet") {
    req.Clear();
    resp.Clear();
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(1);
    req.set_partition_key_start(start_key_middle);
    ASSERT_OK(cluster_->mini_master()->master()->catalog_manager()->GetTableLocations(&req, &resp));
    ASSERT_EQ(1, resp.tablet_locations_size()) << "Response: [" << resp.DebugString() << "]";
    ASSERT_EQ(start_key_middle, resp.tablet_locations(0).partition().partition_key_start());
  }
}

// Creates tables and reloads on-disk metadata concurrently to test for races
// between the two operations.
TEST_F(CreateTableStressTest, TestConcurrentCreateTableAndReloadMetadata) {
  AtomicBool stop(false);

  // Since this test constantly invokes VisitSysCatalog() which is the function
  // that runs after a new leader gets elected, during that period the leader rejects
  // tablet server heart-beats (because it holds the leader_lock_), and this leads
  // the master to mistakenly think that the tablet servers are dead. To avoid this
  // increase the TS unresponsive timeout so that the leader correctly thinks that
  // they are alive.
  SetAtomicFlag(5 * 60 * 1000, &FLAGS_tserver_unresponsive_timeout_ms);

  thread reload_metadata_thread([&]() {
    while (!stop.Load()) {
      CHECK_OK(cluster_->mini_master()->master()->catalog_manager()->VisitSysCatalog(0));
      // Give table creation a chance to run.
      SleepFor(MonoDelta::FromMilliseconds(yb::NonTsanVsTsan(1, 5)));
    }
  });

  for (int num_tables_created = 0; num_tables_created < 20;) {
    YBTableName table_name(
        YQL_DATABASE_CQL, "my_keyspace", Substitute("test-$0", num_tables_created));
    LOG(INFO) << "Creating table " << table_name.ToString();
    Status s = client_->CreateNamespaceIfNotExists(table_name.namespace_name(),
                                                   table_name.namespace_type());
    if (s.ok()) {
      unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
      s = table_creator->table_name(table_name)
          .schema(&schema_)
          .hash_schema(YBHashSchema::kMultiColumnHash)
          .set_range_partition_columns({ "key" })
          .num_tablets(1)
          .wait(false)
          .Create();
    }
    if (s.IsServiceUnavailable()) {
      // The master was busy reloading its metadata. Try again.
      //
      // This is a purely synthetic case. In real life, it only manifests at
      // startup (single master) or during leader failover (multiple masters).
      // In the latter case, the client will transparently retry to another
      // master. That won't happen here as we've only got one master, so we
      // must handle retrying ourselves.
      continue;
    }
    ASSERT_OK(s);
    num_tables_created++;
    LOG(INFO) << "Total created: " << num_tables_created;
  }
  stop.Store(true);
  reload_metadata_thread.join();
}

}  // namespace yb

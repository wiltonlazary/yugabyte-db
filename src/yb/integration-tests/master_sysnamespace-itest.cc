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

#include <yb/master/master_defaults.h>
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/master.h"
#include "yb/master/master.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/rpc/messenger.h"
#include "yb/util/test_util.h"

namespace yb {
namespace master {

class MasterSysNamespaceTest : public YBTest {
 public:
  MasterSysNamespaceTest() {}

  ~MasterSysNamespaceTest() {}

 protected:
  void SetUp() override {
    YBTest::SetUp();
    MiniClusterOptions opts;
    opts.num_masters = 3;
    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());
    rpc::MessengerBuilder bld("Client");
    client_messenger_ = ASSERT_RESULT(bld.Build());
    rpc::ProxyCache proxy_cache(client_messenger_.get());
    proxy_.reset(new MasterServiceProxy(&proxy_cache,
                                        cluster_->leader_mini_master()->bound_rpc_addr()));
  }

  void TearDown() override {
    client_messenger_->Shutdown();
    if (cluster_) {
      cluster_->Shutdown();
      cluster_.reset();
    }

    proxy_.reset();
    YBTest::TearDown();
  }

  void VerifyTabletLocations(const TabletLocationsPB& locs_pb) {
    ASSERT_FALSE(locs_pb.stale());
    ASSERT_EQ(3, locs_pb.replicas_size());
    for (const TabletLocationsPB::ReplicaPB& replica : locs_pb.replicas()) {
      if (replica.role() == consensus::RaftPeerPB::LEADER) {
        ASSERT_EQ(cluster_->leader_mini_master()->bound_rpc_addr().host(),
                  replica.ts_info().private_rpc_addresses(0).host());
        ASSERT_EQ(cluster_->leader_mini_master()->bound_rpc_addr().port(),
                  replica.ts_info().private_rpc_addresses(0).port());
        ASSERT_EQ(cluster_->leader_mini_master()->permanent_uuid(),
                  replica.ts_info().permanent_uuid());
      } else {
        // Search for appropriate master.
        int i;
        for (i = 0; i < cluster_->num_masters(); i++) {
          if (cluster_->mini_master(i)->permanent_uuid() == replica.ts_info().permanent_uuid()) {
            ASSERT_EQ(cluster_->mini_master(i)->bound_rpc_addr().host(),
                      replica.ts_info().private_rpc_addresses(0).host());
            ASSERT_EQ(cluster_->mini_master(i)->bound_rpc_addr().port(),
                      replica.ts_info().private_rpc_addresses(0).port());
            ASSERT_EQ(consensus::RaftPeerPB::FOLLOWER, replica.role());
            break;
          }
        }
        ASSERT_FALSE(i == cluster_->num_masters());
      }
    }
  }

  void ValidateColumn(ColumnSchemaPB col_schema, string name, bool is_key, DataType type) {
    ASSERT_EQ(name, col_schema.name());
    ASSERT_EQ(is_key, col_schema.is_key());
    ASSERT_EQ(type, col_schema.type().main());
  }

  std::unique_ptr<MiniCluster> cluster_;
  std::unique_ptr<MasterServiceProxy> proxy_;
  std::unique_ptr<rpc::Messenger> client_messenger_;
};

TEST_F(MasterSysNamespaceTest, TestSysNamespace) {
  // Test GetTableLocations.
  GetTableLocationsRequestPB req;
  GetTableLocationsResponsePB resp;
  TableIdentifierPB* table_identifier = req.mutable_table();
  table_identifier->set_table_name(master::kSystemPeersTableName);
  NamespaceIdentifierPB* namespace_identifier = table_identifier->mutable_namespace_();
  namespace_identifier->set_name(master::kSystemNamespaceName);
  namespace_identifier->set_id(master::kSystemNamespaceId);
  std::unique_ptr<rpc::RpcController> controller(new rpc::RpcController());
  proxy_->GetTableLocations(req, &resp, controller.get());

  ASSERT_FALSE(resp.has_error());
  ASSERT_EQ(TableType::YQL_TABLE_TYPE, resp.table_type());
  ASSERT_EQ(1, resp.tablet_locations_size());
  VerifyTabletLocations(resp.tablet_locations(0));

  // Test GetTabletLocations.
  GetTabletLocationsRequestPB tablet_req;
  GetTabletLocationsResponsePB tablet_resp;
  tablet_req.add_tablet_ids(resp.tablet_locations(0).tablet_id());
  controller->Reset();
  proxy_->GetTabletLocations(tablet_req, &tablet_resp, controller.get());
  ASSERT_FALSE(tablet_resp.has_error());
  ASSERT_EQ(1, tablet_resp.tablet_locations_size());
  VerifyTabletLocations(tablet_resp.tablet_locations(0));

  // Test GetTableSchema.
  GetTableSchemaRequestPB schema_req;
  GetTableSchemaResponsePB schema_resp;
  controller->Reset();
  *schema_req.mutable_table() = *table_identifier;
  proxy_->GetTableSchema(schema_req, &schema_resp, controller.get());
  ASSERT_FALSE(schema_resp.has_error());
  ASSERT_TRUE(schema_resp.create_table_done());

  // Validate schema.
  SchemaPB schema_pb = schema_resp.schema();
  ASSERT_EQ(9, schema_pb.columns_size());
  ValidateColumn(schema_pb.columns(0), "peer", /* is_key */ true, DataType::INET);
  ValidateColumn(schema_pb.columns(1), "data_center", /* is_key */ false, DataType::STRING);
  ValidateColumn(schema_pb.columns(2), "host_id", /* is_key */ false, DataType::UUID);
  ValidateColumn(schema_pb.columns(3), "preferred_ip", /* is_key */ false, DataType::INET);
  ValidateColumn(schema_pb.columns(4), "rack", /* is_key */ false, DataType::STRING);
  ValidateColumn(schema_pb.columns(5), "release_version", /* is_key */ false, DataType::STRING);
  ValidateColumn(schema_pb.columns(6), "rpc_address", /* is_key */ false, DataType::INET);
  ValidateColumn(schema_pb.columns(7), "schema_version", /* is_key */ false, DataType::UUID);
  ValidateColumn(schema_pb.columns(8), "tokens", /* is_key */ false, DataType::SET);
}

} // namespace master
} // namespace yb

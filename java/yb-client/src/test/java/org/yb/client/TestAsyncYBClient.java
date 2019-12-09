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
package org.yb.client;

import com.google.common.base.Charsets;
import com.google.protobuf.ByteString;
import com.stumbleupon.async.Deferred;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.Common;
import org.yb.consensus.Metadata;
import org.yb.master.Master;

import static org.yb.AssertionWrappers.*;

import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestAsyncYBClient extends BaseYBClientTest {

  private static final String TABLE_NAME =
      TestAsyncYBClient.class.getName() + "-" + System.currentTimeMillis();
  private static YBTable table;

  @Override
  protected void afterStartingMiniCluster() throws Exception {
    super.afterStartingMiniCluster();
    CreateTableOptions options = new CreateTableOptions();
    table = createTable(TABLE_NAME, hashKeySchema, options);
  }

  @Test
  public void testBadHostnames() throws Exception {
    String badHostname = "some-unknown-host-hopefully";

    // Test that a bad hostname for the master makes us error out quickly.
    AsyncYBClient invalidClient = new AsyncYBClient.AsyncYBClientBuilder(badHostname).build();
    try {
      invalidClient.listTabletServers().join(1000);
      fail("This should have failed quickly");
    } catch (Exception ex) {
      assertTrue(ex instanceof NonRecoverableException);
      assertTrue(ex.getMessage().contains(badHostname));
    }

    Master.GetTableLocationsResponsePB.Builder builder =
        Master.GetTableLocationsResponsePB.newBuilder();

    // Builder three bad locations.
    Master.TabletLocationsPB.Builder tabletPb = Master.TabletLocationsPB.newBuilder();
    for (int i = 0; i < 3; i++) {
      Common.PartitionPB.Builder partition = Common.PartitionPB.newBuilder();
      partition.setPartitionKeyStart(ByteString.copyFrom("a" + i, Charsets.UTF_8.name()));
      partition.setPartitionKeyEnd(ByteString.copyFrom("b" + i, Charsets.UTF_8.name()));
      tabletPb.setPartition(partition);
      tabletPb.setStale(false);
      tabletPb.setTabletId(ByteString.copyFromUtf8("some id " + i));
      Master.TSInfoPB.Builder tsInfoBuilder = Master.TSInfoPB.newBuilder();
      Common.HostPortPB.Builder hostBuilder = Common.HostPortPB.newBuilder();
      hostBuilder.setHost(badHostname + i);
      hostBuilder.setPort(i);
      tsInfoBuilder.addPrivateRpcAddresses(hostBuilder);
      tsInfoBuilder.setPermanentUuid(ByteString.copyFromUtf8("some uuid"));
      Master.TabletLocationsPB.ReplicaPB.Builder replicaBuilder =
          Master.TabletLocationsPB.ReplicaPB.newBuilder();
      replicaBuilder.setTsInfo(tsInfoBuilder);
      replicaBuilder.setRole(Metadata.RaftPeerPB.Role.FOLLOWER);
      tabletPb.addReplicas(replicaBuilder);
      builder.addTabletLocations(tabletPb);
    }

    // Test that a tablet full of unreachable replicas won't make us retry.
    try {
      YBTable badTable = new YBTable(client, "Invalid table name",
          "Invalid table ID", null, null);
      client.discoverTablets(badTable, builder.build());
      fail("This should have failed quickly");
    } catch (Exception ex) {
      assertTrue(ex instanceof NonRecoverableException);
      assertTrue(ex.getMessage().contains(badHostname));
    }
  }
}

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

#ifndef YB_MASTER_SYS_CATALOG_TEST_BASE_H_
#define YB_MASTER_SYS_CATALOG_TEST_BASE_H_

#include <gtest/gtest.h>

#include "yb/common/wire_protocol.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"
#include "yb/server/rpc_server.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"
#include "yb/rpc/messenger.h"
#include "yb/common/common.pb.h"

using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;

namespace yb {
namespace master {

class SysCatalogTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();

    // Start master with the create flag on.
    mini_master_.reset(
        new MiniMaster(Env::Default(), GetTestPath("Master"), AllocateFreePort(),
                       AllocateFreePort(), 0));
    ASSERT_OK(mini_master_->Start());
    master_ = mini_master_->master();
    ASSERT_OK(master_->WaitUntilCatalogManagerIsLeaderAndReadyForTests());

    // Create a client proxy to it.
    MessengerBuilder bld("Client");
    client_messenger_ = ASSERT_RESULT(bld.Build());
    rpc::ProxyCache proxy_cache(client_messenger_.get());
    proxy_.reset(new MasterServiceProxy(&proxy_cache, mini_master_->bound_rpc_addr()));
  }

  void TearDown() override {
    client_messenger_->Shutdown();
    mini_master_->Shutdown();
    YBTest::TearDown();
  }

  std::unique_ptr<Messenger> client_messenger_;
  gscoped_ptr<MiniMaster> mini_master_;
  Master* master_;
  gscoped_ptr<MasterServiceProxy> proxy_;
};

const int64_t kLeaderTerm = 1;

static bool PbEquals(const google::protobuf::Message& a, const google::protobuf::Message& b) {
  return a.DebugString() == b.DebugString();
}

template<class C>
static bool MetadatasEqual(C* ti_a, C* ti_b) {
  auto l_a = ti_a->LockForRead();
  auto l_b = ti_b->LockForRead();
  return PbEquals(l_a->data().pb, l_b->data().pb);
}

} // namespace master
} // namespace yb

#endif // YB_MASTER_SYS_CATALOG_TEST_BASE_H_

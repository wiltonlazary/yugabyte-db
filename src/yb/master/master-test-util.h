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

#ifndef YB_MASTER_MASTER_TEST_UTIL_H
#define YB_MASTER_MASTER_TEST_UTIL_H

#include <algorithm>
#include <string>

#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"
#include "yb/master/master.h"
#include "yb/master/master.pb.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"
#include "yb/client/yb_table_name.h"

namespace yb {
namespace master {

Status WaitForRunningTabletCount(MiniMaster* mini_master,
                                 const client::YBTableName& table_name,
                                 int expected_count,
                                 GetTableLocationsResponsePB* resp) {
  int wait_time = 1000;

  SCOPED_LOG_TIMING(INFO, strings::Substitute("waiting for tablet count of $0", expected_count));
  while (true) {
    GetTableLocationsRequestPB req;
    resp->Clear();
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(expected_count);
    RETURN_NOT_OK(mini_master->master()->catalog_manager()->GetTableLocations(&req, resp));
    if (resp->tablet_locations_size() >= expected_count) {
      bool is_stale = false;
      for (const TabletLocationsPB& loc : resp->tablet_locations()) {
        is_stale |= loc.stale();
      }

      if (!is_stale) {
        return Status::OK();
      }
    }

    LOG(INFO) << "Waiting for " << expected_count << " tablets for table "
              << table_name.ToString() << ". So far we have " << resp->tablet_locations_size();

    SleepFor(MonoDelta::FromMicroseconds(wait_time));
    wait_time = std::min(wait_time * 5 / 4, 1000000);
  }

  // Unreachable.
  LOG(FATAL) << "Reached unreachable section";
  return STATUS(RuntimeError, "Unreachable statement"); // Suppress compiler warnings.
}

void CreateTabletForTesting(MiniMaster* mini_master,
                            const client::YBTableName& table_name,
                            const Schema& schema,
                            string* tablet_id,
                            string* table_id = nullptr) {
  {
    CreateNamespaceRequestPB req;
    CreateNamespaceResponsePB resp;
    req.set_name(table_name.resolved_namespace_name());

    const Status s = mini_master->master()->catalog_manager()->CreateNamespace(
        &req, &resp,  /* rpc::RpcContext* */ nullptr);
    ASSERT_TRUE(s.ok() || s.IsAlreadyPresent()) << " status=" << s.ToString();
  }
  {
    CreateTableRequestPB req;
    CreateTableResponsePB resp;

    req.set_name(table_name.table_name());
    req.mutable_namespace_()->set_name(table_name.resolved_namespace_name());

    SchemaToPB(schema, req.mutable_schema());
    ASSERT_OK(mini_master->master()->catalog_manager()->CreateTable(
        &req, &resp, /* rpc::RpcContext* */ nullptr));
  }

  int wait_time = 1000;
  bool is_table_created = false;
  for (int i = 0; i < 80; ++i) {
    IsCreateTableDoneRequestPB req;
    IsCreateTableDoneResponsePB resp;

    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    ASSERT_OK(mini_master->master()->catalog_manager()->IsCreateTableDone(&req, &resp));
    if (resp.done()) {
      is_table_created = true;
      break;
    }

    VLOG(1) << "Waiting for table '" << table_name.ToString() << "'to be created";

    SleepFor(MonoDelta::FromMicroseconds(wait_time));
    wait_time = std::min(wait_time * 5 / 4, 1000000);
  }
  ASSERT_TRUE(is_table_created);

  {
    GetTableSchemaRequestPB req;
    GetTableSchemaResponsePB resp;
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    ASSERT_OK(mini_master->master()->catalog_manager()->GetTableSchema(&req, &resp));
    ASSERT_TRUE(resp.create_table_done());
    if (table_id != nullptr) {
      *table_id = resp.identifier().table_id();
    }
  }

  GetTableLocationsResponsePB resp;
  ASSERT_OK(WaitForRunningTabletCount(mini_master, table_name, 1, &resp));
  *tablet_id = resp.tablet_locations(0).tablet_id();
  LOG(INFO) << "Got tablet " << *tablet_id << " for table " << table_name.ToString();
}

} // namespace master
} // namespace yb

#endif /* YB_MASTER_MASTER_TEST_UTIL_H */

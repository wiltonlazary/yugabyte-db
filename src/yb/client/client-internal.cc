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

#include "yb/client/client-internal.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <fstream>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/client/meta_cache.h"
#include "yb/client/table.h"

#include "yb/common/index.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/util.h"
#include "yb/gutil/sysinfo.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_rpc.h"
#include "yb/master/master_util.h"
#include "yb/master/master.pb.h"
#include "yb/master/master.proxy.h"
#include "yb/yql/redis/redisserver/redis_constants.h"
#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/messenger.h"
#include "yb/tserver/tserver_flags.h"
#include "yb/util/net/dns_resolver.h"
#include "yb/util/curl_util.h"
#include "yb/util/flags.h"
#include "yb/util/flag_tags.h"
#include "yb/util/net/net_util.h"
#include "yb/util/thread_restrictions.h"

using namespace std::literals;

DEFINE_test_flag(bool, assert_local_tablet_server_selected, false, "Verify that SelectTServer "
                 "selected the local tablet server. Also verify that ReplicaSelection is equal "
                 "to CLOSEST_REPLICA");

DEFINE_test_flag(string, assert_tablet_server_select_is_in_zone, "", "Verify that SelectTServer "
                 "selected a talet server in the AZ specified by this flag.");

DECLARE_string(flagfile);

namespace yb {

using std::set;
using std::shared_ptr;
using std::string;
using std::vector;
using strings::Substitute;

using namespace std::placeholders;

using consensus::RaftPeerPB;
using master::GetLeaderMasterRpc;
using master::MasterServiceProxy;
using master::MasterErrorPB;
using rpc::Rpc;
using rpc::RpcController;

namespace client {

using internal::GetTableSchemaRpc;
using internal::RemoteTablet;
using internal::RemoteTabletServer;
using internal::UpdateLocalTsState;

Status RetryFunc(
    CoarseTimePoint deadline, const string& retry_msg, const string& timeout_msg,
    const std::function<Status(CoarseTimePoint, bool*)>& func) {
  DCHECK(deadline != CoarseTimePoint());

  if (deadline < CoarseMonoClock::Now()) {
    return STATUS(TimedOut, timeout_msg);
  }

  MonoDelta wait_time = 1ms;
  MonoDelta kMaxSleep = 2s;
  for (;;) {
    bool retry = true;
    Status s = func(deadline, &retry);
    if (!retry) {
      return s;
    }

    VLOG(1) << retry_msg << " status=" << s.ToString();
    wait_time = std::min(wait_time * 5 / 4, kMaxSleep);

    // We assume that the function will take the same amount of time to run
    // as it did in the previous attempt. If we don't have enough time left
    // to sleep and run it again, we don't bother sleeping and retrying.
    if (CoarseMonoClock::Now() + wait_time > deadline) {
      break;
    }

    VLOG(1) << "Waiting for " << wait_time << " before retrying...";
    SleepFor(wait_time);
  }

  return STATUS(TimedOut, timeout_msg);
}

template <class ReqClass, class RespClass>
Status YBClient::Data::SyncLeaderMasterRpc(
    CoarseTimePoint deadline, YBClient* client, const ReqClass& req, RespClass* resp,
    int* num_attempts, const char* func_name,
    const std::function<Status(MasterServiceProxy*, const ReqClass&, RespClass*, RpcController*)>&
        func) {
  DSCHECK(deadline != CoarseTimePoint(), InvalidArgument, "Deadline is not set");
  CoarseTimePoint start_time;

  while (true) {
    RpcController rpc;

    // Have we already exceeded our deadline?
    auto now = CoarseMonoClock::Now();
    if (start_time == CoarseTimePoint()) {
      start_time = now;
    }
    if (deadline < now) {
      return STATUS_FORMAT(TimedOut,
          "$0 timed out after deadline expired. Time elapsed: $1, allowed: $2",
          func_name, now - start_time, deadline - start_time);
    }

    // The RPC's deadline is intentionally earlier than the overall
    // deadline so that we reserve some time with which to find a new
    // leader master and retry before the overall deadline expires.
    //
    // TODO: KUDU-683 tracks cleanup for this.
    auto rpc_deadline = now + client->default_rpc_timeout();
    rpc.set_deadline(std::min(rpc_deadline, deadline));

    if (num_attempts != nullptr) {
      ++*num_attempts;
    }

    std::shared_ptr<MasterServiceProxy> master_proxy;
    {
      std::lock_guard<simple_spinlock> l(leader_master_lock_);
      master_proxy = master_proxy_;
    }
    Status s = func(master_proxy.get(), req, resp, &rpc);
    if (s.IsNetworkError() || s.IsServiceUnavailable()) {
      YB_LOG_EVERY_N_SECS(WARNING, 1)
          << "Unable to send the request " << req.GetTypeName() << " (" << req.ShortDebugString()
          << ") to leader Master (" << leader_master_hostport().ToString()
          << "): " << s;
      if (client->IsMultiMaster()) {
        YB_LOG_EVERY_N_SECS(INFO, 1) << "Determining the new leader Master and retrying...";
        WARN_NOT_OK(SetMasterServerProxy(client, deadline),
                    "Unable to determine the new leader Master");
      }
      continue;
    }

    if (s.IsTimedOut()) {
      now = CoarseMonoClock::Now();
      if (now < deadline) {
        YB_LOG_EVERY_N_SECS(WARNING, 1)
            << "Unable to send the request (" << req.ShortDebugString()
            << ") to leader Master (" << leader_master_hostport().ToString()
            << "): " << s.ToString();
        if (client->IsMultiMaster()) {
          YB_LOG_EVERY_N_SECS(INFO, 1) << "Determining the new leader Master and retrying...";
          WARN_NOT_OK(SetMasterServerProxy(client, deadline),
                      "Unable to determine the new leader Master");
        }
        continue;
      } else {
        // Operation deadline expired during this latest RPC.
        s = s.CloneAndPrepend(Format(
            "$0 timed out after deadline expired. Time elapsed: $1, allowed: $2",
            func_name, now - start_time, deadline - start_time));
      }
    }

    if (s.ok() && resp->has_error()) {
      if (resp->error().code() == MasterErrorPB::NOT_THE_LEADER ||
          resp->error().code() == MasterErrorPB::CATALOG_MANAGER_NOT_INITIALIZED) {
        if (client->IsMultiMaster()) {
          YB_LOG_EVERY_N_SECS(INFO, 1) << "Determining the new leader Master and retrying...";
          WARN_NOT_OK(SetMasterServerProxy(client, deadline),
                      "Unable to determine the new leader Master");
        }
        continue;
      } else {
        return StatusFromPB(resp->error().status());
      }
    }
    return s;
  }
}

#define YB_CLIENT_SPECIALIZE(RequestTypePB, ResponseTypePB) \
    using yb::master::RequestTypePB; \
    using yb::master::ResponseTypePB; \
    template Status YBClient::Data::SyncLeaderMasterRpc( \
        CoarseTimePoint deadline, YBClient* client, const RequestTypePB& req, \
        ResponseTypePB* resp, int* num_attempts, const char* func_name, \
        const std::function<Status( \
            MasterServiceProxy*, const RequestTypePB&, ResponseTypePB*, RpcController*)>& \
            func);

#define YB_CLIENT_SPECIALIZE_SIMPLE(prefix) \
    YB_CLIENT_SPECIALIZE(BOOST_PP_CAT(prefix, RequestPB), BOOST_PP_CAT(prefix, ResponsePB))

// Explicit specialization for callers outside this compilation unit.
YB_CLIENT_SPECIALIZE_SIMPLE(ListTables);
YB_CLIENT_SPECIALIZE_SIMPLE(ListTabletServers);
YB_CLIENT_SPECIALIZE_SIMPLE(GetTableLocations);
YB_CLIENT_SPECIALIZE_SIMPLE(GetTabletLocations);
YB_CLIENT_SPECIALIZE_SIMPLE(ListMasters);
YB_CLIENT_SPECIALIZE_SIMPLE(CreateNamespace);
YB_CLIENT_SPECIALIZE_SIMPLE(DeleteNamespace);
YB_CLIENT_SPECIALIZE_SIMPLE(AlterNamespace);
YB_CLIENT_SPECIALIZE_SIMPLE(ListNamespaces);
YB_CLIENT_SPECIALIZE_SIMPLE(ReservePgsqlOids);
YB_CLIENT_SPECIALIZE_SIMPLE(GetYsqlCatalogConfig);
YB_CLIENT_SPECIALIZE_SIMPLE(CreateUDType);
YB_CLIENT_SPECIALIZE_SIMPLE(DeleteUDType);
YB_CLIENT_SPECIALIZE_SIMPLE(ListUDTypes);
YB_CLIENT_SPECIALIZE_SIMPLE(GetUDTypeInfo);
YB_CLIENT_SPECIALIZE_SIMPLE(CreateRole);
YB_CLIENT_SPECIALIZE_SIMPLE(AlterRole);
YB_CLIENT_SPECIALIZE_SIMPLE(DeleteRole);
YB_CLIENT_SPECIALIZE_SIMPLE(GrantRevokeRole);
YB_CLIENT_SPECIALIZE_SIMPLE(GrantRevokePermission);
YB_CLIENT_SPECIALIZE_SIMPLE(GetPermissions);
YB_CLIENT_SPECIALIZE_SIMPLE(RedisConfigSet);
YB_CLIENT_SPECIALIZE_SIMPLE(RedisConfigGet);
YB_CLIENT_SPECIALIZE_SIMPLE(CreateCDCStream);
YB_CLIENT_SPECIALIZE_SIMPLE(DeleteCDCStream);
YB_CLIENT_SPECIALIZE_SIMPLE(ListCDCStreams);
YB_CLIENT_SPECIALIZE_SIMPLE(GetCDCStream);
// These are not actually exposed outside, but it's nice to auto-add using directive.
YB_CLIENT_SPECIALIZE_SIMPLE(AlterTable);
YB_CLIENT_SPECIALIZE_SIMPLE(ChangeMasterClusterConfig);
YB_CLIENT_SPECIALIZE_SIMPLE(TruncateTable);
YB_CLIENT_SPECIALIZE_SIMPLE(CreateTable);
YB_CLIENT_SPECIALIZE_SIMPLE(DeleteTable);
YB_CLIENT_SPECIALIZE_SIMPLE(GetMasterClusterConfig);
YB_CLIENT_SPECIALIZE_SIMPLE(GetTableSchema);
YB_CLIENT_SPECIALIZE_SIMPLE(IsAlterTableDone);
YB_CLIENT_SPECIALIZE_SIMPLE(IsCreateTableDone);
YB_CLIENT_SPECIALIZE_SIMPLE(IsTruncateTableDone);
YB_CLIENT_SPECIALIZE_SIMPLE(IsDeleteTableDone);
YB_CLIENT_SPECIALIZE_SIMPLE(IsLoadBalanced);

YBClient::Data::Data()
    : leader_master_rpc_(rpcs_.InvalidHandle()),
      latest_observed_hybrid_time_(YBClient::kNoHybridTime),
      id_(ClientId::GenerateRandom()) {}

YBClient::Data::~Data() {
  dns_resolver_.reset();
  rpcs_.Shutdown();
}

RemoteTabletServer* YBClient::Data::SelectTServer(RemoteTablet* rt,
                                                  const ReplicaSelection selection,
                                                  const set<string>& blacklist,
                                                  vector<RemoteTabletServer*>* candidates) {
  RemoteTabletServer* ret = nullptr;
  candidates->clear();
  if (PREDICT_FALSE(FLAGS_assert_local_tablet_server_selected ||
                    !FLAGS_assert_tablet_server_select_is_in_zone.empty()) &&
      selection != CLOSEST_REPLICA) {
    LOG(FATAL) << "Invalid ReplicaSelection " << selection;
  }

  switch (selection) {
    case LEADER_ONLY: {
      ret = rt->LeaderTServer();
      if (ret != nullptr) {
        candidates->push_back(ret);
        if (ContainsKey(blacklist, ret->permanent_uuid())) {
          ret = nullptr;
        }
      }
      break;
    }
    case CLOSEST_REPLICA:
    case FIRST_REPLICA: {
      if (PREDICT_TRUE(FLAGS_assert_tablet_server_select_is_in_zone.empty())) {
        rt->GetRemoteTabletServers(candidates);
      } else {
        rt->GetRemoteTabletServers(candidates, internal::IncludeFailedReplicas::kTrue);
      }

      // Filter out all the blacklisted candidates.
      vector<RemoteTabletServer*> filtered;
      for (RemoteTabletServer* rts : *candidates) {
        if (!ContainsKey(blacklist, rts->permanent_uuid())) {
          filtered.push_back(rts);
        } else {
          VLOG(1) << "Excluding blacklisted tserver " << rts->permanent_uuid();
        }
      }
      if (selection == FIRST_REPLICA) {
        if (!filtered.empty()) {
          ret = filtered[0];
        }
      } else if (selection == CLOSEST_REPLICA) {
        // Choose the closest replica.
        bool local_zone_ts = false;
        for (RemoteTabletServer* rts : filtered) {
          if (IsTabletServerLocal(*rts)) {
            ret = rts;
            // If the tserver is local, we are done here.
            break;
          } else if (cloud_info_pb_.has_placement_region() &&
                     rts->cloud_info().has_placement_region() &&
                     cloud_info_pb_.placement_region() == rts->cloud_info().placement_region()) {
            if (cloud_info_pb_.has_placement_zone() && rts->cloud_info().has_placement_zone() &&
                cloud_info_pb_.placement_zone() == rts->cloud_info().placement_zone()) {
              // Note down that we have found a zone local tserver and continue looking for node
              // local tserver.
              ret = rts;
              local_zone_ts = true;
            } else if (!local_zone_ts) {
              // Look for a region local tserver only if we haven't found a zone local tserver yet.
              ret = rts;
            }
          }
        }

        // If ret is not null here, it should point to the closest replica from the client.

        // Fallback to a random replica if none are local.
        if (ret == nullptr && !filtered.empty()) {
          ret = filtered[rand() % filtered.size()];
        }
      }
      break;
    }
    default:
      FATAL_INVALID_ENUM_VALUE(ReplicaSelection, selection);
  }
  if (PREDICT_FALSE(FLAGS_assert_local_tablet_server_selected) && !IsTabletServerLocal(*ret)) {
    LOG(FATAL) << "Selected replica is not the local tablet server";
  }
  if (PREDICT_FALSE(!FLAGS_assert_tablet_server_select_is_in_zone.empty())) {
    if (ret->cloud_info().placement_zone() != FLAGS_assert_tablet_server_select_is_in_zone) {
      string msg = Substitute("\nZone placement:\nNumber of candidates: $0\n", candidates->size());
      for (RemoteTabletServer* rts : *candidates) {
        msg += Substitute("Replica: $0 in zone $1\n",
                          rts->ToString(), rts->cloud_info().placement_zone());
      }
      LOG(FATAL) << "Selected replica " << ret->ToString()
                 << " is in zone " << ret->cloud_info().placement_zone()
                 << " instead of the expected zone "
                 << FLAGS_assert_tablet_server_select_is_in_zone
                 << " Cloud info: " << cloud_info_pb_.ShortDebugString()
                 << " for selection policy " << selection
                 << msg;
    }
  }

  return ret;
}

Status YBClient::Data::GetTabletServer(YBClient* client,
                                       const scoped_refptr<RemoteTablet>& rt,
                                       ReplicaSelection selection,
                                       const set<string>& blacklist,
                                       vector<RemoteTabletServer*>* candidates,
                                       RemoteTabletServer** ts) {
  // TODO: write a proper async version of this for async client.
  RemoteTabletServer* ret = SelectTServer(rt.get(), selection, blacklist, candidates);
  if (PREDICT_FALSE(ret == nullptr)) {
    // Construct a blacklist string if applicable.
    string blacklist_string = "";
    if (!blacklist.empty()) {
      blacklist_string = Substitute("(blacklist replicas $0)", JoinStrings(blacklist, ", "));
    }
    return STATUS(ServiceUnavailable,
        Substitute("No $0 for tablet $1 $2",
                   selection == LEADER_ONLY ? "LEADER" : "replicas",
                   rt->tablet_id(),
                   blacklist_string));
  }
  RETURN_NOT_OK(ret->InitProxy(client));

  *ts = ret;
  return Status::OK();
}

Status YBClient::Data::CreateTable(YBClient* client,
                                   const CreateTableRequestPB& req,
                                   const YBSchema& schema,
                                   CoarseTimePoint deadline,
                                   string* table_id) {
  CreateTableResponsePB resp;

  int attempts = 0;
  Status s = SyncLeaderMasterRpc<CreateTableRequestPB, CreateTableResponsePB>(
      deadline, client, req, &resp, &attempts, "CreateTable", &MasterServiceProxy::CreateTable);
  // Set the table id even if there was an error. This is useful when the error is IsAlreadyPresent
  // so that we can wait for the existing table to be available to receive requests.
  *table_id = resp.table_id();
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    if (resp.error().code() == MasterErrorPB::OBJECT_ALREADY_PRESENT && attempts > 1) {
      // If the table already exists and the number of attempts is >
      // 1, then it means we may have succeeded in creating the
      // table, but client didn't receive the successful
      // response (e.g., due to failure before the successful
      // response could be sent back, or due to a I/O pause or a
      // network blip leading to a timeout, etc...)
      YBTableInfo info;
      const string keyspace = req.has_namespace_()
          ? req.namespace_().name()
          : (req.name() == common::kRedisTableName ? common::kRedisKeyspaceName : "");
      const YQLDatabase db_type = req.has_namespace_() && req.namespace_().has_database_type()
          ? req.namespace_().database_type()
          : (keyspace.empty() ? YQL_DATABASE_CQL : master::GetDefaultDatabaseType(keyspace));
      const YBTableName table_name(db_type, req.name());

      // A fix for https://yugabyte.atlassian.net/browse/ENG-529:
      // If we've been retrying table creation, and the table is now in the process is being
      // created, we can sometimes see an empty schema. Wait until the table is fully created
      // before we compare the schema.
      RETURN_NOT_OK_PREPEND(
          WaitForCreateTableToFinish(client, table_name, resp.table_id(), deadline),
          Substitute("Failed waiting for table $0 to finish being created", table_name.ToString()));

      RETURN_NOT_OK_PREPEND(
          GetTableSchema(client, table_name, deadline, &info),
          Substitute("Unable to check the schema of table $0", table_name.ToString()));
      if (!schema.Equals(info.schema)) {
         string msg = Format("Table $0 already exists with a different "
                             "schema. Requested schema was: $1, actual schema is: $2",
                             table_name,
                             internal::GetSchema(schema),
                             internal::GetSchema(info.schema));
        LOG(ERROR) << msg;
        return STATUS(AlreadyPresent, msg);
      } else {
        PartitionSchema partition_schema;
        // We need to use the schema received from the server, because the user-constructed
        // schema might not have column ids.
        RETURN_NOT_OK(PartitionSchema::FromPB(req.partition_schema(),
                                              internal::GetSchema(info.schema),
                                              &partition_schema));
        if (!partition_schema.Equals(info.partition_schema)) {
          string msg = Substitute("Table $0 already exists with a different partition schema. "
              "Requested partition schema was: $1, actual partition schema is: $2",
              table_name.ToString(),
              partition_schema.DebugString(internal::GetSchema(schema)),
              info.partition_schema.DebugString(internal::GetSchema(info.schema)));
          LOG(ERROR) << msg;
          return STATUS(AlreadyPresent, msg);
        } else {
          return Status::OK();
        }
      }
    }
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status YBClient::Data::IsCreateTableInProgress(YBClient* client,
                                               const YBTableName& table_name,
                                               const string& table_id,
                                               CoarseTimePoint deadline,
                                               bool* create_in_progress) {
  DCHECK_ONLY_NOTNULL(create_in_progress);
  IsCreateTableDoneRequestPB req;
  IsCreateTableDoneResponsePB resp;
  if (table_name.has_table()) {
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
  }
  if (!table_id.empty()) {
    req.mutable_table()->set_table_id(table_id);
  }

  const Status s =
      SyncLeaderMasterRpc<IsCreateTableDoneRequestPB, IsCreateTableDoneResponsePB>(
          deadline,
          client,
          req,
          &resp,
          nullptr /* num_attempts */,
          "IsCreateTableDone",
          &MasterServiceProxy::IsCreateTableDone);
  // RETURN_NOT_OK macro can't take templated function call as param,
  // and SyncLeaderMasterRpc must be explicitly instantiated, else the
  // compiler complains.
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  *create_in_progress = !resp.done();
  return Status::OK();
}

Status YBClient::Data::WaitForCreateTableToFinish(YBClient* client,
                                                  const YBTableName& table_name,
                                                  const string& table_id,
                                                  CoarseTimePoint deadline) {
  return RetryFunc(
      deadline, "Waiting on Create Table to be completed", "Timed out waiting for Table Creation",
      std::bind(&YBClient::Data::IsCreateTableInProgress, this, client,
                table_name, table_id, _1, _2));
}

Status YBClient::Data::DeleteTable(YBClient* client,
                                   const YBTableName& table_name,
                                   const string& table_id,
                                   const bool is_index_table,
                                   CoarseTimePoint deadline,
                                   YBTableName* indexed_table_name,
                                   bool wait) {
  DeleteTableRequestPB req;
  DeleteTableResponsePB resp;
  int attempts = 0;

  if (table_name.has_table()) {
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
  }
  if (!table_id.empty()) {
    req.mutable_table()->set_table_id(table_id);
  }
  req.set_is_index_table(is_index_table);
  const Status s = SyncLeaderMasterRpc<DeleteTableRequestPB, DeleteTableResponsePB>(
      deadline, client, req, &resp,
      &attempts, "DeleteTable", &MasterServiceProxy::DeleteTable);

  // Handle special cases based on resp.error().
  if (resp.has_error()) {
    LOG_IF(DFATAL, s.ok()) << "Expecting error status if response has error: " <<
        resp.error().code() << " Status: " << resp.error().status().ShortDebugString();

    if (resp.error().code() == MasterErrorPB::OBJECT_NOT_FOUND && attempts > 1) {
      // A prior attempt to delete the table has succeeded, but
      // appeared as a failure to the client due to, e.g., an I/O or
      // network issue.
      // Good case - go through - to 'return Status::OK()'
    } else {
      return StatusFromPB(resp.error().status());
    }
  } else {
    // Check the status only if the response has no error.
    RETURN_NOT_OK(s);
  }

  // Spin until the table is fully deleted, if requested.
  if (wait && resp.has_table_id()) {
    RETURN_NOT_OK(WaitForDeleteTableToFinish(client, resp.table_id(), deadline));
  }

  // Return indexed table name if requested.
  if (resp.has_indexed_table() && indexed_table_name != nullptr) {
    indexed_table_name->GetFromTableIdentifierPB(resp.indexed_table());
  }

  LOG(INFO) << "Deleted table " << table_name.ToString();
  return Status::OK();
}

Status YBClient::Data::IsDeleteTableInProgress(YBClient* client,
                                               const std::string& table_id,
                                               CoarseTimePoint deadline,
                                               bool* delete_in_progress) {
  DCHECK_ONLY_NOTNULL(delete_in_progress);
  IsDeleteTableDoneRequestPB req;
  IsDeleteTableDoneResponsePB resp;
  req.set_table_id(table_id);

  const Status s =
      SyncLeaderMasterRpc<IsDeleteTableDoneRequestPB, IsDeleteTableDoneResponsePB>(
          deadline,
          client,
          req,
          &resp,
          nullptr /* num_attempts */,
          "IsDeleteTableDone",
          &MasterServiceProxy::IsDeleteTableDone);
  // RETURN_NOT_OK macro can't take templated function call as param,
  // and SyncLeaderMasterRpc must be explicitly instantiated, else the
  // compiler complains.
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    if (resp.error().code() == MasterErrorPB::OBJECT_NOT_FOUND) {
      *delete_in_progress = false;
      return Status::OK();
    }
    return StatusFromPB(resp.error().status());
  }

  *delete_in_progress = !resp.done();
  return Status::OK();
}

Status YBClient::Data::WaitForDeleteTableToFinish(YBClient* client,
                                                  const std::string& table_id,
                                                  CoarseTimePoint deadline) {
  return RetryFunc(
      deadline, "Waiting on Delete Table to be completed", "Timed out waiting for Table Deletion",
      std::bind(&YBClient::Data::IsDeleteTableInProgress, this, client, table_id, _1, _2));
}

Status YBClient::Data::TruncateTables(YBClient* client,
                                     const vector<string>& table_ids,
                                     CoarseTimePoint deadline,
                                     bool wait) {
  TruncateTableRequestPB req;
  TruncateTableResponsePB resp;

  for (const auto& table_id : table_ids) {
    req.add_table_ids(table_id);
  }
  RETURN_NOT_OK((SyncLeaderMasterRpc<TruncateTableRequestPB, TruncateTableResponsePB>(
      deadline, client, req, &resp, nullptr /* num_attempts */, "TruncateTable",
      &MasterServiceProxy::TruncateTable)));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  // Spin until the table is fully truncated, if requested.
  if (wait) {
    for (const auto& table_id : table_ids) {
      RETURN_NOT_OK(WaitForTruncateTableToFinish(client, table_id, deadline));
    }
  }

  LOG(INFO) << "Truncated table(s) " << JoinStrings(table_ids, ",");
  return Status::OK();
}

Status YBClient::Data::IsTruncateTableInProgress(YBClient* client,
                                                 const std::string& table_id,
                                                 CoarseTimePoint deadline,
                                                 bool* truncate_in_progress) {
  DCHECK_ONLY_NOTNULL(truncate_in_progress);
  IsTruncateTableDoneRequestPB req;
  IsTruncateTableDoneResponsePB resp;

  req.set_table_id(table_id);
  RETURN_NOT_OK((SyncLeaderMasterRpc<IsTruncateTableDoneRequestPB, IsTruncateTableDoneResponsePB>(
      deadline, client, req, &resp, nullptr /* num_attempts */, "IsTruncateTableDone",
      &MasterServiceProxy::IsTruncateTableDone)));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  *truncate_in_progress = !resp.done();
  return Status::OK();
}

Status YBClient::Data::WaitForTruncateTableToFinish(YBClient* client,
                                                    const std::string& table_id,
                                                    CoarseTimePoint deadline) {
  return RetryFunc(
      deadline, "Waiting on Truncate Table to be completed",
      "Timed out waiting for Table Truncation",
      std::bind(&YBClient::Data::IsTruncateTableInProgress, this, client, table_id, _1, _2));
}

Status YBClient::Data::AlterNamespace(YBClient* client,
                                      const AlterNamespaceRequestPB& req,
                                      CoarseTimePoint deadline) {
  AlterNamespaceResponsePB resp;
  Status s =
      SyncLeaderMasterRpc<AlterNamespaceRequestPB, AlterNamespaceResponsePB>(
          deadline,
          client,
          req,
          &resp,
          nullptr /* num_attempts */,
          "AlterNamespace",
          &MasterServiceProxy::AlterNamespace);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status YBClient::Data::AlterTable(YBClient* client,
                                  const AlterTableRequestPB& req,
                                  CoarseTimePoint deadline) {
  AlterTableResponsePB resp;
  Status s =
      SyncLeaderMasterRpc<AlterTableRequestPB, AlterTableResponsePB>(
          deadline,
          client,
          req,
          &resp,
          nullptr /* num_attempts */,
          "AlterTable",
          &MasterServiceProxy::AlterTable);
  RETURN_NOT_OK(s);
  // TODO: Consider the situation where the request is sent to the
  // server, gets executed on the server and written to the server,
  // but is seen as failed by the client, and is then retried (in which
  // case the retry will fail due to original table being removed, a
  // column being already added, etc...)
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status YBClient::Data::IsAlterTableInProgress(YBClient* client,
                                              const YBTableName& table_name,
                                              string table_id,
                                              CoarseTimePoint deadline,
                                              bool *alter_in_progress) {
  IsAlterTableDoneRequestPB req;
  IsAlterTableDoneResponsePB resp;

  if (table_name.has_table()) {
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
  }

  if (!table_id.empty()) {
    (req.mutable_table())->set_table_id(table_id);
  }

  Status s =
      SyncLeaderMasterRpc<IsAlterTableDoneRequestPB, IsAlterTableDoneResponsePB>(
          deadline,
          client,
          req,
          &resp,
          nullptr /* num_attempts */,
          "IsAlterTableDone",
          &MasterServiceProxy::IsAlterTableDone);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  *alter_in_progress = !resp.done();
  return Status::OK();
}

Status YBClient::Data::WaitForAlterTableToFinish(YBClient* client,
                                                 const YBTableName& alter_name,
                                                 const string table_id,
                                                 CoarseTimePoint deadline) {
  return RetryFunc(
      deadline, "Waiting on Alter Table to be completed", "Timed out waiting for AlterTable",
      std::bind(&YBClient::Data::IsAlterTableInProgress, this, client,
              alter_name, table_id, _1, _2));
}

Status YBClient::Data::InitLocalHostNames() {
  std::vector<IpAddress> addresses;
  auto status = GetLocalAddresses(&addresses, AddressFilter::EXTERNAL);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to enumerate network interfaces" << status.ToString();
  }

  string hostname;
  status = GetFQDN(&hostname);

  if (status.ok()) {
    // We don't want to consider 'localhost' to be local - otherwise if a misconfigured
    // server reports its own name as localhost, all clients will hammer it.
    if (hostname != "localhost" && hostname != "localhost.localdomain") {
      local_host_names_.insert(hostname);
      VLOG(1) << "Considering host " << hostname << " local";
    }

    std::vector<Endpoint> endpoints;
    status = HostPort(hostname, 0).ResolveAddresses(&endpoints);
    if (!status.ok()) {
      const auto message = Substitute("Could not resolve local host name '$0'", hostname);
      LOG(WARNING) << message;
      if (addresses.empty()) {
        return status.CloneAndPrepend(message);
      }
    } else {
      addresses.reserve(addresses.size() + endpoints.size());
      for (const auto& endpoint : endpoints) {
        addresses.push_back(endpoint.address());
      }
    }
  } else {
    LOG(WARNING) << "Failed to get hostname: " << status.ToString();
    if (addresses.empty()) {
      return status;
    }
  }

  for (const auto& addr : addresses) {
    // Similar to above, ignore local or wildcard addresses.
    if (addr.is_unspecified() || addr.is_loopback()) continue;

    VLOG(1) << "Considering host " << addr << " local";
    local_host_names_.insert(addr.to_string());
  }

  return Status::OK();
}

bool YBClient::Data::IsLocalHostPort(const HostPort& hp) const {
  return ContainsKey(local_host_names_, hp.host());
}

bool YBClient::Data::IsTabletServerLocal(const RemoteTabletServer& rts) const {
  // If the uuid's are same, we are sure the tablet server is local, since if this client is used
  // via the CQL proxy, the tablet server's uuid is set in the client.
  if (uuid_ == rts.permanent_uuid()) {
    return true;
  }

  return rts.HasHostFrom(local_host_names_);
}

namespace internal {

// Gets data from the leader master. If the leader master
// is down, waits for a new master to become the leader, and then gets
// the data from the new leader master.
class ClientMasterRpc : public Rpc {
 public:
  ClientMasterRpc(YBClient* client,
                  CoarseTimePoint deadline,
                  rpc::Messenger* messenger,
                  rpc::ProxyCache* proxy_cache);

  virtual ~ClientMasterRpc();

  void ResetLeaderMasterAndRetry();

  void NewLeaderMasterDeterminedCb(const Status& status);

  template<class Response>
  Status HandleFinished(const Status& status, const Response& resp, bool* finished);

 private:
  YBClient* const client_;

};

// Gets a table's schema from the leader master. If the leader master
// is down, waits for a new master to become the leader, and then gets
// the table schema from the new leader master.
//
// TODO: When we implement the next fault tolerant client-master RPC
// call (e.g., CreateTable/AlterTable), we should generalize this
// method as to enable code sharing.
class GetTableSchemaRpc : public ClientMasterRpc {
 public:
  GetTableSchemaRpc(YBClient* client,
                    StatusCallback user_cb,
                    const YBTableName& table_name,
                    YBTableInfo* info,
                    CoarseTimePoint deadline,
                    rpc::Messenger* messenger,
                    rpc::ProxyCache* proxy_cache);
  GetTableSchemaRpc(YBClient* client,
                    StatusCallback user_cb,
                    const TableId& table_id,
                    YBTableInfo* info,
                    CoarseTimePoint deadline,
                    rpc::Messenger* messenger,
                    rpc::ProxyCache* proxy_cache);

  void SendRpc() override;

  string ToString() const override;

  virtual ~GetTableSchemaRpc();

 private:
  void Finished(const Status& status) override;

  YBClient* const client_;
  StatusCallback user_cb_;
  master::TableIdentifierPB table_identifier_;
  YBTableInfo* info_;
  GetTableSchemaRequestPB req_;
  GetTableSchemaResponsePB resp_;
  rpc::Rpcs::Handle retained_self_;
};

namespace {

master::TableIdentifierPB ToTableIdentifierPB(const YBTableName& table_name) {
  master::TableIdentifierPB id;
  table_name.SetIntoTableIdentifierPB(&id);
  return id;
}

master::TableIdentifierPB ToTableIdentifierPB(const TableId& table_id) {
  master::TableIdentifierPB id;
  id.set_table_id(table_id);
  return id;
}

} // namespace

ClientMasterRpc::ClientMasterRpc(YBClient* client,
                                 CoarseTimePoint deadline,
                                 rpc::Messenger* messenger,
                                 rpc::ProxyCache* proxy_cache)
    : Rpc(deadline, messenger, proxy_cache),
      client_(DCHECK_NOTNULL(client)) {
}

ClientMasterRpc::~ClientMasterRpc() {
}

void ClientMasterRpc::ResetLeaderMasterAndRetry() {
  client_->data_->SetMasterServerProxyAsync(
      client_,
      retrier().deadline(),
      false /* skip_resolution */,
      Bind(&ClientMasterRpc::NewLeaderMasterDeterminedCb,
           Unretained(this)));
}

void ClientMasterRpc::NewLeaderMasterDeterminedCb(const Status& status) {
  if (status.ok()) {
    mutable_retrier()->mutable_controller()->Reset();
    SendRpc();
  } else {
    LOG(WARNING) << "Failed to determine new Master: " << status.ToString();
    ScheduleRetry(status);
  }
}

template<class Response>
Status ClientMasterRpc::HandleFinished(const Status& status, const Response& resp,
                                       bool* finished) {
  *finished = false;
  Status new_status = status;
  if (new_status.ok() && mutable_retrier()->HandleResponse(this, &new_status)) {
    return new_status;
  }

  if (new_status.ok() && resp.has_error()) {
    if (resp.error().code() == MasterErrorPB::NOT_THE_LEADER ||
        resp.error().code() == MasterErrorPB::CATALOG_MANAGER_NOT_INITIALIZED) {
      LOG(WARNING) << "Leader Master has changed ("
                   << client_->data_->leader_master_hostport().ToString()
                   << " is no longer the leader), re-trying...";
      ResetLeaderMasterAndRetry();
      return new_status;
    }

    if (resp.error().status().code() == AppStatusPB::LEADER_NOT_READY_TO_SERVE ||
        resp.error().status().code() == AppStatusPB::LEADER_HAS_NO_LEASE) {
      LOG(WARNING) << "Leader Master " << client_->data_->leader_master_hostport().ToString()
                   << " does not have a valid exclusive lease: "
                   << resp.error().status().ShortDebugString() << ", re-trying...";
      ResetLeaderMasterAndRetry();
      return new_status;
    }
    VLOG(2) << "resp.error().status()=" << resp.error().status().DebugString();
    new_status = StatusFromPB(resp.error().status());
  }

  if (new_status.IsTimedOut()) {
    if (CoarseMonoClock::Now() < retrier().deadline()) {
      LOG(WARNING) << "Leader Master ("
          << client_->data_->leader_master_hostport().ToString()
          << ") timed out, re-trying...";
      ResetLeaderMasterAndRetry();
      return new_status;
    } else {
      // Operation deadline expired during this latest RPC.
      new_status = new_status.CloneAndPrepend(
          "RPC timed out after deadline expired");
    }
  }

  if (new_status.IsNetworkError()) {
    LOG(WARNING) << "Encountered a network error from the Master("
                 << client_->data_->leader_master_hostport().ToString() << "): "
                 << new_status.ToString() << ", retrying...";
    ResetLeaderMasterAndRetry();
    return new_status;
  }

  *finished = true;
  return new_status;
}

GetTableSchemaRpc::GetTableSchemaRpc(YBClient* client,
                                     StatusCallback user_cb,
                                     const YBTableName& table_name,
                                     YBTableInfo* info,
                                     CoarseTimePoint deadline,
                                     rpc::Messenger* messenger,
                                     rpc::ProxyCache* proxy_cache)
    : ClientMasterRpc(client, deadline, messenger, proxy_cache),
      client_(DCHECK_NOTNULL(client)),
      user_cb_(std::move(user_cb)),
      table_identifier_(ToTableIdentifierPB(table_name)),
      info_(DCHECK_NOTNULL(info)),
      retained_self_(client->data_->rpcs_.InvalidHandle()) {
}

GetTableSchemaRpc::GetTableSchemaRpc(YBClient* client,
                                     StatusCallback user_cb,
                                     const TableId& table_id,
                                     YBTableInfo* info,
                                     CoarseTimePoint deadline,
                                     rpc::Messenger* messenger,
                                     rpc::ProxyCache* proxy_cache)
    : ClientMasterRpc(client, deadline, messenger, proxy_cache),
      client_(DCHECK_NOTNULL(client)),
      user_cb_(std::move(user_cb)),
      table_identifier_(ToTableIdentifierPB(table_id)),
      info_(DCHECK_NOTNULL(info)),
      retained_self_(client->data_->rpcs_.InvalidHandle()) {
}

GetTableSchemaRpc::~GetTableSchemaRpc() {
}

void GetTableSchemaRpc::SendRpc() {
  client_->data_->rpcs_.Register(shared_from_this(), &retained_self_);

  auto now = CoarseMonoClock::Now();
  if (retrier().deadline() < now) {
    Finished(STATUS(TimedOut, "GetTableSchema timed out after deadline expired"));
    return;
  }

  // See YBClient::Data::SyncLeaderMasterRpc().
  auto rpc_deadline = now + client_->default_rpc_timeout();
  mutable_retrier()->mutable_controller()->set_deadline(
      std::min(rpc_deadline, retrier().deadline()));

  req_.mutable_table()->CopyFrom(table_identifier_);
  client_->data_->master_proxy()->GetTableSchemaAsync(
      req_, &resp_, mutable_retrier()->mutable_controller(),
      std::bind(&GetTableSchemaRpc::Finished, this, Status::OK()));
}

string GetTableSchemaRpc::ToString() const {
  return Substitute("GetTableSchemaRpc(table_identifier: $0, num_attempts: $1)",
                    table_identifier_.ShortDebugString(), num_attempts());
}

void GetTableSchemaRpc::Finished(const Status& status) {
  bool finished;
  Status new_status = HandleFinished(status, resp_, &finished);
  if (!finished) {
    return;
  }

  auto retained_self = client_->data_->rpcs_.Unregister(&retained_self_);

  if (new_status.ok()) {
    std::unique_ptr<Schema> schema(new Schema());
    new_status = SchemaFromPB(resp_.schema(), schema.get());
    if (new_status.ok()) {
      info_->schema.Reset(std::move(schema));
      info_->schema.set_version(resp_.version());
      new_status = PartitionSchema::FromPB(resp_.partition_schema(),
                                           GetSchema(&info_->schema),
                                           &info_->partition_schema);

      info_->table_name.GetFromTableIdentifierPB(resp_.identifier());
      info_->table_id = resp_.identifier().table_id();
      YBTable::PBToClientTableType(resp_.table_type(), &info_->table_type);
      info_->index_map.FromPB(resp_.indexes());
      if (resp_.has_index_info()) {
        info_->index_info.emplace(resp_.index_info());
      }
      CHECK_GT(info_->table_id.size(), 0) << "Running against a too-old master";
    }
  }
  if (!new_status.ok()) {
    LOG(WARNING) << ToString() << " failed: " << new_status.ToString();
  }
  user_cb_.Run(new_status);
}

class CreateCDCStreamRpc : public ClientMasterRpc {
 public:
  CreateCDCStreamRpc(YBClient* client,
                     CreateCDCStreamCallback user_cb,
                     const TableId& table_id,
                     const std::unordered_map<std::string, std::string>& options,
                     CoarseTimePoint deadline,
                     rpc::Messenger* messenger,
                     rpc::ProxyCache* proxy_cache);

  void SendRpc() override;

  string ToString() const override;

  virtual ~CreateCDCStreamRpc();

 private:
  void Finished(const Status& status) override;

  YBClient* const client_;
  CreateCDCStreamCallback user_cb_;
  std::string table_id_;
  std::unordered_map<std::string, std::string> options_;
  CreateCDCStreamRequestPB req_;
  CreateCDCStreamResponsePB resp_;
  rpc::Rpcs::Handle retained_self_;
};

CreateCDCStreamRpc::CreateCDCStreamRpc(YBClient* client,
                                       CreateCDCStreamCallback user_cb,
                                       const TableId& table_id,
                                       const std::unordered_map<std::string, std::string>& options,
                                       CoarseTimePoint deadline,
                                       rpc::Messenger* messenger,
                                       rpc::ProxyCache* proxy_cache)
    : ClientMasterRpc(client, deadline, messenger, proxy_cache),
      client_(DCHECK_NOTNULL(client)),
      user_cb_(std::move(user_cb)),
      table_id_(table_id),
      options_(options),
      retained_self_(client->data_->rpcs_.InvalidHandle()) {
}

CreateCDCStreamRpc::~CreateCDCStreamRpc() {
}

void CreateCDCStreamRpc::SendRpc() {
  client_->data_->rpcs_.Register(shared_from_this(), &retained_self_);

  auto now = CoarseMonoClock::Now();
  if (retrier().deadline() < now) {
    Finished(STATUS(TimedOut, "CreateCDCStream timed out after deadline expired"));
    return;
  }

  // See YBClient::Data::SyncLeaderMasterRpc().
  auto rpc_deadline = now + client_->default_rpc_timeout();
  mutable_retrier()->mutable_controller()->set_deadline(
      std::min(rpc_deadline, retrier().deadline()));

  req_.set_table_id(table_id_);
  req_.mutable_options()->Reserve(options_.size());
  for (const auto& option : options_) {
    auto* op = req_.add_options();
    op->set_key(option.first);
    op->set_value(option.second);
  }

  client_->data_->master_proxy()->CreateCDCStreamAsync(
      req_, &resp_, mutable_retrier()->mutable_controller(),
      std::bind(&CreateCDCStreamRpc::Finished, this, Status::OK()));
}

string CreateCDCStreamRpc::ToString() const {
  return Substitute("CreateCDCStream(table_id: $0, num_attempts: $1)",
                    table_id_, num_attempts());
}

void CreateCDCStreamRpc::Finished(const Status& status) {
  bool finished;
  Status new_status = HandleFinished(status, resp_, &finished);
  if (!finished) {
    return;
  }

  auto retained_self = client_->data_->rpcs_.Unregister(&retained_self_);

  if (new_status.ok()) {
    user_cb_(resp_.stream_id());
  } else {
    LOG(WARNING) << ToString() << " failed: " << new_status.ToString();
    user_cb_(new_status);
  }
}

class DeleteCDCStreamRpc : public ClientMasterRpc {
 public:
  DeleteCDCStreamRpc(YBClient* client,
                     StatusCallback user_cb,
                     const CDCStreamId& stream_id,
                     CoarseTimePoint deadline,
                     rpc::Messenger* messenger,
                     rpc::ProxyCache* proxy_cache);

  void SendRpc() override;

  string ToString() const override;

  virtual ~DeleteCDCStreamRpc();

 private:
  void Finished(const Status& status) override;

  YBClient* const client_;
  StatusCallback user_cb_;
  std::string stream_id_;
  DeleteCDCStreamRequestPB req_;
  DeleteCDCStreamResponsePB resp_;
  rpc::Rpcs::Handle retained_self_;
};

DeleteCDCStreamRpc::DeleteCDCStreamRpc(YBClient* client,
                                       StatusCallback user_cb,
                                       const CDCStreamId& stream_id,
                                       CoarseTimePoint deadline,
                                       rpc::Messenger* messenger,
                                       rpc::ProxyCache* proxy_cache)
    : ClientMasterRpc(client, deadline, messenger, proxy_cache),
      client_(DCHECK_NOTNULL(client)),
      user_cb_(std::move(user_cb)),
      stream_id_(stream_id),
      retained_self_(client->data_->rpcs_.InvalidHandle()) {
}

DeleteCDCStreamRpc::~DeleteCDCStreamRpc() {
}

void DeleteCDCStreamRpc::SendRpc() {
  client_->data_->rpcs_.Register(shared_from_this(), &retained_self_);

  auto now = CoarseMonoClock::Now();
  if (retrier().deadline() < now) {
    Finished(STATUS(TimedOut, "DeleteCDCStream timed out after deadline expired"));
    return;
  }

  // See YBClient::Data::SyncLeaderMasterRpc().
  auto rpc_deadline = now + client_->default_rpc_timeout();
  mutable_retrier()->mutable_controller()->set_deadline(
      std::min(rpc_deadline, retrier().deadline()));

  req_.add_stream_id(stream_id_);
  client_->data_->master_proxy()->DeleteCDCStreamAsync(
      req_, &resp_, mutable_retrier()->mutable_controller(),
      std::bind(&DeleteCDCStreamRpc::Finished, this, Status::OK()));
}

string DeleteCDCStreamRpc::ToString() const {
  return Substitute("DeleteCDCStream(stream_id: $0, num_attempts: $1)",
                    stream_id_, num_attempts());
}

void DeleteCDCStreamRpc::Finished(const Status& status) {
  bool finished;
  Status new_status = HandleFinished(status, resp_, &finished);
  if (!finished) {
    return;
  }

  auto retained_self = client_->data_->rpcs_.Unregister(&retained_self_);

  if (!new_status.ok()) {
    LOG(WARNING) << ToString() << " failed: " << new_status.ToString();
  }
  user_cb_.Run(new_status);
}

} // namespace internal

Status YBClient::Data::GetTableSchema(YBClient* client,
                                      const YBTableName& table_name,
                                      CoarseTimePoint deadline,
                                      YBTableInfo* info) {
  Synchronizer sync;
  auto rpc = rpc::StartRpc<GetTableSchemaRpc>(
      client,
      sync.AsStatusCallback(),
      table_name,
      info,
      deadline,
      messenger_,
      proxy_cache_.get());
  return sync.Wait();
}

Status YBClient::Data::GetTableSchema(YBClient* client,
                                      const TableId& table_id,
                                      CoarseTimePoint deadline,
                                      YBTableInfo* info) {
  Synchronizer sync;
  auto rpc = rpc::StartRpc<GetTableSchemaRpc>(
      client,
      sync.AsStatusCallback(),
      table_id,
      info,
      deadline,
      messenger_,
      proxy_cache_.get());
  return sync.Wait();
}

Status YBClient::Data::GetTableSchemaById(YBClient* client,
                                          const TableId& table_id,
                                          CoarseTimePoint deadline,
                                          std::shared_ptr<YBTableInfo> info,
                                          StatusCallback callback) {
  auto rpc = rpc::StartRpc<GetTableSchemaRpc>(
      client,
      callback,
      table_id,
      info.get(),
      deadline,
      messenger_,
      proxy_cache_.get());
  return Status::OK();
}

void YBClient::Data::CreateCDCStream(YBClient* client,
                                     const TableId& table_id,
                                     const std::unordered_map<std::string, std::string>& options,
                                     CoarseTimePoint deadline,
                                     CreateCDCStreamCallback callback) {
  auto rpc = rpc::StartRpc<internal::CreateCDCStreamRpc>(
      client,
      callback,
      table_id,
      options,
      deadline,
      messenger_,
      proxy_cache_.get());
}

void YBClient::Data::DeleteCDCStream(YBClient* client,
                                     const CDCStreamId& stream_id,
                                     CoarseTimePoint deadline,
                                     StatusCallback callback) {
  auto rpc = rpc::StartRpc<internal::DeleteCDCStreamRpc>(
      client,
      callback,
      stream_id,
      deadline,
      messenger_,
      proxy_cache_.get());
}

void YBClient::Data::LeaderMasterDetermined(const Status& status,
                                            const HostPort& host_port) {
  Status new_status = status;

  std::vector<StatusCallback> cbs;
  {
    std::lock_guard<simple_spinlock> l(leader_master_lock_);
    cbs.swap(leader_master_callbacks_);

    if (new_status.ok()) {
      leader_master_hostport_ = host_port;
      master_proxy_.reset(new MasterServiceProxy(proxy_cache_.get(), host_port));
    }

    rpcs_.Unregister(&leader_master_rpc_);
  }

  for (const StatusCallback& cb : cbs) {
    cb.Run(new_status);
  }
}

Status YBClient::Data::SetMasterServerProxy(YBClient* client,
                                            CoarseTimePoint deadline,
                                            bool skip_resolution) {
  Synchronizer sync;
  SetMasterServerProxyAsync(client, deadline, skip_resolution, sync.AsStatusCallback());
  return sync.Wait();
}

void YBClient::Data::SetMasterServerProxyAsync(YBClient* client,
                                               CoarseTimePoint deadline,
                                               bool skip_resolution,
                                               const StatusCallback& cb) {
  DCHECK(deadline != CoarseTimePoint::max());

  server::MasterAddresses master_addrs;
  // Refresh the value of 'master_server_addrs_' if needed.
  Status s = ReinitializeMasterAddresses();
  {
    std::lock_guard<simple_spinlock> l(master_server_addrs_lock_);
    if (!s.ok() && master_server_addrs_.empty()) {
      cb.Run(s);
      return;
    }
    for (const string &master_server_addr : master_server_addrs_) {
      std::vector<HostPort> addrs;
      // TODO: Do address resolution asynchronously as well.
      s = HostPort::ParseStrings(master_server_addr, master::kMasterDefaultPort, &addrs);
      if (!s.ok()) {
        cb.Run(s);
        return;
      }
      if (addrs.empty()) {
        cb.Run(STATUS_FORMAT(
            InvalidArgument,
            "No master address specified by '$0' (all master server addresses: $1)",
            master_server_addr, master_server_addrs_));
        return;
      }

      master_addrs.push_back(std::move(addrs));
    }
  }

  // Finding a new master involves a fan-out RPC to each master. A single
  // RPC timeout's worth of time should be sufficient, though we'll use
  // the provided deadline if it's sooner.
  auto leader_master_deadline = CoarseMonoClock::Now() + client->default_rpc_timeout();
  auto actual_deadline = std::min(deadline, leader_master_deadline);

  // This ensures that no more than one GetLeaderMasterRpc is in
  // flight at a time -- there isn't much sense in requesting this information
  // in parallel, since the requests should end up with the same result.
  // Instead, we simply piggy-back onto the existing request by adding our own
  // callback to leader_master_callbacks_.
  std::unique_lock<simple_spinlock> l(leader_master_lock_);
  leader_master_callbacks_.push_back(cb);
  if (skip_resolution && !master_addrs.empty() && !master_addrs.front().empty()) {
    l.unlock();
    LeaderMasterDetermined(Status::OK(), master_addrs.front().front());
    return;
  }
  if (leader_master_rpc_ == rpcs_.InvalidHandle()) {
    // No one is sending a request yet - we need to be the one to do it.
    rpcs_.Register(
        std::make_shared<GetLeaderMasterRpc>(
            Bind(&YBClient::Data::LeaderMasterDetermined, Unretained(this)),
            master_addrs,
            actual_deadline,
            messenger_,
            proxy_cache_.get(),
            &rpcs_),
        &leader_master_rpc_);
    l.unlock();
    (**leader_master_rpc_).SendRpc();
  }
}

// API to clear and reset master addresses, used during master config change.
Status YBClient::Data::SetMasterAddresses(const string& addrs) {
  std::lock_guard<simple_spinlock> l(master_server_addrs_lock_);
  if (addrs.empty()) {
    std::ostringstream out;
    out.str("Invalid empty master address cannot be set. Current list is: ");
    for (const string& master_server_addr : master_server_addrs_) {
      out.str(master_server_addr);
      out.str(" ");
    }
    LOG(ERROR) << out.str();
    return STATUS(InvalidArgument, "master addresses cannot be empty");
  }

  master_server_addrs_.clear();
  master_server_addrs_.push_back(addrs);

  return Status::OK();
}

// Add a given master to the master address list.
Status YBClient::Data::AddMasterAddress(const HostPort& addr) {
  std::lock_guard<simple_spinlock> l(master_server_addrs_lock_);
  master_server_addrs_.push_back(addr.ToString());
  return Status::OK();
}

namespace {

Result<std::string> ReadMasterAddressesFromFlagFile(const std::string& flag_file_path) {
  std::ifstream input_file(flag_file_path);
  if (!input_file) {
    return STATUS_FORMAT(IOError, "Unable to open flag file '$0': $1",
        flag_file_path, strerror(errno));
  }
  std::string line;

  std::string master_addrs;
  while (input_file.good() && std::getline(input_file, line)) {
    static const std::string kTServerMasterAddrsFlagPrefix = "--tserver_master_addrs=";
    if (boost::starts_with(line, kTServerMasterAddrsFlagPrefix)) {
      master_addrs = line.c_str() + kTServerMasterAddrsFlagPrefix.size();
    }
  }

  if (input_file.bad()) {
    // Do not check input_file.fail() here, reaching EOF may set that.
    return STATUS_FORMAT(IOError, "Failed reading flag file '$0': $1",
        flag_file_path, strerror(errno));
  }
  return master_addrs;
}

} // anonymous namespace

// Read the master addresses (from a remote endpoint or a file depending on which is specified), and
// re-initialize the 'master_server_addrs_' variable.
Status YBClient::Data::ReinitializeMasterAddresses() {
  std::lock_guard<simple_spinlock> l(master_server_addrs_lock_);
  if (!master_server_endpoint_.empty()) {
    faststring buf;
    RETURN_NOT_OK(EasyCurl().FetchURL(master_server_endpoint_, &buf));
    // The JSON serialization adds a " character to the beginning and end of the response, remove
    // those if they are present.
    std::string master_addrs = buf.ToString();
    if (master_addrs.at(0) == '"') {
      master_addrs = master_addrs.erase(0, 1);
    }
    if (master_addrs.at(master_addrs.size() - 1) == '"') {
      master_addrs = master_addrs.erase(master_addrs.size() - 1, 1);
    }
    master_server_addrs_.clear();
    master_server_addrs_.push_back(master_addrs);
    LOG(INFO) << "Got master addresses = " << master_addrs
              << " from REST endpoint: " << master_server_endpoint_;
  } else if (!FLAGS_flagfile.empty() && !skip_master_flagfile_) {
    LOG(INFO) << "Reinitialize master addresses from file: " << FLAGS_flagfile;
    string master_addrs = VERIFY_RESULT(ReadMasterAddressesFromFlagFile(FLAGS_flagfile));

    if (master_addrs.empty()) {
      return STATUS_SUBSTITUTE(IllegalState, "Couldn't find flag $0 in flagfile $1",
                               FLAGS_tserver_master_addrs, FLAGS_flagfile);
    }
    master_server_addrs_.clear();
    master_server_addrs_.push_back(master_addrs);
    LOG(INFO) << "New master addresses: " << master_addrs;
  } else {
    VLOG(1) << "Skipping reinitialize of master addresses, no REST endpoint or file specified";
  }
  return Status::OK();
}

// Remove a given master from the list of master_server_addrs_.
Status YBClient::Data::RemoveMasterAddress(const HostPort& addr) {

  {
    auto str = addr.ToString();
    std::lock_guard<simple_spinlock> l(master_server_addrs_lock_);
    auto it = std::find(master_server_addrs_.begin(), master_server_addrs_.end(), str);
    if (it != master_server_addrs_.end()) {
      master_server_addrs_.erase(it);
    }
  }

  return Status::OK();
}

Status YBClient::Data::SetReplicationInfo(
    YBClient* client, const master::ReplicationInfoPB& replication_info, CoarseTimePoint deadline,
    bool* retry) {
  // If retry was not set, we'll wrap around in a retryable function.
  if (!retry) {
    return RetryFunc(
        deadline, "Other clients changed the config. Retrying.",
        "Timed out retrying the config change. Probably too many concurrent attempts.",
        std::bind(&YBClient::Data::SetReplicationInfo, this, client, replication_info, _1, _2));
  }

  // Get the current config.
  GetMasterClusterConfigRequestPB get_req;
  GetMasterClusterConfigResponsePB get_resp;
  Status s = SyncLeaderMasterRpc<GetMasterClusterConfigRequestPB, GetMasterClusterConfigResponsePB>(
      deadline, client, get_req, &get_resp, nullptr /* num_attempts */, "GetMasterClusterConfig",
      &MasterServiceProxy::GetMasterClusterConfig);
  RETURN_NOT_OK(s);
  if (get_resp.has_error()) {
    return StatusFromPB(get_resp.error().status());
  }

  ChangeMasterClusterConfigRequestPB change_req;
  ChangeMasterClusterConfigResponsePB change_resp;

  // Update the list with the new replication info.
  change_req.mutable_cluster_config()->CopyFrom(get_resp.cluster_config());
  auto new_ri = change_req.mutable_cluster_config()->mutable_replication_info();
  new_ri->CopyFrom(replication_info);

  // Try to update it on the live cluster.
  s = SyncLeaderMasterRpc<ChangeMasterClusterConfigRequestPB, ChangeMasterClusterConfigResponsePB>(
      deadline, client, change_req, &change_resp, nullptr /* num_attempts */,
      "ChangeMasterClusterConfig", &MasterServiceProxy::ChangeMasterClusterConfig);
  RETURN_NOT_OK(s);
  if (change_resp.has_error()) {
    // Retry on config mismatch.
    *retry = change_resp.error().code() == MasterErrorPB::CONFIG_VERSION_MISMATCH;
    return StatusFromPB(change_resp.error().status());
  }
  *retry = false;
  return Status::OK();
}

HostPort YBClient::Data::leader_master_hostport() const {
  std::lock_guard<simple_spinlock> l(leader_master_lock_);
  return leader_master_hostport_;
}

shared_ptr<master::MasterServiceProxy> YBClient::Data::master_proxy() const {
  std::lock_guard<simple_spinlock> l(leader_master_lock_);
  return master_proxy_;
}

uint64_t YBClient::Data::GetLatestObservedHybridTime() const {
  return latest_observed_hybrid_time_.Load();
}

void YBClient::Data::UpdateLatestObservedHybridTime(uint64_t hybrid_time) {
  latest_observed_hybrid_time_.StoreMax(hybrid_time);
}

} // namespace client
} // namespace yb

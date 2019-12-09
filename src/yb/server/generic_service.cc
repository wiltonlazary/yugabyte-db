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

#include "yb/server/generic_service.h"

#include <string>
#include <unordered_set>

#include <gflags/gflags.h>

#include "yb/gutil/map-util.h"
#include "yb/rpc/rpc_context.h"
#include "yb/server/clock.h"
#include "yb/server/hybrid_clock.h"
#include "yb/server/server_base.h"
#include "yb/util/flag_tags.h"

using std::string;
using std::unordered_set;

#ifdef COVERAGE_BUILD
extern "C" void __gcov_flush(void);
#endif


namespace yb {
namespace server {

GenericServiceImpl::GenericServiceImpl(RpcServerBase* server)
  : GenericServiceIf(server->metric_entity()),
    server_(server) {
}

GenericServiceImpl::~GenericServiceImpl() {
}

void GenericServiceImpl::SetFlag(const SetFlagRequestPB* req,
                                 SetFlagResponsePB* resp,
                                 rpc::RpcContext rpc) {

  // Validate that the flag exists and get the current value.
  string old_val;
  if (!google::GetCommandLineOption(req->flag().c_str(),
                                    &old_val)) {
    resp->set_result(SetFlagResponsePB::NO_SUCH_FLAG);
    rpc.RespondSuccess();
    return;
  }

  // Validate that the flag is runtime-changeable.
  unordered_set<string> tags;
  GetFlagTags(req->flag(), &tags);
  if (!ContainsKey(tags, "runtime")) {
    if (req->force()) {
      LOG(WARNING) << rpc.requestor_string() << " forcing change of "
                   << "non-runtime-safe flag " << req->flag();
    } else {
      resp->set_result(SetFlagResponsePB::NOT_SAFE);
      resp->set_msg("Flag is not safe to change at runtime");
      rpc.RespondSuccess();
      return;
    }
  }

  resp->set_old_value(old_val);

  // The gflags library sets new values of flags without synchronization.
  // TODO: patch gflags to use proper synchronization.
  ANNOTATE_IGNORE_WRITES_BEGIN();
  // Try to set the new value.
  string ret = google::SetCommandLineOption(
      req->flag().c_str(),
      req->value().c_str());
  ANNOTATE_IGNORE_WRITES_END();

  if (ret.empty()) {
    resp->set_result(SetFlagResponsePB::BAD_VALUE);
    resp->set_msg("Unable to set flag: bad value");
  } else {
    LOG(INFO) << rpc.requestor_string() << " changed flags via RPC: "
              << req->flag() << " from '" << old_val << "' to '"
              << req->value() << "'";
    resp->set_result(SetFlagResponsePB::SUCCESS);
    resp->set_msg(ret);
  }

  rpc.RespondSuccess();
}

void GenericServiceImpl::GetFlag(const GetFlagRequestPB* req,
                                 GetFlagResponsePB* resp,
                                 rpc::RpcContext rpc) {
  // Validate that the flag exists and get the current value.
  string val;
  if (!google::GetCommandLineOption(req->flag().c_str(), &val)) {
    resp->set_valid(false);
    rpc.RespondSuccess();
    return;
  }
  resp->set_value(val);
  rpc.RespondSuccess();
}

void GenericServiceImpl::FlushCoverage(const FlushCoverageRequestPB* req,
                                       FlushCoverageResponsePB* resp,
                                       rpc::RpcContext rpc) {
#ifdef COVERAGE_BUILD
  __gcov_flush();
  LOG(INFO) << "Flushed coverage info. (request from " << rpc.requestor_string() << ")";
  resp->set_success(true);
#else
  LOG(WARNING) << "Non-coverage build cannot flush coverage (request from "
               << rpc.requestor_string() << ")";
  resp->set_success(false);
#endif
  rpc.RespondSuccess();
}

void GenericServiceImpl::ServerClock(const ServerClockRequestPB* req,
                                     ServerClockResponsePB* resp,
                                     rpc::RpcContext rpc) {
  resp->set_hybrid_time(server_->clock()->Now().ToUint64());
  rpc.RespondSuccess();
}

void GenericServiceImpl::GetStatus(const GetStatusRequestPB* req,
                                   GetStatusResponsePB* resp,
                                   rpc::RpcContext rpc) {
  server_->GetStatusPB(resp->mutable_status());
  rpc.RespondSuccess();
}

void GenericServiceImpl::Ping(
    const PingRequestPB* req, PingResponsePB* resp, rpc::RpcContext rpc) {
  rpc.RespondSuccess();
}

} // namespace server
} // namespace yb

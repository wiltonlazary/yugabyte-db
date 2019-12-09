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

#include "yb/tserver/backup_service.h"

#include "yb/util/debug/trace_event.h"
#include "yb/common/wire_protocol.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/operations/snapshot_operation.h"

#include "yb/tserver/service_util.h"
#include "yb/tserver/ts_tablet_manager.h"

namespace yb {
namespace tserver {

using rpc::RpcContext;
using tablet::SnapshotOperationState;
using tablet::OperationCompletionCallback;
using tablet::enterprise::Tablet;

TabletServiceBackupImpl::TabletServiceBackupImpl(TSTabletManager* tablet_manager,
                                                 const scoped_refptr<MetricEntity>& metric_entity)
    : TabletServerBackupServiceIf(metric_entity),
      tablet_manager_(tablet_manager) {
}

void TabletServiceBackupImpl::TabletSnapshotOp(const TabletSnapshotOpRequestPB* req,
                                               TabletSnapshotOpResponsePB* resp,
                                               RpcContext context) {
  if (!CheckUuidMatchOrRespond(tablet_manager_, "TabletSnapshotOp", req, resp, &context)) {
    return;
  }

  if (!req->has_tablet_id()) {
    auto status = STATUS(InvalidArgument, "Tablet id missing");
    SetupErrorAndRespond(
        resp->mutable_error(), status, TabletServerErrorPB_Code_UNKNOWN_ERROR, &context);
    return;
  }

  server::UpdateClock(*req, tablet_manager_->server()->Clock());

  TRACE_EVENT1("tserver", "TabletSnapshotOp",
               "tablet_id: ", req->tablet_id());

  LOG(INFO) << "Processing TabletSnapshotOp for tablet " << req->tablet_id()
            << " from " << context.requestor_string();
  VLOG(1) << "Full request: " << req->DebugString();

  auto tablet = LookupLeaderTabletOrRespond(tablet_manager_, req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  auto tx_state = std::make_unique<SnapshotOperationState>(tablet.peer->tablet(), req);

  auto clock = tablet_manager_->server()->Clock();
  tx_state->set_completion_callback(
      MakeRpcOperationCompletionCallback(std::move(context), resp, clock));

  // Submit the create snapshot op. The RPC will be responded to asynchronously.
  tablet.peer->Submit(
      std::make_unique<tablet::SnapshotOperation>(std::move(tx_state)), tablet.leader_term);
}

}  // namespace tserver
}  // namespace yb

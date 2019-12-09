//
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
//

#ifndef YB_RPC_RPC_WITH_CALL_ID_H
#define YB_RPC_RPC_WITH_CALL_ID_H

#include <functional>
#include <unordered_map>

#include "yb/rpc/connection_context.h"
#include "yb/rpc/inbound_call.h"

namespace yb {
namespace rpc {

class ConnectionContextWithCallId : public ConnectionContextBase {
 protected:
  ConnectionContextWithCallId();

  InboundCall::CallProcessedListener call_processed_listener() {
    return std::bind(&ConnectionContextWithCallId::CallProcessed, this, std::placeholders::_1);
  }

  CHECKED_STATUS Store(InboundCall* call);
  void DumpPB(const DumpRunningRpcsRequestPB& req, RpcConnectionPB* resp) override;

  uint64_t ProcessedCallCount() override {
    return processed_call_count_.load(std::memory_order_acquire);
  }

 private:
  virtual uint64_t ExtractCallId(InboundCall* call) = 0;
  void ListenIdle(IdleListener listener) override { idle_listener_ = std::move(listener); }
  void Shutdown(const Status& status) override;

  bool Idle(std::string* reason_not_idle = nullptr) override;

  void CallProcessed(InboundCall* call);
  void QueueResponse(const ConnectionPtr& conn, InboundCallPtr call) override;

  // Calls which have been received on the server and are currently
  // being handled.
  std::unordered_map<uint64_t, InboundCall*> calls_being_handled_;
  std::atomic<uint64_t> processed_call_count_{0};
  IdleListener idle_listener_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_RPC_WITH_CALL_ID_H

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

#include "yb/rpc/rpc_context.h"

#include <ostream>
#include <sstream>

#include <boost/core/null_deleter.hpp>

#include "yb/rpc/connection.h"
#include "yb/rpc/inbound_call.h"
#include "yb/rpc/local_call.h"
#include "yb/rpc/outbound_call.h"
#include "yb/rpc/service_if.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/yb_rpc.h"

#include "yb/util/hdr_histogram.h"
#include "yb/util/metrics.h"
#include "yb/util/trace.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/pb_util.h"

using google::protobuf::Message;
DECLARE_int32(rpc_max_message_size);

namespace yb {
namespace rpc {

using std::shared_ptr;

namespace {

// Wrapper for a protobuf message which lazily converts to JSON when
// the trace buffer is dumped. This pushes the work of stringification
// to the trace dumping process.
class PbTracer : public debug::ConvertableToTraceFormat {
 public:
  enum {
    kMaxFieldLengthToTrace = 100
  };

  explicit PbTracer(const Message& msg) : msg_(msg.New()) {
    msg_->CopyFrom(msg);
  }

  void AppendAsTraceFormat(std::string* out) const override {
    pb_util::TruncateFields(msg_.get(), kMaxFieldLengthToTrace);
    std::stringstream ss;
    JsonWriter jw(&ss, JsonWriter::COMPACT);
    jw.Protobuf(*msg_);
    out->append(ss.str());
  }
 private:
  const gscoped_ptr<Message> msg_;
};

scoped_refptr<debug::ConvertableToTraceFormat> TracePb(const Message& msg) {
  return make_scoped_refptr(new PbTracer(msg));
}
}  // anonymous namespace

RpcContext::~RpcContext() {
  if (call_ && !responded_) {
    LOG(DFATAL) << "RpcContext is destroyed, but response has not been sent, for call: "
                << call_->ToString();
  }
}

RpcContext::RpcContext(std::shared_ptr<YBInboundCall> call,
                       std::shared_ptr<google::protobuf::Message> request_pb,
                       std::shared_ptr<google::protobuf::Message> response_pb,
                       RpcMethodMetrics metrics)
    : call_(std::move(call)),
      request_pb_(request_pb),
      response_pb_(std::move(response_pb)),
      metrics_(metrics) {
  const Status s = call_->ParseParam(request_pb.get());
  if (PREDICT_FALSE(!s.ok())) {
    RespondRpcFailure(ErrorStatusPB::ERROR_INVALID_REQUEST, s);
    return;
  }
  TRACE_EVENT_ASYNC_BEGIN2("rpc_call", "RPC", this,
                           "call", call_->ToString(),
                           "request", TracePb(*request_pb_));
}

RpcContext::RpcContext(std::shared_ptr<LocalYBInboundCall> call,
                       RpcMethodMetrics metrics)
    : call_(call),
      request_pb_(call->request(), boost::null_deleter()),
      response_pb_(call->response(), boost::null_deleter()),
      metrics_(metrics) {
  TRACE_EVENT_ASYNC_BEGIN2("rpc_call", "RPC", this,
                           "call", call_->ToString(),
                           "request", TracePb(*request_pb_));
}

void RpcContext::RespondSuccess() {
  if (response_pb_->ByteSize() > FLAGS_rpc_max_message_size) {
    RespondFailure(STATUS_FORMAT(InvalidArgument, "RPC message too long: $0 vs $1",
                                 response_pb_->ByteSize(), FLAGS_rpc_max_message_size));
    return;
  }
  call_->RecordHandlingCompleted(metrics_.handler_latency);
  TRACE_EVENT_ASYNC_END2("rpc_call", "RPC", this,
                         "response", TracePb(*response_pb_),
                         "trace", trace()->DumpToString(true));
  call_->RespondSuccess(*response_pb_);
  responded_ = true;
}

void RpcContext::RespondFailure(const Status &status) {
  call_->RecordHandlingCompleted(metrics_.handler_latency);
  TRACE_EVENT_ASYNC_END2("rpc_call", "RPC", this,
                         "status", status.ToString(),
                         "trace", trace()->DumpToString(true));
  call_->RespondFailure(ErrorStatusPB::ERROR_APPLICATION, status);
  responded_ = true;
}

void RpcContext::RespondRpcFailure(ErrorStatusPB_RpcErrorCodePB err, const Status& status) {
  call_->RecordHandlingCompleted(metrics_.handler_latency);
  TRACE_EVENT_ASYNC_END2("rpc_call", "RPC", this,
                         "status", status.ToString(),
                         "trace", trace()->DumpToString(true));
  call_->RespondFailure(err, status);
  responded_ = true;
}

void RpcContext::RespondApplicationError(int error_ext_id, const std::string& message,
                                         const Message& app_error_pb) {
  call_->RecordHandlingCompleted(metrics_.handler_latency);
  TRACE_EVENT_ASYNC_END2("rpc_call", "RPC", this,
                         "response", TracePb(app_error_pb),
                         "trace", trace()->DumpToString(true));
  call_->RespondApplicationError(error_ext_id, message, app_error_pb);
  responded_ = true;
}

Status RpcContext::AddRpcSidecar(RefCntBuffer car, int* idx) {
  return call_->AddRpcSidecar(car, idx);
}

int RpcContext::RpcSidecarsSize() const {
  return call_->RpcSidecarsSize();
}

const RefCntBuffer& RpcContext::RpcSidecar(int idx) const {
  return call_->RpcSidecar(idx);
}

void RpcContext::ResetRpcSidecars() {
  call_->ResetRpcSidecars();
}

const Endpoint& RpcContext::remote_address() const {
  return call_->remote_address();
}

const Endpoint& RpcContext::local_address() const {
  return call_->local_address();
}

std::string RpcContext::requestor_string() const {
  return yb::ToString(call_->remote_address());
}

CoarseTimePoint RpcContext::GetClientDeadline() const {
  return call_->GetClientDeadline();
}

Trace* RpcContext::trace() {
  return call_->trace();
}

void RpcContext::Panic(const char* filepath, int line_number, const string& message) {
  // Use the LogMessage class directly so that the log messages appear to come from
  // the line of code which caused the panic, not this code.
#define MY_ERROR google::LogMessage(filepath, line_number, google::GLOG_ERROR).stream()
#define MY_FATAL google::LogMessageFatal(filepath, line_number).stream()

  MY_ERROR << "Panic handling " << call_->ToString() << ": " << message;
  MY_ERROR << "Request:\n" << request_pb_->DebugString();
  Trace* t = trace();
  if (t) {
    MY_ERROR << "RPC trace:";
    t->Dump(&MY_ERROR, true);
  }
  MY_FATAL << "Exiting due to panic.";

#undef MY_ERROR
#undef MY_FATAL
}

void RpcContext::CloseConnection() {
  auto connection = call_->connection();
  connection->reactor()->ScheduleReactorFunctor([connection](Reactor*) {
    connection->Close();
  }, SOURCE_LOCATION());
}

std::string RpcContext::ToString() const {
  return call_->ToString();
}

void PanicRpc(RpcContext* context, const char* file, int line_number, const std::string& message) {
  if (context) {
    context->Panic(file, line_number, message);
  } else {
    google::LogMessage(file, line_number, google::GLOG_FATAL).stream() << message;
  }
}

}  // namespace rpc
}  // namespace yb

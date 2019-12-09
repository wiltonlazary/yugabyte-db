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
#ifndef YB_YQL_CQL_CQLSERVER_CQL_RPC_H
#define YB_YQL_CQL_CQLSERVER_CQL_RPC_H

#include <atomic>

#include "yb/yql/cql/cqlserver/cql_message.h"

#include "yb/rpc/binary_call_parser.h"
#include "yb/rpc/circular_read_buffer.h"
#include "yb/rpc/rpc_with_call_id.h"
#include "yb/rpc/server_event.h"

#include "yb/yql/cql/ql/ql_session.h"

namespace yb {
namespace cqlserver {

class CQLStatement;
class CQLServiceImpl;

class CQLConnectionContext : public rpc::ConnectionContextWithCallId,
                             public rpc::BinaryCallParserListener {
 public:
  CQLConnectionContext(size_t receive_buffer_size, const MemTrackerPtr& buffer_tracker,
                       const MemTrackerPtr& call_tracker);

  void DumpPB(const rpc::DumpRunningRpcsRequestPB& req,
              rpc::RpcConnectionPB* resp) override;

  // Accessor methods for CQL message compression scheme to use.
  CQLMessage::CompressionScheme compression_scheme() const {
    return compression_scheme_;
  }
  void set_compression_scheme(CQLMessage::CompressionScheme compression_scheme) {
    compression_scheme_ = compression_scheme;
  }

  // Accessor methods for registered CQL events.
  CQLMessage::Events registered_events() const {
    return registered_events_;
  }
  void add_registered_events(CQLMessage::Events events) {
    registered_events_ |= events;
  }

  static std::string Name() { return "CQL"; }

 private:
  void Connected(const rpc::ConnectionPtr& connection) override {}

  rpc::RpcConnectionPB::StateType State() override {
    return rpc::RpcConnectionPB::OPEN;
  }

  uint64_t ExtractCallId(rpc::InboundCall* call) override;
  Result<rpc::ProcessDataResult> ProcessCalls(const rpc::ConnectionPtr& connection,
                                              const IoVecs& bytes_to_process,
                                              rpc::ReadBufferFull read_buffer_full) override;
  // Takes ownership of call_data content.
  CHECKED_STATUS HandleCall(
      const rpc::ConnectionPtr& connection, rpc::CallData* call_data) override;

  rpc::StreamReadBuffer& ReadBuffer() override {
    return read_buffer_;
  }

  // SQL session of this CQL client connection.
  ql::QLSession::SharedPtr ql_session_;

  // CQL message compression scheme to use.
  CQLMessage::CompressionScheme compression_scheme_ = CQLMessage::CompressionScheme::kNone;

  // Stored registered events for the connection.
  CQLMessage::Events registered_events_ = CQLMessage::kNoEvents;

  rpc::BinaryCallParser parser_;

  rpc::CircularReadBuffer read_buffer_;

  MemTrackerPtr call_tracker_;
};

class CQLInboundCall : public rpc::InboundCall {
 public:
  explicit CQLInboundCall(rpc::ConnectionPtr conn,
                          CallProcessedListener call_processed_listener,
                          ql::QLSession::SharedPtr ql_session);

  // Takes ownership of call_data content.
  CHECKED_STATUS ParseFrom(const MemTrackerPtr& call_tracker, rpc::CallData* call_data);

  // Serialize the response packet for the finished call.
  // The resulting slices refer to memory in this object.
  void Serialize(boost::container::small_vector_base<RefCntBuffer>* output) override;

  void LogTrace() const override;
  std::string ToString() const override;
  bool DumpPB(const rpc::DumpRunningRpcsRequestPB& req, rpc::RpcCallInProgressPB* resp) override;

  CoarseTimePoint GetClientDeadline() const override;

  // Return the response message buffer.
  RefCntBuffer& response_msg_buf() {
    return response_msg_buf_;
  }

  // Return the SQL session of this CQL call.
  const ql::QLSession::SharedPtr& ql_session() const {
    return ql_session_;
  }

  uint16_t stream_id() const { return stream_id_; }

  const std::string& service_name() const override;
  const std::string& method_name() const override;
  void RespondFailure(rpc::ErrorStatusPB::RpcErrorCodePB error_code, const Status& status) override;
  void RespondSuccess(const RefCntBuffer& buffer, const yb::rpc::RpcMethodMetrics& metrics);
  void GetCallDetails(rpc::RpcCallInProgressPB *call_in_progress_pb) const;
  void SetRequest(std::shared_ptr<const CQLRequest> request, CQLServiceImpl* service_impl) {
    service_impl_ = service_impl;
#ifdef THREAD_SANITIZER
    request_ = request;
#else
    std::atomic_store_explicit(&request_, request, std::memory_order_release);
#endif
  }

 private:
  RefCntBuffer response_msg_buf_;
  const ql::QLSession::SharedPtr ql_session_;
  uint16_t stream_id_;
  std::shared_ptr<const CQLRequest> request_;
  // Pointer to the containing CQL service implementation.
  CQLServiceImpl* service_impl_;

  ScopedTrackedConsumption consumption_;
};

using CQLInboundCallPtr = std::shared_ptr<CQLInboundCall>;

} // namespace cqlserver
} // namespace yb

#endif // YB_YQL_CQL_CQLSERVER_CQL_RPC_H

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

#include "yb/rpc/serialization.h"

#include <google/protobuf/message_lite.h>
#include <google/protobuf/io/coded_stream.h>
#include <glog/logging.h>

#include "yb/gutil/endian.h"
#include "yb/gutil/stringprintf.h"
#include "yb/rpc/constants.h"
#include "yb/util/faststring.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

DECLARE_int32(rpc_max_message_size);

using google::protobuf::MessageLite;
using google::protobuf::io::CodedInputStream;
using google::protobuf::io::CodedOutputStream;

namespace yb {
namespace rpc {
namespace serialization {

Status SerializeMessage(const MessageLite& message,
                        RefCntBuffer* param_buf,
                        int additional_size,
                        bool use_cached_size,
                        size_t offset,
                        size_t* size) {

  if (PREDICT_FALSE(!message.IsInitialized())) {
    return STATUS(InvalidArgument, "RPC argument missing required fields",
        message.InitializationErrorString());
  }
  int pb_size = use_cached_size ? message.GetCachedSize() : message.ByteSize();
  DCHECK_EQ(message.ByteSize(), pb_size);
  int recorded_size = pb_size + additional_size;
  int size_with_delim = pb_size + CodedOutputStream::VarintSize32(recorded_size);
  int total_size = size_with_delim + additional_size;

  if (total_size > FLAGS_rpc_max_message_size) {
    LOG(DFATAL) << "Sending too long of an RPC message (" << total_size
                << " bytes)";
  }

  if (size != nullptr) {
    *size = offset + size_with_delim;
  }
  if (param_buf != nullptr) {
    if (!*param_buf) {
      *param_buf = RefCntBuffer(offset + size_with_delim);
    } else {
      CHECK_EQ(param_buf->size(), offset + size_with_delim) << "offset = " << offset;
    }
    uint8_t *dst = param_buf->udata() + offset;
    dst = CodedOutputStream::WriteVarint32ToArray(recorded_size, dst);
    dst = message.SerializeWithCachedSizesToArray(dst);
    CHECK_EQ(dst, param_buf->udata() + param_buf->size());
  }

  return Status::OK();
}

Status SerializeHeader(const MessageLite& header,
                       size_t param_len,
                       RefCntBuffer* header_buf,
                       size_t reserve_for_param,
                       size_t* header_size) {

  if (PREDICT_FALSE(!header.IsInitialized())) {
    LOG(DFATAL) << "Uninitialized RPC header";
    return STATUS(InvalidArgument, "RPC header missing required fields",
                                  header.InitializationErrorString());
  }

  // Compute all the lengths for the packet.
  size_t header_pb_len = header.ByteSize();
  size_t header_tot_len = kMsgLengthPrefixLength        // Int prefix for the total length.
      + CodedOutputStream::VarintSize32(header_pb_len)  // Varint delimiter for header PB.
      + header_pb_len;                                  // Length for the header PB itself.
  size_t total_size = header_tot_len + param_len;

  *header_buf = RefCntBuffer(header_tot_len + reserve_for_param);
  if (header_size != nullptr) {
    *header_size = header_tot_len;
  }
  uint8_t* dst = header_buf->udata();

  // 1. The length for the whole request, not including the 4-byte
  // length prefix.
  NetworkByteOrder::Store32(dst, total_size - kMsgLengthPrefixLength);
  dst += sizeof(uint32_t);

  // 2. The varint-prefixed RequestHeader PB
  dst = CodedOutputStream::WriteVarint32ToArray(header_pb_len, dst);
  dst = header.SerializeWithCachedSizesToArray(dst);

  // We should have used the whole buffer we allocated.
  CHECK_EQ(dst, header_buf->udata() + header_tot_len);

  return Status::OK();
}

Status ParseYBMessage(const Slice& buf,
                      MessageLite* parsed_header,
                      Slice* parsed_main_message) {
  CodedInputStream in(buf.data(), buf.size());
  in.SetTotalBytesLimit(FLAGS_rpc_max_message_size, FLAGS_rpc_max_message_size*3/4);

  uint32_t header_len;
  if (PREDICT_FALSE(!in.ReadVarint32(&header_len))) {
    return STATUS(Corruption, "Invalid packet: missing header delimiter",
                              buf.ToDebugString());
  }

  CodedInputStream::Limit l;
  l = in.PushLimit(header_len);
  if (PREDICT_FALSE(!parsed_header->ParseFromCodedStream(&in))) {
    return STATUS(Corruption, "Invalid packet: header too short",
                              buf.ToDebugString());
  }
  in.PopLimit(l);

  uint32_t main_msg_len;
  if (PREDICT_FALSE(!in.ReadVarint32(&main_msg_len))) {
    return STATUS(Corruption, "Invalid packet: missing main msg length",
                              buf.ToDebugString());
  }

  if (PREDICT_FALSE(!in.Skip(main_msg_len))) {
    return STATUS(Corruption,
        StringPrintf("Invalid packet: data too short, expected %d byte main_msg", main_msg_len),
        buf.ToDebugString());
  }

  if (PREDICT_FALSE(in.BytesUntilLimit() > 0)) {
    return STATUS(Corruption,
      StringPrintf("Invalid packet: %d extra bytes at end of packet", in.BytesUntilLimit()),
      buf.ToDebugString());
  }

  *parsed_main_message = Slice(buf.data() + buf.size() - main_msg_len,
                              main_msg_len);
  return Status::OK();
}

}  // namespace serialization
}  // namespace rpc
}  // namespace yb

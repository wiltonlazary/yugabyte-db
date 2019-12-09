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
package org.yb.client;

import com.google.protobuf.ByteString;
import com.google.protobuf.Message;
import org.yb.annotations.InterfaceAudience;
import org.yb.master.Master;
import org.yb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;

/**
 * Package-private RPC that can only go to a master.
 */
@InterfaceAudience.Private
class IsInitDbDoneRequest extends YRpc<IsInitDbDoneResponse> {

  IsInitDbDoneRequest(YBTable masterTable) {
    super(masterTable);
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "IsInitDbDone";
  }

  @Override
  Pair<IsInitDbDoneResponse, Object> deserialize(
      CallResponse callResponse,
      String masterUUID) throws Exception {
    final Master.IsInitDbDoneResponsePB.Builder respBuilder =
        Master.IsInitDbDoneResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    IsInitDbDoneResponse response =
        new IsInitDbDoneResponse(
            deadlineTracker.getElapsedMillis(),
            masterUUID,
            hasErr ? false : respBuilder.getPgProcExists(),
            hasErr ? false : respBuilder.getDone(),
            hasErr ? "" : respBuilder.getInitdbError(),
            hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<IsInitDbDoneResponse, Object>(
        response,
        hasErr ? respBuilder.getError() : null);
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.IsInitDbDoneRequestPB.Builder builder = Master
        .IsInitDbDoneRequestPB.newBuilder();
    return toChannelBuffer(header, builder.build());
  }
}

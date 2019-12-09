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
import org.jboss.netty.buffer.ChannelBuffer;
import org.yb.util.Pair;

import org.yb.master.Master;

public class HasUniverseKeyInMemoryRequest extends YRpc<HasUniverseKeyInMemoryResponse> {
  private String universeKeyId;

  public HasUniverseKeyInMemoryRequest(String universeKeyId) {
    super(null);
    this.universeKeyId = universeKeyId;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.HasUniverseKeyInMemoryRequestPB.Builder builder =
            Master.HasUniverseKeyInMemoryRequestPB.newBuilder();
    builder.setVersionId(universeKeyId);
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "HasUniverseKeyInMemory";
  }

  @Override
  Pair<HasUniverseKeyInMemoryResponse, Object> deserialize(
          CallResponse callResponse, String uuid) throws Exception {
    final Master.HasUniverseKeyInMemoryResponsePB.Builder respBuilder =
            Master.HasUniverseKeyInMemoryResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    Master.MasterErrorPB serverError = respBuilder.hasError() ? respBuilder.getError() : null;
    HasUniverseKeyInMemoryResponse response = new HasUniverseKeyInMemoryResponse(
            deadlineTracker.getElapsedMillis(), uuid, serverError, respBuilder.getHasKey());
    return new Pair<HasUniverseKeyInMemoryResponse, Object>(response, serverError);
  }
}

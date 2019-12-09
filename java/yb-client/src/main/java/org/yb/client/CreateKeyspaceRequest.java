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

import org.yb.annotations.InterfaceAudience;
import org.yb.Common.HostPortPB;
import org.yb.Common.YQLDatabase;
import org.yb.consensus.Consensus;
import org.yb.consensus.Metadata;
import org.yb.consensus.Metadata.RaftPeerPB;
import org.yb.master.Master;
import org.yb.util.Pair;

import java.util.ArrayList;
import java.util.List;

@InterfaceAudience.Public
class CreateKeyspaceRequest extends YRpc<CreateKeyspaceResponse> {
  private String name;
  private YQLDatabase databaseType;

  public CreateKeyspaceRequest(YBTable masterTable, String name) {
    super(masterTable);
    this.name = name;
  }

  public CreateKeyspaceRequest(YBTable masterTable, String name, YQLDatabase databaseType) {
    super(masterTable);
    this.name = name;
    this.databaseType = databaseType;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.CreateNamespaceRequestPB.Builder builder =
      Master.CreateNamespaceRequestPB.newBuilder();
    builder.setName(this.name);
    if (this.databaseType != null)
      builder.setDatabaseType(this.databaseType);

    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "CreateNamespace";
  }

  @Override
  Pair<CreateKeyspaceResponse, Object> deserialize(CallResponse callResponse,
                                                   String masterUUID) throws Exception {
    final Master.CreateNamespaceResponsePB.Builder respBuilder =
        Master.CreateNamespaceResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    boolean hasErr = respBuilder.hasError();
    CreateKeyspaceResponse response =
        new CreateKeyspaceResponse(deadlineTracker.getElapsedMillis(), masterUUID,
                                   hasErr ? respBuilder.getErrorBuilder().build() : null);
    return new Pair<CreateKeyspaceResponse, Object>(response,
                                                    hasErr ? respBuilder.getError() : null);
  }
}

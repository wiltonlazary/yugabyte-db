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
import java.util.Map;
import java.util.Iterator;
import org.yb.util.Pair;
import org.yb.Encryption;

import org.yb.master.Master;

public class AddUniverseKeysRequest extends YRpc<AddUniverseKeysResponse> {
  private Map<String, byte[]> universeKeys;

  public AddUniverseKeysRequest(Map<String, byte[]> universeKeys) {
    super(null);
    this.universeKeys = universeKeys;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.AddUniverseKeysRequestPB.Builder builder =
            Master.AddUniverseKeysRequestPB.newBuilder();
    Encryption.UniverseKeysPB.Builder keysBuilder =  Encryption.UniverseKeysPB.newBuilder();
    Iterator iter = universeKeys.entrySet().iterator();
    while (iter.hasNext()) {
      Map.Entry<String, byte[]> entry = (Map.Entry)iter.next();
      keysBuilder.putMap(entry.getKey(), ByteString.copyFrom(entry.getValue()));
    }
    builder.setUniverseKeys(keysBuilder.build());
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "AddUniverseKeys";
  }

  @Override
  Pair<AddUniverseKeysResponse, Object> deserialize(
          CallResponse callResponse, String uuid) throws Exception {
    final Master.AddUniverseKeysResponsePB.Builder respBuilder =
            Master.AddUniverseKeysResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    Master.MasterErrorPB serverError = respBuilder.hasError() ? respBuilder.getError() : null;
    AddUniverseKeysResponse response = new AddUniverseKeysResponse(
            deadlineTracker.getElapsedMillis(), uuid, serverError);
    return new Pair<AddUniverseKeysResponse, Object>(response, serverError);
  }
}

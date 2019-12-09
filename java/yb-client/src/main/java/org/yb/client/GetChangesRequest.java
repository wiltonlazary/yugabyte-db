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
import org.yb.Opid;
import org.yb.cdc.CdcService;
import org.yb.util.Pair;
import org.yb.cdc.CdcService.GetChangesRequestPB;
import org.yb.cdc.CdcService.GetChangesResponsePB;

public class GetChangesRequest extends YRpc<GetChangesResponse> {
  private final String streamId;
  private final String tabletId;
  private final long term;
  private final long index;

  public GetChangesRequest(YBTable table, String streamId, String tabletId, long term, long index) {
    super(table);
    this.streamId = streamId;
    this.tabletId = tabletId;
    this.term = term;
    this.index = index;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final GetChangesRequestPB.Builder builder = GetChangesRequestPB.newBuilder();
    builder.setStreamId(ByteString.copyFromUtf8(this.streamId));
    builder.setTabletId(ByteString.copyFromUtf8(this.tabletId));
    if (term != 0 || index != 0) {
      CdcService.CDCCheckpointPB.Builder checkpointBuilder =
              CdcService.CDCCheckpointPB.newBuilder();
      checkpointBuilder.setOpId(Opid.OpIdPB.newBuilder().setIndex(this.index).setTerm(this.term));
      builder.setFromCheckpoint(checkpointBuilder);
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return CDC_SERVICE_NAME; }

  @Override
  String method() {
    return "GetChanges";
  }

  @Override
  Pair<GetChangesResponse, Object> deserialize(
          CallResponse callResponse, String uuid) throws Exception {
    final GetChangesResponsePB.Builder respBuilder = GetChangesResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    GetChangesResponse response = new GetChangesResponse(
            deadlineTracker.getElapsedMillis(), uuid, respBuilder.build());
    return new Pair<GetChangesResponse, Object>(
            response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}

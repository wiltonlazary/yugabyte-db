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
package org.yb.client;

import com.google.protobuf.Message;
import org.yb.annotations.InterfaceAudience;
import org.yb.Common.YQLDatabase;
import org.yb.master.Master;
import org.yb.util.Pair;
import org.jboss.netty.buffer.ChannelBuffer;

import java.util.ArrayList;
import java.util.List;

@InterfaceAudience.Private
class ListTablesRequest extends YRpc<ListTablesResponse> {

  private final String nameFilter;
  private final String namespace;
  private final boolean excludeSystemTables;

  ListTablesRequest(
      YBTable masterTable, String nameFilter, boolean excludeSystemTables, String namespace) {
    super(masterTable);
    this.nameFilter = nameFilter;
    this.excludeSystemTables = excludeSystemTables;
    this.namespace = namespace;
  }

  @Override
  ChannelBuffer serialize(Message header) {
    assert header.isInitialized();
    final Master.ListTablesRequestPB.Builder builder =
        Master.ListTablesRequestPB.newBuilder();
    if (nameFilter != null) {
      builder.setNameFilter(nameFilter);
    }
    if (excludeSystemTables) {
      builder.setExcludeSystemTables(excludeSystemTables);
    }
    if (namespace != null) {
      final Master.NamespaceIdentifierPB.Builder namespaceBuilder =
          Master.NamespaceIdentifierPB.newBuilder();
          namespaceBuilder.setName(namespace);
          builder.setNamespace(namespaceBuilder.build());
    }
    return toChannelBuffer(header, builder.build());
  }

  @Override
  String serviceName() { return MASTER_SERVICE_NAME; }

  @Override
  String method() {
    return "ListTables";
  }

  @Override
  Pair<ListTablesResponse, Object> deserialize(CallResponse callResponse,
                                               String tsUUID) throws Exception {
    final Master.ListTablesResponsePB.Builder respBuilder =
        Master.ListTablesResponsePB.newBuilder();
    readProtobuf(callResponse.getPBMessage(), respBuilder);
    ListTablesResponse response = new ListTablesResponse(deadlineTracker.getElapsedMillis(),
                                                         tsUUID, respBuilder.getTablesList());
    return new Pair<ListTablesResponse, Object>(
        response, respBuilder.hasError() ? respBuilder.getError() : null);
  }
}

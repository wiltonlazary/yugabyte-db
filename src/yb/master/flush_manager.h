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
#ifndef YB_MASTER_FLUSH_MANAGER_H
#define YB_MASTER_FLUSH_MANAGER_H

#include "yb/common/entity_ids.h"
#include "yb/master/master.pb.h"
#include "yb/util/locks.h"
#include "yb/util/status.h"
#include "yb/util/enums.h"

namespace yb {
namespace master {

class Master;
class CatalogManager;
class TableInfo;

// Handle Flush-related operations.
class FlushManager {
 public:
  explicit FlushManager(Master* master, CatalogManager* catalog_manager)
      : master_(DCHECK_NOTNULL(master)),
        catalog_manager_(DCHECK_NOTNULL(catalog_manager)) {}

  // API to start a table flushing.
  CHECKED_STATUS FlushTables(const FlushTablesRequestPB* req,
                             FlushTablesResponsePB* resp);

  CHECKED_STATUS IsFlushTablesDone(const IsFlushTablesDoneRequestPB* req,
                                   IsFlushTablesDoneResponsePB* resp);

  void HandleFlushTabletsResponse(const FlushRequestId& flush_id,
                                  const TabletServerId& ts_uuid,
                                  const Status& status);

 private:
  // Start the background task to send the FlushTablets RPC to the Tablet Server.
  void SendFlushTabletsRequest(const TabletServerId& ts_uuid,
                               const scoped_refptr<TableInfo>& table,
                               const std::vector<TabletId>& tablet_ids,
                               const FlushRequestId& flush_id,
                               bool is_compaction);

  void DeleteCompleteFlushRequests();

  Master* master_;
  CatalogManager* catalog_manager_;

  // Lock protecting the various in memory storage structures.
  typedef rw_spinlock LockType;
  mutable LockType lock_;

  typedef std::unordered_set<TabletServerId> TSIdSet;
  struct TSFlushingInfo {
    void clear() {
      ts_flushing_.clear();
      ts_succeed_.clear();
      ts_failed_.clear();
    }

    TSIdSet ts_flushing_;
    TSIdSet ts_succeed_;
    TSIdSet ts_failed_;
  };

  // Map of flushing requests: flush_request-id -> current per TS info.
  typedef std::unordered_map<FlushRequestId, TSFlushingInfo> FlushRequestMap;
  FlushRequestMap flush_requests_;

  DISALLOW_COPY_AND_ASSIGN(FlushManager);
};

} // namespace master
} // namespace yb
#endif // YB_MASTER_FLUSH_MANAGER_H

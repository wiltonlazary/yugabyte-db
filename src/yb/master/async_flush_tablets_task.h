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
#ifndef YB_MASTER_ASYNC_FLUSH_TABLETS_TASK_H
#define YB_MASTER_ASYNC_FLUSH_TABLETS_TASK_H

#include "yb/master/async_rpc_tasks.h"

namespace yb {
namespace master {

// Send the "Flush Tablets" request to the specified Tablet Server.
// Keeps retrying until we get an "ok" response.
class AsyncFlushTablets : public RetrySpecificTSRpcTask {
 public:
  AsyncFlushTablets(Master* master,
                    ThreadPool* callback_pool,
                    const TabletServerId& ts_uuid,
                    const scoped_refptr<TableInfo>& table,
                    const std::vector<TabletId>& tablet_ids,
                    const FlushRequestId& flush_id,
                    bool is_compaction);

  Type type() const override { return ASYNC_FLUSH_TABLETS; }

  std::string type_name() const override { return "Flush Tablets"; }

  std::string description() const override;

 private:
  TabletId tablet_id() const override { return TabletId(); }
  TabletServerId permanent_uuid() const;

  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  const std::vector<TabletId> tablet_ids_;
  const FlushRequestId flush_id_;
  tserver::FlushTabletsResponsePB resp_;
  bool is_compaction_ = false;
};

} // namespace master
} // namespace yb

#endif // YB_MASTER_ASYNC_FLUSH_TABLETS_TASK_H

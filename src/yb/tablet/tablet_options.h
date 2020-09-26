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
#ifndef YB_TABLET_TABLET_OPTIONS_H
#define YB_TABLET_TABLET_OPTIONS_H

#include <memory>
#include <vector>

#include "yb/util/env.h"
#include "yb/rocksdb/env.h"

#include "yb/client/client_fwd.h"

#include "yb/server/server_fwd.h"

#include "yb/tablet/tablet_fwd.h"

namespace rocksdb {
class Cache;
class EventListener;
class MemoryMonitor;
class Env;
}

namespace yb {

class Env;
class MetricRegistry;

namespace log {

class LogAnchorRegistry;

}

namespace tablet {

YB_STRONGLY_TYPED_BOOL(IsDropTable);

struct TabletOptions {
  std::shared_ptr<rocksdb::Cache> block_cache;
  std::shared_ptr<rocksdb::MemoryMonitor> memory_monitor;
  std::vector<std::shared_ptr<rocksdb::EventListener>> listeners;
  yb::Env* env = Env::Default();
  rocksdb::Env* rocksdb_env = rocksdb::Env::Default();
};

struct TabletInitData {
  RaftGroupMetadataPtr metadata;
  std::shared_future<client::YBClient*> client_future;
  scoped_refptr<server::Clock> clock;
  std::shared_ptr<MemTracker> parent_mem_tracker;
  std::shared_ptr<MemTracker> block_based_table_mem_tracker;
  MetricRegistry* metric_registry = nullptr;
  scoped_refptr<log::LogAnchorRegistry> log_anchor_registry;
  TabletOptions tablet_options;
  std::string log_prefix_suffix;
  TransactionParticipantContext* transaction_participant_context = nullptr;
  client::LocalTabletFilter local_tablet_filter;
  TransactionCoordinatorContext* transaction_coordinator_context = nullptr;
  TransactionsEnabled txns_enabled = TransactionsEnabled::kTrue;
  IsSysCatalogTablet is_sys_catalog = IsSysCatalogTablet::kFalse;
  SnapshotCoordinator* snapshot_coordinator = nullptr;
  TabletSplitter* tablet_splitter = nullptr;
};

} // namespace tablet
} // namespace yb
#endif // YB_TABLET_TABLET_OPTIONS_H

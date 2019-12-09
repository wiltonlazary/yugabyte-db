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

#ifndef CODE_YUGABYTE_SRC_YB_TSERVER_METRICS_SNAPSHOTTER_H
#define CODE_YUGABYTE_SRC_YB_TSERVER_METRICS_SNAPSHOTTER_H

#include <memory>

#include "yb/server/server_base_options.h"

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/util/status.h"
#include "yb/util/net/net_util.h"
#include "yb/client/client.h"

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/options.h"
#include "yb/client/async_initializer.h"
#include "yb/client/client_fwd.h"


namespace yb {
namespace tserver {

class TabletServer;
class TabletServerOptions;

class MetricsSnapshotter {
 public:
  MetricsSnapshotter(const TabletServerOptions& options, TabletServer* server);
  CHECKED_STATUS Start();
  CHECKED_STATUS Stop();

  ~MetricsSnapshotter();

 private:
  class Thread;
  gscoped_ptr<Thread> thread_;
  DISALLOW_COPY_AND_ASSIGN(MetricsSnapshotter);
};

} // namespace tserver
} // namespace yb
#endif /* CODE_YUGABYTE_SRC_YB_TSERVER_METRICS_SNAPSHOTTER_H */

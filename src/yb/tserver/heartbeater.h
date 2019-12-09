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
#ifndef YB_TSERVER_HEARTBEATER_H
#define YB_TSERVER_HEARTBEATER_H

#include <memory>

#include "yb/server/server_base_options.h"

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/util/status.h"
#include "yb/util/net/net_util.h"

namespace yb {
namespace tserver {

class TabletServer;
class TabletServerOptions;

// Component of the Tablet Server which is responsible for heartbeating to the
// leader master.
//
// TODO: send heartbeats to non-leader masters.
class Heartbeater {
 public:
  Heartbeater(const TabletServerOptions& options, TabletServer* server);
  CHECKED_STATUS Start();
  CHECKED_STATUS Stop();

  // Trigger a heartbeat as soon as possible, even if the normal
  // heartbeat interval has not expired.
  void TriggerASAP();

  void set_master_addresses(server::MasterAddressesPtr master_addresses);

  ~Heartbeater();

 private:
  class Thread;
  gscoped_ptr<Thread> thread_;
  DISALLOW_COPY_AND_ASSIGN(Heartbeater);
};

} // namespace tserver
} // namespace yb
#endif /* YB_TSERVER_HEARTBEATER_H */

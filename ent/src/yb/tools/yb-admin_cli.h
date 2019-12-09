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

#ifndef ENT_SRC_YB_TOOLS_YB_ADMIN_CLI_H
#define ENT_SRC_YB_TOOLS_YB_ADMIN_CLI_H

namespace yb {
namespace tools {
namespace enterprise {

class ClusterAdminClient;

}  // namespace enterprise
}  // namespace tools
}  // namespace yb

#include "../../../../src/yb/tools/yb-admin_cli.h"

namespace yb {
namespace tools {
namespace enterprise {

class ClusterAdminClient;

class ClusterAdminCli : public yb::tools::ClusterAdminCli {
  typedef yb::tools::ClusterAdminCli super;

 private:
  void RegisterCommandHandlers(ClusterAdminClientClass* client) override;
};

}  // namespace enterprise
}  // namespace tools
}  // namespace yb

#endif // ENT_SRC_YB_TOOLS_YB_ADMIN_CLI_H

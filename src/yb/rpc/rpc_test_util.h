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

#ifndef YB_RPC_RPC_TEST_UTIL_H
#define YB_RPC_RPC_TEST_UTIL_H

#include "yb/rpc/rpc_fwd.h"

namespace yb {
namespace rpc {

struct MessengerShutdownDeleter {
  void operator()(Messenger* messenger) const;
};

using AutoShutdownMessengerHolder = std::unique_ptr<Messenger, MessengerShutdownDeleter>;

AutoShutdownMessengerHolder CreateAutoShutdownMessengerHolder(
    std::unique_ptr<Messenger>&& messenger);

} // namespace rpc
} // namespace yb

#endif // YB_RPC_RPC_TEST_UTIL_H

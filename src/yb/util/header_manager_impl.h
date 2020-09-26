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

#ifndef YB_UTIL_HEADER_MANAGER_IMPL_H
#define YB_UTIL_HEADER_MANAGER_IMPL_H

#include <memory>

#include "yb/util/header_manager.h"

namespace yb {
namespace enterprise {

class HeaderManager;
class UniverseKeyManager;

// Implementation of used by FileFactory to construct header for encrypted files.
// The header format looks like:
//
// magic encryption string
// header size (4 bytes)
// universe key id size (4 bytes)
// universe key id
// EncryptionParamsPB size (4 bytes)
// EncryptionParamsPB
std::unique_ptr<HeaderManager> DefaultHeaderManager(UniverseKeyManager* universe_key_manager);

} // namespace enterprise
} // namespace yb

#endif // YB_UTIL_HEADER_MANAGER_IMPL_H

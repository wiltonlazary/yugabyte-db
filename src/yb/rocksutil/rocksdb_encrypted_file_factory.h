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

#ifndef YB_ROCKSUTIL_ROCKSDB_ENCRYPTED_FILE_FACTORY_H
#define YB_ROCKSUTIL_ROCKSDB_ENCRYPTED_FILE_FACTORY_H

#include "yb/rocksdb/env.h"

namespace yb {
namespace enterprise {

class HeaderManager;

std::unique_ptr<rocksdb::Env> NewRocksDBEncryptedEnv(std::unique_ptr<HeaderManager> header_manager);

} // namespace enterprise
} // namespace yb


#endif // YB_ROCKSUTIL_ROCKSDB_ENCRYPTED_FILE_FACTORY_H

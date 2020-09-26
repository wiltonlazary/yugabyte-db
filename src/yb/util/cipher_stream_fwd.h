// Copyright (c) Yugabyte, Inc.
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

#ifndef YB_UTIL_CIPHER_STREAM_FWD_H
#define YB_UTIL_CIPHER_STREAM_FWD_H

#include "yb/util/strongly_typed_bool.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(EncryptionOverflowWorkaround);

namespace enterprise {
class BlockAccessCipherStream;
} // namespace enterprise

} // namespace yb

#endif  // YB_UTIL_CIPHER_STREAM_FWD_H

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

#ifndef YB_UTIL_PG_QUOTE_H
#define YB_UTIL_PG_QUOTE_H

#include <string>

namespace yb {

// TODO(jason): integrate better with Format (yb/util/format.h).
std::string QuotePgConnStrValue(const std::string input);
std::string QuotePgName(const std::string input);

} // namespace yb

#endif // YB_UTIL_PG_QUOTE_H

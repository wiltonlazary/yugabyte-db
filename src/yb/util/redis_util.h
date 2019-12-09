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

#ifndef YB_UTIL_REDIS_UTIL_H
#define YB_UTIL_REDIS_UTIL_H

#include <string>

namespace yb {

class RedisUtil {
 public:
  static bool RedisPatternMatch(
      const std::string& pattern, const std::string& string, bool ignore_case);

 private:
  static bool RedisPatternMatchWithLen(
      const char* pattern, int pattern_len, const char* string, int str_len, bool ignore_case);
};

} // namespace yb

#endif // YB_UTIL_REDIS_UTIL_H

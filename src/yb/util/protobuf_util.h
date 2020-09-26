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

// Portions Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_PROTOBUF_UTIL_H
#define YB_UTIL_PROTOBUF_UTIL_H

#include <google/protobuf/message_lite.h>

#include "yb/util/enums.h"

namespace yb {

inline bool AppendPBToString(const google::protobuf::MessageLite &msg, faststring *output) {
  int old_size = output->size();
  int byte_size = msg.ByteSize();
  output->resize(old_size + byte_size);
  uint8* start = reinterpret_cast<uint8*>(output->data() + old_size);
  uint8* end = msg.SerializeWithCachedSizesToArray(start);
  CHECK(end - start == byte_size)
    << "Error in serialization. byte_size=" << byte_size
    << " new ByteSize()=" << msg.ByteSize()
    << " end-start=" << (end-start);
  return true;
}

} // namespace yb

#define PB_ENUM_FORMATTERS(EnumType) \
  inline std::string PBEnumToString(EnumType value) { \
    if (BOOST_PP_CAT(EnumType, _IsValid)(value)) { \
      return BOOST_PP_CAT(EnumType, _Name)(value); \
    } else { \
      return ::yb::Format("<unknown " BOOST_PP_STRINGIZE(EnumType) " : $0>", \
          ::yb::to_underlying(value)); \
    } \
  } \
  inline std::string ToString(EnumType value) { \
    return PBEnumToString(value); \
  } \
  __attribute__((unused)) inline std::ostream& operator << (std::ostream& out, EnumType value) { \
    return out << PBEnumToString(value); \
  }

template<typename T>
std::vector<T> GetAllPbEnumValues() {
  const auto* desc = google::protobuf::GetEnumDescriptor<T>();
  std::vector<T> result;
  result.reserve(desc->value_count());
  for (int i = 0; i < desc->value_count(); ++i) {
    result.push_back(T(desc->value(i)->number()));
  }
  return result;
}

#endif  // YB_UTIL_PROTOBUF_UTIL_H

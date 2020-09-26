//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#ifndef YB_UTIL_STRING_UTIL_H
#define YB_UTIL_STRING_UTIL_H

#pragma once

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "yb/util/slice.h"
#include "yb/util/tostring.h"

namespace yb {
namespace details {

template<class Container>
struct Unpacker {
  using container_type = Container;
  Container container;
};

template<class T, class... Args>
size_t ItemCount(const T&, const Args&...);

template<class T, class... Args>
void AppendItem(vector<string>* dest, const T& t, const Args&... args);

inline size_t ItemCount() { return 0; }
inline void AppendItem(vector<string>* dest) {}

template<class T>
struct ToStringVectorHelper {
  template<class... Args>
  static size_t Count(const T& t, const Args&... args) {
    return 1 + ItemCount(args...);
  }

  template<class... Args>
  static void Append(vector<string>* dest, const T& t, const Args&... args) {
    dest->push_back(ToString(t));
    AppendItem(dest, args...);
  }
};

template<class T>
struct ToStringVectorHelper<Unpacker<T> > {
  template<class... Args>
  static size_t Count(const Unpacker<T>& unpacker, const Args&... args) {
    return std::distance(std::begin(unpacker.container), std::end(unpacker.container)) +
        ItemCount(args...);
  }

  template<class... Args>
  static void Append(vector<string>* dest, const Unpacker<T>& unpacker, const Args&... args) {
    for(auto&& i : unpacker.container) {
      dest->push_back(ToString(i));
    }
    AppendItem(dest, args...);
  }
};

template<class T, class... Args>
size_t ItemCount(const T& t, const Args&...args) {
  return ToStringVectorHelper<T>::Count(t, args...);
}

template<class T, class... Args>
void AppendItem(vector<string>* dest, const T& t, const Args&... args) {
  return ToStringVectorHelper<T>::Append(dest, t, args...);
}

} // namespace details

// Whether the string contains (arbitrary long) integer value
bool IsBigInteger(const Slice& s);

// Whether the string contains (arbitrary long) decimal or integer value
bool IsDecimal(const Slice& s);

// Whether the string is "true"/"false" (case-insensitive)
bool IsBoolean(const Slice& s);

using StringVector = std::vector<std::string>;
StringVector StringSplit(const std::string& arg, char delim);

template<typename Iterator>
inline std::string RangeToString(Iterator begin, Iterator end) {
  return ToString(boost::make_iterator_range(begin, end));
}

template <typename T>
inline std::string VectorToString(const std::vector<T>& vec) {
  return ToString(vec);
}

// Whether or not content of two strings is equal ignoring case
// Examples:
// - abcd == ABCD
// - AbCd == aBCD
bool EqualsIgnoreCase(const std::string &string1,
                      const std::string &string2);

template <class T>
std::string RightPadToWidth(const T& val, int width) {
  std::stringstream ss;
  ss << val;
  string ss_str = ss.str();
  int64_t padding = width - ss_str.size();
  if (padding <= 0) {
    return ss_str;
  }
  return ss_str + string(padding, ' ');
}

// Returns true if s ends with substring end, and s has at least one more character before
// end. If left is a valid string pointer, it will contain s minus the end substring.
// Example 1: s = "15ms", end = "ms", then this function will return true and set left to "15".
// Example 2: s = "ms", end = "ms", this function will return false.
bool StringEndsWith(
    const std::string& s, const char* end, size_t end_len, std::string* left = nullptr);

inline bool StringEndsWith(
    const std::string& s, const std::string end, std::string* left = nullptr) {
  return StringEndsWith(s, end.c_str(), end.length(), left);
}

static constexpr const char* kDefaultSeparatorStr = ", ";

// Append then given string to the given destination string. If the destination string is already
// non-empty, append a separator first.
void AppendWithSeparator(const std::string& to_append,
                         std::string* dest,
                         const char* separator = kDefaultSeparatorStr);

void AppendWithSeparator(const char* to_append,
                         std::string* dest,
                         const char* separator = kDefaultSeparatorStr);

template<class Container>
auto unpack(Container&& container) {
  return details::Unpacker<Container>{std::forward<Container>(container)};
}

template<class... Args>
vector<string> ToStringVector(Args&&... args) {
  vector<string> result;
  result.reserve(details::ItemCount(args...));
  details::AppendItem(&result, args...);
  return result;
}

}  // namespace yb

namespace rocksdb {
using yb::ToString;
using yb::StringSplit;
using yb::VectorToString;
}

#endif // YB_UTIL_STRING_UTIL_H

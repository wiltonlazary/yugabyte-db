//--------------------------------------------------------------------------------------------------
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
//
// Utilities for DateTime parsing, processing and formatting
// TODO: some parsing and formatting settings (e.g. default timezone) should be configurable using a
// config file or QL functions
// currently hardcoding default_timezone(UTC), precision, output format and epoch
//--------------------------------------------------------------------------------------------------


#ifndef YB_UTIL_DATE_TIME_H_
#define YB_UTIL_DATE_TIME_H_


#include <locale>
#include <regex>

#include "yb/util/result.h"
#include "yb/util/timestamp.h"

namespace yb {

class DateTime {
 public:
  //----------------------------------------------------------------------------------------------
  // Timestamp input and output formats.
  struct InputFormat {
    const std::vector<std::regex> regexes;
    const int input_precision;

    InputFormat(const std::vector<std::regex>& regexes, const int input_precision)
        : regexes(regexes), input_precision(input_precision) {}
  };

  struct OutputFormat {
    const std::locale output_locale;

    explicit OutputFormat(const std::locale& output_locale) : output_locale(output_locale) {}
  };

  // CQL timestamp formats.
  static const InputFormat CqlInputFormat;
  static const OutputFormat CqlOutputFormat;

  //----------------------------------------------------------------------------------------------
  static Result<Timestamp> TimestampFromString(const std::string& str,
                                               const InputFormat& input_format = CqlInputFormat);
  static Timestamp TimestampFromInt(int64_t val, const InputFormat& input_format = CqlInputFormat);
  static std::string TimestampToString(Timestamp timestamp,
                                       const OutputFormat& output_format = CqlOutputFormat);
  static Timestamp TimestampNow();

  //----------------------------------------------------------------------------------------------
  // Date represented as the number of days in uint32_t with the epoch (1970-01-01) at the center of
  // the range (2^31). Min and max possible dates are "-5877641-06-23" and "5881580-07-11".
  static Result<uint32_t> DateFromString(const std::string& str);
  static Result<uint32_t> DateFromTimestamp(Timestamp timestamp);
  static Result<uint32_t> DateFromUnixTimestamp(int64_t unix_timestamp);
  static Result<std::string> DateToString(uint32_t date);
  static Timestamp DateToTimestamp(uint32_t date);
  static int64_t DateToUnixTimestamp(uint32_t date);
  static uint32_t DateNow();

  //----------------------------------------------------------------------------------------------
  // Min and max time of day since midnight in nano-seconds.
  static constexpr int64_t kMinTime = 0;
  static constexpr int64_t kMaxTime = 24 * 60 * 60 * 1000000000L - 1; // 23:59:59.999999999

  static Result<int64_t> TimeFromString(const std::string& str);
  static Result<std::string> TimeToString(int64_t time);
  static int64_t TimeNow();

  //----------------------------------------------------------------------------------------------
  static int64_t AdjustPrecision(int64_t val, int input_precision, int output_precision);
  static constexpr int64_t kInternalPrecision = 6; // microseconds
  static constexpr int64_t kMillisecondPrecision = 3; // milliseconds
};

} // namespace yb

#endif // YB_UTIL_DATE_TIME_H_

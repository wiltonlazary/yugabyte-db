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
#ifndef YB_UTIL_TEST_MACROS_H
#define YB_UTIL_TEST_MACROS_H

#include <string>

#include <boost/preprocessor/cat.hpp>

#include "yb/util/string_trim.h"
#include "yb/util/debug-util.h"

// ASSERT_NO_FATAL_FAILURE is just too long to type.
#define NO_FATALS(expr) \
  ASSERT_NO_FATAL_FAILURE(expr)

// Detect fatals in the surrounding scope. NO_FATALS() only checks for fatals
// in the expression passed to it.
#define NO_PENDING_FATALS() \
  if (testing::Test::HasFatalFailure()) { return; }

// ASSERT_NO_FATAL_FAILURE is just too long to type.
#define ASSERT_NO_FATALS ASSERT_NO_FATAL_FAILURE

// We are using "const auto" for storing the status so that the same macros work for both YB and
// RocksDB's Status types.

#define ASSERT_OK(status) do { \
    auto&& _s = (status); \
    if (_s.ok()) { \
      SUCCEED(); \
    } else { \
      FAIL() << "Bad status: " << StatusToString(_s);  \
    } \
  } while (0)

#define ASSERT_NOK(s) ASSERT_FALSE((s).ok())

#define ASSERT_OK_PREPEND(status, msg) do { \
  auto&& _s = (status); \
  if (_s.ok()) { \
    SUCCEED(); \
  } else { \
    FAIL() << (msg) << " - status: " << StatusToString(_s);  \
  } \
} while (0)

#define EXPECT_OK(status) do { \
    auto&& _s = (status); \
    if (_s.ok()) { \
      SUCCEED(); \
    } else { \
      ADD_FAILURE() << "Bad status: " << StatusToString(_s);  \
    } \
  } while (0)

#define EXPECT_NOT_OK(s) EXPECT_FALSE((s).ok())

// Like the above, but doesn't record successful
// tests.
#define ASSERT_OK_FAST(status) do {      \
    auto&& _s = (status); \
    if (!_s.ok()) { \
      FAIL() << "Bad status: " << StatusToString(_s);  \
    } \
  } while (0)

#define ASSERT_NO_ERROR(ec) \
  do { \
    auto&& _ec = (ec); \
    if (!_ec) { \
      SUCCEED(); \
    } else { \
      FAIL() << "Unexpected error: " << ec.message(); \
    } \
  } while (false)

#define EXPECT_NO_ERROR(ec) \
  do { \
    auto&& _ec = (ec); \
    if (!_ec) { \
      SUCCEED(); \
    } else { \
      ADD_FAILURE() << "Unexpected error: " << ec.message(); \
    } \
  } while (false)

#ifdef THREAD_SANITIZER
#define ASSERT_PERF_LE(lhs, rhs) do { (void)(lhs); (void)(rhs); } while(false)
#define EXPECT_PERF_LE(lhs, rhs) do { (void)(lhs); (void)(rhs); } while(false)
#else
#define ASSERT_PERF_LE(lhs, rhs) ASSERT_LE(lhs, rhs)
#define EXPECT_PERF_LE(lhs, rhs) EXPECT_LE(lhs, rhs)
#endif

#define ASSERT_STR_CONTAINS(str, substr) do { \
  std::string _s = (str); \
  if (_s.find((substr)) == std::string::npos) { \
    FAIL() << "Expected to find substring '" << (substr) \
    << "'. Got: '" << _s << "'"; \
  } \
  } while (0)

#define ASSERT_FILE_EXISTS(env, path) do { \
  std::string _s = (path); \
  ASSERT_TRUE(env->FileExists(_s)) \
    << "Expected file to exist: " << _s; \
  } while (0)

#define ASSERT_FILE_NOT_EXISTS(env, path) do { \
  std::string _s = (path); \
  ASSERT_FALSE(env->FileExists(_s)) \
    << "Expected file not to exist: " << _s; \
  } while (0)

// Wrappers around ASSERT_EQ and EXPECT_EQ that trim expected and actual strings and outputs
// expected and actual values without any escaping. We're also printing a stack trace to allow
// easier debugging.
#define _ASSERT_EXPECT_STR_EQ_VERBOSE_COMMON_SETUP(expected, actual) \
    const auto expected_tmp = ::yb::util::TrimStr(yb::util::LeftShiftTextBlock(expected)); \
    const auto actual_tmp = ::yb::util::TrimStr(yb::util::LeftShiftTextBlock(actual));

#define _ASSERT_EXPECT_STR_EQ_VERBOSE_COMMON_MSG \
    "\nActual (trimmed):\n" << actual_tmp \
        << "\n\nExpected (trimmed):\n" << expected_tmp;

#define ASSERT_STR_EQ_VERBOSE_TRIMMED(expected, actual) \
  do { \
    _ASSERT_EXPECT_STR_EQ_VERBOSE_COMMON_SETUP(expected, actual) \
    ASSERT_EQ(expected_tmp, actual_tmp) << _ASSERT_EXPECT_STR_EQ_VERBOSE_COMMON_MSG; \
  } while(0)

// A wrapper around EXPECT_EQ that trims expected and actual strings and outputs expected and actual
// values without any escaping.
#define EXPECT_STR_EQ_VERBOSE_TRIMMED(expected, actual) \
  do { \
    _ASSERT_EXPECT_STR_EQ_VERBOSE_COMMON_SETUP(expected, actual) \
    EXPECT_EQ(expected_tmp, actual_tmp) << _ASSERT_EXPECT_STR_EQ_VERBOSE_COMMON_MSG; \
  } while(0)

#define YB_ASSERT_TRUE(condition) \
  GTEST_TEST_BOOLEAN_((condition) ? true : false, #condition, false, true, \
                      GTEST_FATAL_FAILURE_)

#define VERIFY_EQ(expected_expr, actual_expr) \
  do { \
    auto&& expected = (expected_expr); \
    auto&& actual = (actual_expr); \
    if (expected != actual) { \
      return ::testing::internal::EqFailure( \
          BOOST_PP_STRINGIZE(expected_expr), \
          BOOST_PP_STRINGIZE(actual_expr), \
          ::testing::internal::FormatForComparisonFailureMessage(expected, actual), \
          ::testing::internal::FormatForComparisonFailureMessage(actual, expected), \
          false); \
    } \
  } while (false) \
  /**/

#define ASSERT_VERIFY(expr) \
  do { \
    auto&& result = (expr); \
    if (!result) { \
      FAIL() << result.message(); \
    } \
  } while (false) \
  /**/

#define CURRENT_TEST_NAME() \
  ::testing::UnitTest::GetInstance()->current_test_info()->name()

#define CURRENT_TEST_CASE_NAME() \
  ::testing::UnitTest::GetInstance()->current_test_info()->test_case_name()

#ifdef __APPLE__
#define YB_DISABLE_TEST_ON_MACOS(test_name) BOOST_PP_CAT(DISABLED_, test_name)
#else
#define YB_DISABLE_TEST_ON_MACOS(test_name) test_name
#endif

#ifdef THREAD_SANITIZER
#define YB_DISABLE_TEST_IN_TSAN(test_name) BOOST_PP_CAT(DISABLED_, test_name)
#else
#define YB_DISABLE_TEST_IN_TSAN(test_name) test_name
#endif

#if defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
#define YB_DISABLE_TEST_IN_SANITIZERS(test_name) BOOST_PP_CAT(DISABLED_, test_name)
#else
#define YB_DISABLE_TEST_IN_SANITIZERS(test_name) test_name
#endif

#endif  // YB_UTIL_TEST_MACROS_H

#!/usr/bin/env bash

# A "unit test" for Bash libraries used in build/test scripts.

#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
set -euo pipefail

. "${0%/*}/common-build-env.sh"

assert_equals() {
  expect_num_args 2 "$@"
  if [[ "$1" != "$2" ]]; then
    fatal "Assertion failed. Expected: '$1', got: '$2'"
  fi
}

pretend_we_are_on_jenkins() {
  if [[ -z ${JOB_NAME:-} ]]; then
    JOB_NAME=some-jenkins-job-name
  fi
  BUILD_ID=12345
  USER=jenkins
}

# -------------------------------------------------------------------------------------------------
# Testing detecting build type by Jenkins job name.
# -------------------------------------------------------------------------------------------------

test_build_type_detection_by_jenkins_job_name() {
  expect_num_args 2 "$@"
  local expected_build_type=$1
  local jenkins_job_name=$2
  (
    unset YB_COMPILER_TYPE
    unset build_type
    JOB_NAME="$jenkins_job_name"
    set_build_type_based_on_jenkins_job_name
    assert_equals "$expected_build_type" "$build_type"
  )
}

yb_log_quiet=true
test_build_type_detection_by_jenkins_job_name asan my-asan-job
test_build_type_detection_by_jenkins_job_name asan Asan-my-job
test_build_type_detection_by_jenkins_job_name asan my-job-ASAN
test_build_type_detection_by_jenkins_job_name debug my-debug-job
test_build_type_detection_by_jenkins_job_name debug deBuG-my-job
test_build_type_detection_by_jenkins_job_name debug my-job-debug
test_build_type_detection_by_jenkins_job_name fastdebug my-fastdebug-job
test_build_type_detection_by_jenkins_job_name fastdebug fasTdeBug-my-job
test_build_type_detection_by_jenkins_job_name fastdebug my-job-fastdebug
test_build_type_detection_by_jenkins_job_name profile_build my-profile_build-job
test_build_type_detection_by_jenkins_job_name profile_build profile_buIlD-my-job
test_build_type_detection_by_jenkins_job_name profile_build my-job-profile_build
test_build_type_detection_by_jenkins_job_name profile_gen my-profile_gen-job
test_build_type_detection_by_jenkins_job_name profile_gen Profile_Gen-my-job
test_build_type_detection_by_jenkins_job_name profile_gen my-job-profile_gen
test_build_type_detection_by_jenkins_job_name release my-relEase-job
test_build_type_detection_by_jenkins_job_name release releasE-job
test_build_type_detection_by_jenkins_job_name release my-job-RELEASE
test_build_type_detection_by_jenkins_job_name tsan my-tsan-job
test_build_type_detection_by_jenkins_job_name tsan TSAN-my-job
test_build_type_detection_by_jenkins_job_name tsan my-job-tsan

test_build_type_detection_by_jenkins_job_name debug random-job-name
test_build_type_detection_by_jenkins_job_name debug releasewithoutwordseparator-job-name
test_build_type_detection_by_jenkins_job_name debug job-name-noseparatorfastdebug

# -------------------------------------------------------------------------------------------------
# Test determining compiler type based on the Jenkins job name.
# -------------------------------------------------------------------------------------------------

test_compiler_detection_by_jenkins_job_name() {
  expect_num_args 2 "$@"
  local expected_compiler_type=$1
  local jenkins_job_name=$2
  (
    unset YB_COMPILER_TYPE
    JOB_NAME="$jenkins_job_name"
    set_compiler_type_based_on_jenkins_job_name
    assert_equals "$expected_compiler_type" "$YB_COMPILER_TYPE"
  )
}

test_compiler_detection_by_jenkins_job_name gcc my-job-gcc
test_compiler_detection_by_jenkins_job_name gcc gcc-my-job
test_compiler_detection_by_jenkins_job_name clang my-job-clang
test_compiler_detection_by_jenkins_job_name clang clang-my-job
test_compiler_detection_by_jenkins_job_name "" random-job-name

# -------------------------------------------------------------------------------------------------
# Test determining build type to pass to our CMakeLists.txt as CMAKE_BUILD_TYPE and compiler type.
# -------------------------------------------------------------------------------------------------

test_set_cmake_build_type_and_compiler_type() {
  expect_num_args 6 "$@"
  local _build_type=$1
  validate_build_type "$_build_type"
  local os_type=$2
  if [[ ! "$os_type" =~ ^(darwin|linux-gnu) ]]; then
    fatal "Unexpected value for the mock OSTYPE: '$os_type'"
  fi
  local compiler_type_preference=$3
  if [[ ! "$compiler_type_preference" =~ ^(gcc|clang|auto|N/A)$ ]]; then
    fatal "Invalid value for compiler_type_preference: '$compiler_type_preference'"
  fi
  local expected_cmake_build_type=$4
  local expected_compiler_type=$5
  local expected_exit_code=$6

  set +e
  (
    set -e
    if [[ "$compiler_type_preference" == "auto" ]]; then
      unset YB_COMPILER_TYPE
    else
      YB_COMPILER_TYPE=$compiler_type_preference
    fi
    unset cmake_build_type
    build_type=$_build_type
    OSTYPE=$os_type
    yb_fatal_quiet=true
    set_cmake_build_type_and_compiler_type
    assert_equals "$expected_cmake_build_type" "$cmake_build_type"
    assert_equals "$expected_compiler_type" "$YB_COMPILER_TYPE"
  )
  local exit_code=$?
  set -e
  assert_equals "$expected_exit_code" "$exit_code"
}

# Parameters:                               build_type OSTYPE    Compiler   Expected   Expected
#                                                                type       build_type YB_COMPILER_
#                                                                preference            TYPE

test_set_cmake_build_type_and_compiler_type asan       darwin    auto       fastdebug  clang  0
test_set_cmake_build_type_and_compiler_type asan       darwin    clang      fastdebug  clang  0
test_set_cmake_build_type_and_compiler_type asan       darwin    gcc        N/A        N/A    1
test_set_cmake_build_type_and_compiler_type asan       linux-gnu auto       fastdebug  clang  0
test_set_cmake_build_type_and_compiler_type asan       linux-gnu clang      fastdebug  clang  0
test_set_cmake_build_type_and_compiler_type asan       linux-gnu gcc        N/A        N/A    1
test_set_cmake_build_type_and_compiler_type debug      darwin    auto       debug      clang  0
test_set_cmake_build_type_and_compiler_type debug      darwin    clang      debug      clang  0
test_set_cmake_build_type_and_compiler_type debug      darwin    gcc        N/A        N/A    1
test_set_cmake_build_type_and_compiler_type debug      linux-gnu auto       debug      gcc    0
test_set_cmake_build_type_and_compiler_type debug      linux-gnu clang      debug      clang  0
test_set_cmake_build_type_and_compiler_type debug      linux-gnu gcc        debug      gcc    0
test_set_cmake_build_type_and_compiler_type FaStDeBuG  darwin    auto       fastdebug  clang  0
test_set_cmake_build_type_and_compiler_type FaStDeBuG  darwin    clang      fastdebug  clang  0
test_set_cmake_build_type_and_compiler_type FaStDeBuG  darwin    gcc        N/A        N/A    1
test_set_cmake_build_type_and_compiler_type FaStDeBuG  linux-gnu auto       fastdebug  gcc    0
test_set_cmake_build_type_and_compiler_type FaStDeBuG  linux-gnu clang      fastdebug  clang  0
test_set_cmake_build_type_and_compiler_type FaStDeBuG  linux-gnu gcc        fastdebug  gcc    0
test_set_cmake_build_type_and_compiler_type release    darwin    auto       release    clang  0
test_set_cmake_build_type_and_compiler_type release    darwin    clang      release    clang  0
test_set_cmake_build_type_and_compiler_type release    darwin    gcc        N/A        N/A    1
test_set_cmake_build_type_and_compiler_type release    linux-gnu auto       release    gcc    0
test_set_cmake_build_type_and_compiler_type release    linux-gnu clang      release    clang  0
test_set_cmake_build_type_and_compiler_type release    linux-gnu gcc        release    gcc    0

# -------------------------------------------------------------------------------------------------

echo "${0##/*} succeeded"

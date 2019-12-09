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
// A builtin function is specified in PGSQL but implemented in C++, and this module represents the
// metadata of a builtin function.
//--------------------------------------------------------------------------------------------------

#ifndef YB_UTIL_BFPG_BFDECL_H_
#define YB_UTIL_BFPG_BFDECL_H_

#include "yb/common/common.pb.h"

#include "yb/gutil/macros.h"

#include "yb/util/logging.h"
#include "yb/util/bfpg/tserver_opcodes.h"

namespace yb {
namespace bfpg {

// This class contains the metadata of a builtin function, which has to principal components.
// 1. Specification of a builtin function.
//    - A PGSQL name: This is the name of the builtin function.
//    - A PGSQL parameter types: This is the signature of the builtin function.
//    - A PGSQL return type.
// 2. Definition or body of a builtin function.
//    - A C++ function name: This represents the implementation of the builtin function in C++.
class BFDecl {
 public:
  BFDecl(const char *cpp_name,
         const char *ql_name,
         DataType return_type,
         std::initializer_list<DataType> param_types,
         TSOpcode tsopcode = TSOpcode::kNoOp,
         bool implemented = true)
      : cpp_name_(cpp_name),
        ql_name_(ql_name),
        return_type_(return_type),
        param_types_(param_types),
        tsopcode_(tsopcode),
        implemented_(implemented) {
  }

  const char *cpp_name() const {
    return cpp_name_;
  }

  const char *ql_name() const {
    return ql_name_;
  }

  const DataType& return_type() const {
    return return_type_;
  }

  const std::vector<DataType>& param_types() const {
    return param_types_;
  }

  int param_count() const {
    return param_types_.size();
  }

  TSOpcode tsopcode() const {
    return tsopcode_;
  }

  bool is_server_operator() const {
    return tsopcode_ != TSOpcode::kNoOp;
  }

  bool implemented() const {
    return implemented_;
  }

  bool is_collection_bcall() const {
    return is_collection_op(tsopcode_);
  }

  bool is_aggregate_bcall() const {
    return is_aggregate_op(tsopcode_);
  }

  static bool is_collection_op(TSOpcode tsopcode) {
    switch (tsopcode) {
      case TSOpcode::kMapExtend: FALLTHROUGH_INTENDED;
      case TSOpcode::kMapRemove: FALLTHROUGH_INTENDED;
      case TSOpcode::kSetExtend: FALLTHROUGH_INTENDED;
      case TSOpcode::kSetRemove: FALLTHROUGH_INTENDED;
      case TSOpcode::kListAppend: FALLTHROUGH_INTENDED;
      case TSOpcode::kListPrepend: FALLTHROUGH_INTENDED;
      case TSOpcode::kListRemove:
        return true;
      default:
        return false;
    }
  }

  static bool is_aggregate_op(TSOpcode tsopcode) {
    switch (tsopcode) {
      case TSOpcode::kAvg: FALLTHROUGH_INTENDED;
      case TSOpcode::kCount: FALLTHROUGH_INTENDED;
      case TSOpcode::kMax: FALLTHROUGH_INTENDED;
      case TSOpcode::kMin: FALLTHROUGH_INTENDED;
      case TSOpcode::kSumInt8: FALLTHROUGH_INTENDED;
      case TSOpcode::kSumInt16: FALLTHROUGH_INTENDED;
      case TSOpcode::kSumInt32: FALLTHROUGH_INTENDED;
      case TSOpcode::kSumInt64: FALLTHROUGH_INTENDED;
      case TSOpcode::kSumFloat: FALLTHROUGH_INTENDED;
      case TSOpcode::kSumDouble:
        return true;
      default:
        return false;
    }
  }

 private:
  const char *cpp_name_;
  const char *ql_name_;
  DataType return_type_;
  std::vector<DataType> param_types_;
  TSOpcode tsopcode_;
  bool implemented_;
};

} // namespace bfpg
} // namespace yb

#endif  // YB_UTIL_BFPG_BFDECL_H_

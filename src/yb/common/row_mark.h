//
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
#ifndef YB_COMMON_ROW_MARK_H
#define YB_COMMON_ROW_MARK_H

#include <glog/logging.h>

#include "yb/common/common.pb.h"

namespace yb {

// Determine whether two row mark types conflict.
bool AreConflictingRowMarkTypes(RowMarkType row_mark_type_a, RowMarkType row_mark_type_b);

template <typename PB>
RowMarkType GetRowMarkTypeFromPB(const PB& pb) {
  if (pb.has_row_mark_type()) {
    if (IsValidRowMarkType(pb.row_mark_type())) {
      return pb.row_mark_type();
    } else {
      // We shouldn't get here because other row lock types are disabled at the postgres level.
      LOG(DFATAL) << "Unsupported row lock of type " << RowMarkType_Name(pb.row_mark_type());
    }
  }
  return RowMarkType::ROW_MARK_ABSENT;
}

// Get the most restrictive row mark type from a list of row mark types.
RowMarkType GetStrongestRowMarkType(std::initializer_list<RowMarkType> row_mark_types);

// Determine whether a row mark type is valid.
bool IsValidRowMarkType(RowMarkType row_mark_type);

/*
 * Returns whether an operation with this row mark should try to use pessimistic locking.
 * Currently txn layer will use a best-effort approach, by setting the txn priority to highest if
 * this is a new txn (first operation within a transaction).
 */
bool RowMarkNeedsPessimisticLock(RowMarkType row_mark_type);

// Convert a row mark type to a string to use in a PostgreSQL query.
std::string RowMarkTypeToPgsqlString(const RowMarkType row_mark_type);

} // namespace yb

#endif // YB_COMMON_ROW_MARK_H

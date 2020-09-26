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
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGGATE_PG_VALUE_H_
#define YB_YQL_PGGATE_PG_VALUE_H_

#include "yb/client/client.h"
#include "yb/common/ql_value.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {
namespace pggate {

/*
 * Convert a QLValue to a Datum given its type entity.
 */
Status PgValueFromPB(const YBCPgTypeEntity *type_entity,
                     YBCPgTypeAttrs type_attrs,
                     const QLValuePB& ql_value,
                     uint64_t* datum,
                     bool *is_null);

/*
 * Convert a Datum to QLValue given its type entity.
 */
Status PgValueToPB(const YBCPgTypeEntity *type_entity,
                   uint64_t datum,
                   bool is_null,
                   QLValue* ql_value);

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_VALUE_H_

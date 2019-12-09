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

#include <algorithm>
#include <vector>

#include <boost/lexical_cast.hpp>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "yb/common/schema.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet-test-base.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using strings::Substitute;

namespace yb {
namespace tablet {

class TestTabletSchema : public YBTabletTest {
 public:
  TestTabletSchema()
    : YBTabletTest(CreateBaseSchema(), YQL_TABLE_TYPE) {
  }

  void InsertRows(size_t first_key, size_t nrows) {
    for (size_t i = first_key; i < nrows; ++i) {
      InsertRow(i);
      if (i == (nrows / 2)) {
        ASSERT_OK(tablet()->Flush(tablet::FlushMode::kSync));
      }
    }
  }

  void InsertRow(size_t key) {
    LocalTabletWriter writer(tablet().get());
    QLWriteRequestPB req;
    QLAddInt32HashValue(&req, key);
    QLAddInt32ColumnValue(&req, kFirstColumnId + 1, key);
    ASSERT_OK(writer.Write(&req));
  }

  void DeleteRow(size_t key) {
    LocalTabletWriter writer(tablet().get());
    QLWriteRequestPB req;
    req.set_type(QLWriteRequestPB::QL_STMT_DELETE);
    QLAddInt32HashValue(&req, key);
    ASSERT_OK(writer.Write(&req));
  }

  void MutateRow(size_t key, size_t col_idx, int32_t new_val) {
    LocalTabletWriter writer(tablet().get());
    QLWriteRequestPB req;
    QLAddInt32HashValue(&req, key);
    QLAddInt32ColumnValue(&req, kFirstColumnId + col_idx, new_val);
    ASSERT_OK(writer.Write(&req));
  }

  void VerifyTabletRows(const Schema& projection,
                        const std::vector<std::pair<string, string> >& keys) {
    typedef std::pair<string, string> StringPair;

    vector<string> rows;
    ASSERT_OK(DumpTablet(*tablet(), projection, &rows));
    std::sort(rows.begin(), rows.end());
    for (const string& row : rows) {
      bool found = false;
      for (const StringPair& k : keys) {
        if (row.find(k.first) != string::npos) {
          ASSERT_STR_CONTAINS(row, k.second);
          found = true;
          break;
        }
      }
      ASSERT_TRUE(found) << "Row: " << row << ", keys: " << yb::ToString(keys);
    }
  }

 private:
  Schema CreateBaseSchema() {
    return Schema({ ColumnSchema("key", INT32, false, true),
                    ColumnSchema("c1", INT32) }, 1);
  }
};

// Read from a tablet using a projection schema with columns not present in
// the original schema. Verify that the server reject the request.
TEST_F(TestTabletSchema, TestRead) {
  const size_t kNumRows = 10;
  Schema projection({ ColumnSchema("key", INT32, false, true),
                      ColumnSchema("c2", INT64),
                      ColumnSchema("c3", STRING) },
                    1);

  InsertRows(0, kNumRows);

  auto iter = tablet()->NewRowIterator(projection, boost::none);
  ASSERT_TRUE(!iter.ok() && iter.status().IsInvalidArgument());
  ASSERT_STR_CONTAINS(iter.status().message().ToBuffer(),
                      "Some columns are not present in the current schema: c2, c3");
}

// Write to the table using a projection schema with a renamed field.
TEST_F(TestTabletSchema, TestRenameProjection) {
  std::vector<std::pair<string, string> > keys;

  // Insert with the base schema
  InsertRow(1);

  // Switch schema to s2
  SchemaBuilder builder(tablet()->metadata()->schema());
  ASSERT_OK(builder.RenameColumn("c1", "c1_renamed"));
  AlterSchema(builder.Build());
  Schema s2 = builder.BuildWithoutIds();

  // Insert with the s2 schema after AlterSchema(s2)
  InsertRow(2);

  // Read and verify using the s2 schema
  keys.clear();
  for (int i = 1; i <= 4; ++i) {
    keys.push_back(std::pair<string, string>(Substitute("{ int32_value: $0", i),
                                             Substitute("int32_value: $0 }", i)));
  }
  VerifyTabletRows(s2, keys);

  // Delete the first two rows
  DeleteRow(/* key= */ 1);

  // Alter the remaining row
  MutateRow(/* key= */ 2, /* col_idx= */ 1, /* new_val= */ 6);

  // Read and verify using the s2 schema
  keys.clear();
  keys.push_back(std::pair<string, string>("{ int32_value: 2", "int32_value: 6 }"));
  VerifyTabletRows(s2, keys);
}

// Verify that removing a column and re-adding it will not result in making old data visible
TEST_F(TestTabletSchema, TestDeleteAndReAddColumn) {
  std::vector<std::pair<string, string> > keys;

  // Insert and Mutate with the base schema
  InsertRow(1);
  MutateRow(/* key= */ 1, /* col_idx= */ 1, /* new_val= */ 2);

  keys.clear();
  keys.push_back(std::pair<string, string>("{ int32_value: 1", "int32_value: 2 }"));
  VerifyTabletRows(client_schema_, keys);

  // Switch schema to s2
  SchemaBuilder builder(tablet()->metadata()->schema());
  ASSERT_OK(builder.RemoveColumn("c1"));
  // NOTE this new 'c1' will have a different id from the previous one
  //      so the data added to the previous 'c1' will not be visible.
  ASSERT_OK(builder.AddNullableColumn("c1", INT32));
  AlterSchema(builder.Build());
  Schema s2 = builder.BuildWithoutIds();

  // Verify that the new 'c1' have the default value
  keys.clear();
  keys.push_back(std::pair<string, string>("{ int32_value: 1", "null }"));
  VerifyTabletRows(s2, keys);
}

} // namespace tablet
} // namespace yb

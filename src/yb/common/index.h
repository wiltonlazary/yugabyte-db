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
// Classes that implement secondary index.
//--------------------------------------------------------------------------------------------------

#ifndef YB_COMMON_INDEX_H_
#define YB_COMMON_INDEX_H_

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "yb/common/entity_ids.h"
#include "yb/common/schema.h"

namespace yb {

// A class to maintain the information of an index.
class IndexInfo {
 public:
  // Index column mapping.
  struct IndexColumn {
    ColumnId column_id;         // Column id in the index table.
    string column_name;         // Column name in the index table - colexpr.MangledName().
    ColumnId indexed_column_id; // Corresponding column id in indexed table.
    QLExpressionPB colexpr;     // Index expression.

    explicit IndexColumn(const IndexInfoPB::IndexColumnPB& pb);
    IndexColumn() {}

    void ToPB(IndexInfoPB::IndexColumnPB* pb) const;
  };

  explicit IndexInfo(const IndexInfoPB& pb);
  IndexInfo() {}

  void ToPB(IndexInfoPB* pb) const;

  const TableId& table_id() const { return table_id_; }
  const TableId& indexed_table_id() const { return indexed_table_id_; }
  uint32_t schema_version() const { return schema_version_; }
  bool is_local() const { return is_local_; }
  bool is_unique() const { return is_unique_; }

  const std::vector<IndexColumn>& columns() const { return columns_; }
  const IndexColumn& column(const size_t idx) const { return columns_[idx]; }
  size_t hash_column_count() const { return hash_column_count_; }
  size_t range_column_count() const { return range_column_count_; }
  size_t key_column_count() const { return hash_column_count_ + range_column_count_; }

  const std::vector<ColumnId>& indexed_hash_column_ids() const {
    return indexed_hash_column_ids_;
  }
  const std::vector<ColumnId>& indexed_range_column_ids() const {
    return indexed_range_column_ids_;
  }

  // Return column ids that are primary key columns of the indexed table.
  std::vector<ColumnId> index_key_column_ids() const;

  // Index primary key columns of the indexed table only?
  bool PrimaryKeyColumnsOnly(const Schema& indexed_schema) const;

  // Is column covered by this index? (Note: indexed columns are always covered)
  bool IsColumnCovered(ColumnId column_id) const;

  // Check if this INDEX contain the column being referenced by the given selected expression.
  // - If found, return the location of the column (columns_[loc]).
  // - Otherwise, return -1.
  int32_t IsExprCovered(const string& expr_content) const;

  // Same as "IsExprCovered" but only search the key columns.
  int32_t FindKeyIndex(const string& key_name) const;

  bool use_mangled_column_name() const {
    return use_mangled_column_name_;
  }

 private:
  const TableId table_id_;            // Index table id.
  const TableId indexed_table_id_;    // Indexed table id.
  const uint32_t schema_version_ = 0; // Index table's schema version.
  const bool is_local_ = false;       // Whether this is a local index.
  const bool is_unique_ = false;      // Whether this is a unique index.
  const std::vector<IndexColumn> columns_; // Index columns.
  const size_t hash_column_count_ = 0;     // Number of hash columns in the index.
  const size_t range_column_count_ = 0;    // Number of range columns in the index.
  const std::vector<ColumnId> indexed_hash_column_ids_;  // Hash column ids in the indexed table.
  const std::vector<ColumnId> indexed_range_column_ids_; // Range column ids in the indexed table.

  // Column ids covered by the index (include indexed columns).
  std::unordered_set<ColumnId> covered_column_ids_;

  // Newer INDEX use mangled column name instead of ID.
  bool use_mangled_column_name_ = false;
};

// A map to look up an index by its index table id.
class IndexMap : public std::unordered_map<TableId, IndexInfo> {
 public:
  explicit IndexMap(const google::protobuf::RepeatedPtrField<IndexInfoPB>& indexes);
  IndexMap() {}

  void FromPB(const google::protobuf::RepeatedPtrField<IndexInfoPB>& indexes);
  void ToPB(google::protobuf::RepeatedPtrField<IndexInfoPB>* indexes) const;

  Result<const IndexInfo*> FindIndex(const TableId& index_id) const;
};

}  // namespace yb

#endif  // YB_COMMON_INDEX_H_

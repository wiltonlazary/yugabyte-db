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

#include "yb/docdb/ql_rocksdb_storage.h"

#include "yb/common/pgsql_protocol.pb.h"

#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/doc_expr.h"
#include "yb/docdb/primitive_value_util.h"

namespace yb {
namespace docdb {

QLRocksDBStorage::QLRocksDBStorage(const DocDB& doc_db)
    : doc_db_(doc_db) {
}

//--------------------------------------------------------------------------------------------------

Status QLRocksDBStorage::GetIterator(const QLReadRequestPB& request,
                                     const Schema& projection,
                                     const Schema& schema,
                                     const TransactionOperationContextOpt& txn_op_context,
                                     CoarseTimePoint deadline,
                                     const ReadHybridTime& read_time,
                                     const common::QLScanSpec& spec,
                                     std::unique_ptr<common::YQLRowwiseIteratorIf> *iter) const {

  auto doc_iter = std::make_unique<DocRowwiseIterator>(
      projection, schema, txn_op_context, doc_db_, deadline, read_time);
  RETURN_NOT_OK(doc_iter->Init(spec));
  *iter = std::move(doc_iter);
  return Status::OK();
}

Status QLRocksDBStorage::BuildYQLScanSpec(const QLReadRequestPB& request,
                                          const ReadHybridTime& read_time,
                                          const Schema& schema,
                                          const bool include_static_columns,
                                          const Schema& static_projection,
                                          std::unique_ptr<common::QLScanSpec>* spec,
                                          std::unique_ptr<common::QLScanSpec>*
                                          static_row_spec) const {
  // Populate dockey from QL key columns.
  auto hash_code = request.has_hash_code() ?
      boost::make_optional<int32_t>(request.hash_code()) : boost::none;
  auto max_hash_code = request.has_max_hash_code() ?
      boost::make_optional<int32_t>(request.max_hash_code()) : boost::none;

  vector<PrimitiveValue> hashed_components;
  RETURN_NOT_OK(QLKeyColumnValuesToPrimitiveValues(
      request.hashed_column_values(), schema, 0, schema.num_hash_key_columns(),
      &hashed_components));

  SubDocKey start_sub_doc_key;
  // Decode the start SubDocKey from the paging state and set scan start key and hybrid time.
  if (request.has_paging_state() &&
      request.paging_state().has_next_row_key() &&
      !request.paging_state().next_row_key().empty()) {

    KeyBytes start_key_bytes(request.paging_state().next_row_key());
    RETURN_NOT_OK(start_sub_doc_key.FullyDecodeFrom(start_key_bytes.AsSlice()));

    // If we start the scan with a specific primary key, the normal scan spec we return below will
    // not include the static columns if any for the start key. We need to return a separate scan
    // spec to fetch those static columns.
    const DocKey& start_doc_key = start_sub_doc_key.doc_key();
    if (include_static_columns && !start_doc_key.range_group().empty()) {
      const DocKey hashed_doc_key(start_doc_key.hash(), start_doc_key.hashed_group());
      static_row_spec->reset(new DocQLScanSpec(static_projection, hashed_doc_key,
          request.query_id(), request.is_forward_scan()));
    }
  } else if (!request.is_forward_scan() && include_static_columns) {
      const DocKey hashed_doc_key(hash_code ? *hash_code : 0, hashed_components);
      static_row_spec->reset(new DocQLScanSpec(static_projection, hashed_doc_key,
          request.query_id(), /* is_forward_scan = */ true));
  }

  // Construct the scan spec basing on the WHERE condition.
  spec->reset(new DocQLScanSpec(schema, hash_code, max_hash_code, hashed_components,
      request.has_where_expr() ? &request.where_expr().condition() : nullptr,
      request.has_if_expr() ? &request.if_expr().condition() : nullptr,
      request.query_id(), request.is_forward_scan(),
      request.is_forward_scan() && include_static_columns, start_sub_doc_key.doc_key()));
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status QLRocksDBStorage::GetIterator(const PgsqlReadRequestPB& request,
                                     const Schema& projection,
                                     const Schema& schema,
                                     const TransactionOperationContextOpt& txn_op_context,
                                     CoarseTimePoint deadline,
                                     const ReadHybridTime& read_time,
                                     common::YQLRowwiseIteratorIf::UniPtr* iter) const {
  std::unique_ptr<DocRowwiseIterator> doc_iter;

  // Populate dockey from QL key columns.
  auto hash_code = request.has_hash_code() ?
      boost::make_optional<int32_t>(request.hash_code()) : boost::none;
  auto max_hash_code = request.has_max_hash_code() ?
      boost::make_optional<int32_t>(request.max_hash_code()) : boost::none;
  vector<PrimitiveValue> hashed_components;
  RETURN_NOT_OK(InitKeyColumnPrimitiveValues(request.partition_column_values(),
                                             schema,
                                             0,
                                             &hashed_components));

  if (request.has_ybctid_column_value()) {
    CHECK(!request.has_paging_state()) << "ASSERT(!ybctid || !paging_state). Each ybctid value "
      "identifies one row in the table while paging state is only used for multi-row queries.";
    DocKey range_doc_key(schema);
    RETURN_NOT_OK(range_doc_key.DecodeFrom(request.ybctid_column_value().value().binary_value()));
    doc_iter = std::make_unique<DocRowwiseIterator>(
        projection, schema, txn_op_context, doc_db_, deadline, read_time);
    RETURN_NOT_OK(doc_iter->Init(DocPgsqlScanSpec(schema,
                                                  request.stmt_id(),
                                                  range_doc_key)));
  } else {
    SubDocKey start_sub_doc_key;
    ReadHybridTime req_read_time = read_time;
    // Decode the start SubDocKey from the paging state and set scan start key and hybrid time.
    if (request.has_paging_state() &&
        request.paging_state().has_next_row_key() &&
        !request.paging_state().next_row_key().empty()) {
      KeyBytes start_key_bytes(request.paging_state().next_row_key());
      RETURN_NOT_OK(start_sub_doc_key.FullyDecodeFrom(start_key_bytes.AsSlice()));
      req_read_time.read = start_sub_doc_key.hybrid_time();
    }

    doc_iter = std::make_unique<DocRowwiseIterator>(
        projection, schema, txn_op_context, doc_db_, deadline, req_read_time);

    if (request.range_column_values().size() > 0) {
      // Construct the scan spec basing on the RANGE condition.
      vector<PrimitiveValue> range_components;
      RETURN_NOT_OK(InitKeyColumnPrimitiveValues(request.range_column_values(),
                                                 schema,
                                                 schema.num_hash_key_columns(),
                                                 &range_components));
      RETURN_NOT_OK(doc_iter->Init(DocPgsqlScanSpec(schema,
                                                    request.stmt_id(),
                                                    hashed_components.empty()
                                                      ? DocKey(schema, range_components)
                                                      : DocKey(schema,
                                                               request.hash_code(),
                                                               hashed_components,
                                                               range_components),
                                                    start_sub_doc_key.doc_key(),
                                                    request.is_forward_scan())));
    } else {
      // Construct the scan spec basing on the WHERE condition.
      CHECK(!request.has_where_expr()) << "WHERE clause is not yet supported in docdb::pgsql";
      RETURN_NOT_OK(doc_iter->Init(DocPgsqlScanSpec(schema,
                                                    request.stmt_id(),
                                                    hashed_components,
                                                    request.has_condition_expr()
                                                      ? &request.condition_expr().condition()
                                                      : nullptr,
                                                    hash_code,
                                                    max_hash_code,
                                                    request.has_where_expr()
                                                      ? &request.where_expr()
                                                      : nullptr,
                                                    start_sub_doc_key.doc_key(),
                                                    request.is_forward_scan())));
    }
  }

  *iter = std::move(doc_iter);
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb

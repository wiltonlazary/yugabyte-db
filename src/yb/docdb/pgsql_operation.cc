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

#include "yb/docdb/pgsql_operation.h"

#include <boost/optional/optional_io.hpp>

#include "yb/common/partition.h"
#include "yb/common/ql_storage_interface.h"
#include "yb/common/ql_value.h"

#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/primitive_value_util.h"

#include "yb/util/trace.h"

DECLARE_bool(trace_docdb_calls);
DECLARE_int64(retryable_rpc_single_call_timeout_ms);

DEFINE_double(ysql_scan_timeout_multiplier, 0.5,
              "YSQL read scan timeout multipler of retryable_rpc_single_call_timeout_ms.");

namespace yb {
namespace docdb {

namespace {

CHECKED_STATUS CreateProjection(const Schema& schema,
                                const PgsqlColumnRefsPB& column_refs,
                                Schema* projection) {
  // Create projection of non-primary key columns. Primary key columns are implicitly read by DocDB.
  // It will also sort the columns before scanning.
  vector<ColumnId> column_ids;
  column_ids.reserve(column_refs.ids_size());
  for (int32_t id : column_refs.ids()) {
    const ColumnId column_id(id);
    if (!schema.is_key_column(column_id)) {
      column_ids.emplace_back(column_id);
    }
  }
  return schema.CreateProjectionByIdsIgnoreMissing(column_ids, projection);
}

} // namespace

//--------------------------------------------------------------------------------------------------

Status PgsqlWriteOperation::Init(PgsqlWriteRequestPB* request, PgsqlResponsePB* response) {
  // Initialize operation inputs.
  request_.Swap(request);
  response_ = response;

  // Init DocDB key using either ybctid or partition and range values.
  if (request_.has_ybctid_column_value()) {
    const string& ybctid = request_.ybctid_column_value().value().binary_value();
    SCHECK(!ybctid.empty(), InternalError, "empty ybctid");
    doc_key_.emplace(schema_);
    RETURN_NOT_OK(doc_key_->DecodeFrom(ybctid));
  } else {
    vector<PrimitiveValue> hashed_components;
    vector<PrimitiveValue> range_components;
    RETURN_NOT_OK(InitKeyColumnPrimitiveValues(request_.partition_column_values(),
                                               schema_,
                                               0,
                                               &hashed_components));
    RETURN_NOT_OK(InitKeyColumnPrimitiveValues(request_.range_column_values(),
                                               schema_,
                                               schema_.num_hash_key_columns(),
                                               &range_components));
    if (hashed_components.empty()) {
      doc_key_.emplace(schema_, range_components);
    } else {
      doc_key_.emplace(schema_, request_.hash_code(), hashed_components, range_components);
    }
  }
  encoded_doc_key_ = doc_key_->EncodeAsRefCntPrefix();

  return Status::OK();
}

Status PgsqlWriteOperation::Apply(const DocOperationApplyData& data) {
  VLOG(4) << "Write, read time: " << data.read_time << ", txn: " << txn_op_context_;

  switch (request_.stmt_type()) {
    case PgsqlWriteRequestPB::PGSQL_INSERT:
      return ApplyInsert(data, IsUpsert::kFalse);

    case PgsqlWriteRequestPB::PGSQL_UPDATE:
      return ApplyUpdate(data);

    case PgsqlWriteRequestPB::PGSQL_DELETE:
      return ApplyDelete(data);

    case PgsqlWriteRequestPB::PGSQL_UPSERT:
      return ApplyInsert(data, IsUpsert::kTrue);
  }
  return Status::OK();
}

Status PgsqlWriteOperation::ApplyInsert(const DocOperationApplyData& data, IsUpsert is_upsert) {
  QLTableRow::SharedPtr table_row = std::make_shared<QLTableRow>();
  if (!is_upsert) {
    RETURN_NOT_OK(ReadColumns(data, table_row));
    if (!table_row->IsEmpty()) {
      VLOG(4) << "Duplicate row: " << table_row->ToString();
      // Primary key or unique index value found.
      response_->set_status(PgsqlResponsePB::PGSQL_STATUS_DUPLICATE_KEY_ERROR);
      response_->set_error_message("Duplicate key found in primary key or unique index");
      return Status::OK();
    }
  }

  // Add the liveness column.
  static const PrimitiveValue kLivenessColumnId =
      PrimitiveValue::SystemColumnId(SystemColumnIds::kLivenessColumn);

  RETURN_NOT_OK(data.doc_write_batch->SetPrimitive(
      DocPath(encoded_doc_key_.as_slice(), kLivenessColumnId),
      Value(PrimitiveValue()),
      data.read_time, data.deadline, request_.stmt_id()));

  for (const auto& column_value : request_.column_values()) {
    // Get the column.
    if (!column_value.has_column_id()) {
      return STATUS(InternalError, "column id missing", column_value.DebugString());
    }
    const ColumnId column_id(column_value.column_id());
    const ColumnSchema& column = VERIFY_RESULT(schema_.column_by_id(column_id));

    // Check column-write operator.
    CHECK(GetTSWriteInstruction(column_value.expr()) == bfpg::TSOpcode::kScalarInsert)
      << "Illegal write instruction";

    // Evaluate column value.
    QLValue expr_result;
    RETURN_NOT_OK(EvalExpr(column_value.expr(), table_row, &expr_result));
    const SubDocument sub_doc =
        SubDocument::FromQLValuePB(expr_result.value(), column.sorting_type());

    // Inserting into specified column.
    DocPath sub_path(encoded_doc_key_.as_slice(), PrimitiveValue(column_id));
    RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
        sub_path, sub_doc, data.read_time, data.deadline, request_.stmt_id()));
  }

  RETURN_NOT_OK(PopulateResultSet(table_row));

  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

Status PgsqlWriteOperation::ApplyUpdate(const DocOperationApplyData& data) {
  QLTableRow::SharedPtr table_row = std::make_shared<QLTableRow>();
  RETURN_NOT_OK(ReadColumns(data, table_row));
  if (table_row->IsEmpty()) {
    // Row not found.
    response_->set_skipped(true);
    return Status::OK();
  }

  // skipped is set to false if this operation produces some data to write.
  bool skipped = true;

  if (request_.has_ybctid_column_value()) {
    for (const auto& column_value : request_.column_new_values()) {
      // Get the column.
      if (!column_value.has_column_id()) {
        return STATUS(InternalError, "column id missing", column_value.DebugString());
      }
      const ColumnId column_id(column_value.column_id());
      const ColumnSchema& column = VERIFY_RESULT(schema_.column_by_id(column_id));

      // Check column-write operator.
      CHECK(GetTSWriteInstruction(column_value.expr()) == bfpg::TSOpcode::kScalarInsert)
        << "Illegal write instruction";

      // Evaluate column value.
      QLValue expr_result;
      RETURN_NOT_OK(EvalExpr(column_value.expr(), table_row, &expr_result));

      // Inserting into specified column.
      const SubDocument sub_doc =
          SubDocument::FromQLValuePB(expr_result.value(), column.sorting_type());

      DocPath sub_path(encoded_doc_key_.as_slice(), PrimitiveValue(column_id));
      RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
          sub_path, sub_doc, data.read_time, data.deadline, request_.stmt_id()));
      skipped = false;
    }
  } else {
    // This UPDATE is calling PGGATE directly without going thru PosgreSQL layer.
    // Keep it here as we might need it.

    // Very limited support for where expressions. Only used for updates to the sequences data
    // table.
    bool is_match = true;
    if (request_.has_where_expr()) {
      QLValue match;
      RETURN_NOT_OK(EvalExpr(request_.where_expr(), table_row, &match));
      is_match = match.bool_value();
    }

    if (is_match) {
      for (const auto &column_value : request_.column_new_values()) {
        // Get the column.
        if (!column_value.has_column_id()) {
          return STATUS(InternalError, "column id missing", column_value.DebugString());
        }
        const ColumnId column_id(column_value.column_id());
        const ColumnSchema& column = VERIFY_RESULT(schema_.column_by_id(column_id));

        // Check column-write operator.
        CHECK(GetTSWriteInstruction(column_value.expr()) == bfpg::TSOpcode::kScalarInsert)
        << "Illegal write instruction";

        // Evaluate column value.
        QLValue expr_result;
        RETURN_NOT_OK(EvalExpr(column_value.expr(), table_row, &expr_result));

        const SubDocument sub_doc =
            SubDocument::FromQLValuePB(expr_result.value(), column.sorting_type());

        // Inserting into specified column.
        DocPath sub_path(encoded_doc_key_.as_slice(), PrimitiveValue(column_id));
        RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
            sub_path, sub_doc, data.read_time, data.deadline, request_.stmt_id()));
        skipped = false;
      }
    }
  }

  // Returning the values before the update.
  RETURN_NOT_OK(PopulateResultSet(table_row));

  if (skipped) {
    response_->set_skipped(true);
  }
  response_->set_rows_affected_count(1);
  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

Status PgsqlWriteOperation::ApplyDelete(const DocOperationApplyData& data) {
  QLTableRow::SharedPtr table_row = std::make_shared<QLTableRow>();
  RETURN_NOT_OK(ReadColumns(data, table_row));
  if (table_row->IsEmpty()) {
    // Row not found.
    response_->set_skipped(true);
    return Status::OK();
  }

  // TODO(neil) Add support for WHERE clause.
  CHECK(request_.column_values_size() == 0) << "WHERE clause condition is not yet fully supported";

  // Otherwise, delete the referenced row (all columns).
  RETURN_NOT_OK(data.doc_write_batch->DeleteSubDoc(DocPath(
      encoded_doc_key_.as_slice()), data.read_time, data.deadline));

  RETURN_NOT_OK(PopulateResultSet(table_row));

  response_->set_rows_affected_count(1);
  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

Status PgsqlWriteOperation::ReadColumns(const DocOperationApplyData& data,
                                        const QLTableRow::SharedPtr& table_row) {
  // Filter the columns using primary key.
  if (doc_key_) {
    Schema projection;
    RETURN_NOT_OK(CreateProjection(schema_, request_.column_refs(), &projection));
    DocPgsqlScanSpec spec(projection, request_.stmt_id(), *doc_key_);
    DocRowwiseIterator iterator(projection,
                                schema_,
                                txn_op_context_,
                                data.doc_write_batch->doc_db(),
                                data.deadline,
                                data.read_time);
    RETURN_NOT_OK(iterator.Init(spec));
    if (VERIFY_RESULT(iterator.HasNext())) {
      RETURN_NOT_OK(iterator.NextRow(table_row.get()));
    } else {
      table_row->Clear();
    }
    data.restart_read_ht->MakeAtLeast(iterator.RestartReadHt());
  }

  return Status::OK();
}

Status PgsqlWriteOperation::PopulateResultSet(const QLTableRow::SharedPtr& table_row) {
  PgsqlRSRow* rsrow = resultset_.AllocateRSRow(request_.targets().size());
  int rscol_index = 0;
  for (const PgsqlExpressionPB& expr : request_.targets()) {
    if (expr.has_column_id()) {
      if (expr.column_id() == static_cast<int>(PgSystemAttrNum::kYBTupleId)) {
        // Strip cotable id from the serialized DocKey before returning it as ybctid.
        Slice tuple_id = encoded_doc_key_.as_slice();
        if (tuple_id.starts_with(ValueTypeAsChar::kTableId)) {
          tuple_id.remove_prefix(1 + kUuidSize);
        }
        rsrow->rscol(rscol_index)->set_binary_value(tuple_id.data(), tuple_id.size());
      } else {
        RETURN_NOT_OK(EvalExpr(expr, table_row, rsrow->rscol(rscol_index)));
      }
    }
    rscol_index++;
  }
  return Status::OK();
}

Status PgsqlWriteOperation::GetDocPaths(GetDocPathsMode mode,
                                        DocPathsToLock *paths,
                                        IsolationLevel *level) const {
  // When this write operation requires a read, it requires a read snapshot so paths will be locked
  // in snapshot isolation for consistency. Otherwise, pure writes will happen in serializable
  // isolation so that they will serialize but do not conflict with one another.
  //
  // Currently, only keys that are being written are locked, no lock is taken on read at the
  // snapshot isolation level.
  *level = RequireReadSnapshot() ? IsolationLevel::SNAPSHOT_ISOLATION
                                 : IsolationLevel::SERIALIZABLE_ISOLATION;

  if (mode == GetDocPathsMode::kIntents) {
    const google::protobuf::RepeatedPtrField<PgsqlColumnValuePB>* column_values = nullptr;
    if (request_.stmt_type() == PgsqlWriteRequestPB::PGSQL_INSERT ||
        request_.stmt_type() == PgsqlWriteRequestPB::PGSQL_UPSERT) {
      column_values = &request_.column_values();
    } else if (request_.stmt_type() == PgsqlWriteRequestPB::PGSQL_UPDATE) {
      column_values = &request_.column_new_values();
    }

    if (column_values != nullptr && !column_values->empty()) {
      KeyBytes buffer;
      for (const auto& column_value : *column_values) {
        ColumnId column_id(column_value.column_id());
        Slice doc_key = encoded_doc_key_.as_slice();
        buffer.Clear();
        buffer.AppendValueType(ValueType::kColumnId);
        buffer.AppendColumnId(column_id);
        RefCntBuffer path(doc_key.size() + buffer.size());
        memcpy(path.data(), doc_key.data(), doc_key.size());
        memcpy(path.data() + doc_key.size(), buffer.data().c_str(), buffer.size());
        paths->push_back(RefCntPrefix(path));
      }
      return Status::OK();
    }
  }
  if (encoded_doc_key_) {
    paths->push_back(encoded_doc_key_);
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgsqlReadOperation::Execute(const common::YQLStorageIf& ql_storage,
                                   CoarseTimePoint deadline,
                                   const ReadHybridTime& read_time,
                                   const Schema& schema,
                                   const Schema *index_schema,
                                   PgsqlResultSet *resultset,
                                   HybridTime *restart_read_ht) {
  VLOG(4) << "Read, read time: " << read_time << ", txn: " << txn_op_context_;

  size_t row_count_limit = std::numeric_limits<std::size_t>::max();
  if (request_.has_limit()) {
    if (request_.limit() == 0) {
      return Status::OK();
    }
    row_count_limit = request_.limit();
  }

  // Create the projection of regular columns selected by the row block plus any referenced in
  // the WHERE condition. When DocRowwiseIterator::NextRow() populates the value map, it uses this
  // projection only to scan sub-documents. The query schema is used to select only referenced
  // columns and key columns.
  Schema projection;
  Schema index_projection;
  common::YQLRowwiseIteratorIf *iter;

  RETURN_NOT_OK(CreateProjection(schema, request_.column_refs(), &projection));
  RETURN_NOT_OK(ql_storage.GetIterator(request_, projection, schema, txn_op_context_,
                                       deadline, read_time, &table_iter_));

  ColumnId ybbasectid_id;
  if (request_.has_index_request()) {
    const PgsqlReadRequestPB& index_request = request_.index_request();
    RETURN_NOT_OK(CreateProjection(*index_schema, index_request.column_refs(), &index_projection));
    RETURN_NOT_OK(ql_storage.GetIterator(index_request, index_projection, *index_schema,
                                         txn_op_context_, deadline, read_time, &index_iter_));
    iter = index_iter_.get();
    const size_t idx = index_schema->find_column("ybidxbasectid");
    SCHECK_NE(idx, Schema::kColumnNotFound, Corruption, "ybidxbasectid not found in index schema");
    ybbasectid_id = index_schema->column_id(idx);
  } else {
    iter = table_iter_.get();
  }

  if (FLAGS_trace_docdb_calls) {
    TRACE("Initialized iterator");
  }

  // Set scan start time.
  bool scan_time_exceeded = false;
  const int64 scan_time_limit =
    FLAGS_retryable_rpc_single_call_timeout_ms * FLAGS_ysql_scan_timeout_multiplier;
  const MonoTime start_time = MonoTime::Now();

  // Fetching data.
  int match_count = 0;
  QLTableRow::SharedPtr row = std::make_shared<QLTableRow>();
  while (resultset->rsrow_count() < row_count_limit && VERIFY_RESULT(iter->HasNext()) &&
         !scan_time_exceeded) {

    row->Clear();

    // If there is an index request, fetch ybbasectid from the index and use it as ybctid
    // to fetch from the base table. Otherwise, fetch from the base table directly.
    if (request_.has_index_request()) {
      RETURN_NOT_OK(iter->NextRow(row.get()));
      const auto& tuple_id = row->GetValue(ybbasectid_id);
      SCHECK_NE(tuple_id, boost::none, Corruption, "ybbasectid not found in index row");
      if (!VERIFY_RESULT(table_iter_->SeekTuple(tuple_id->binary_value()))) {
        DocKey doc_key;
        RETURN_NOT_OK(doc_key.DecodeFrom(tuple_id->binary_value()));
        return STATUS_FORMAT(Corruption, "ybctid $0 not found in indexed table", doc_key);
      }
      row->Clear();
      RETURN_NOT_OK(table_iter_->NextRow(projection, row.get()));
    } else {
      RETURN_NOT_OK(iter->NextRow(projection, row.get()));
    }

    // Match the row with the where condition before adding to the row block.
    bool is_match = true;
    if (request_.has_where_expr()) {
      QLValue match;
      RETURN_NOT_OK(EvalExpr(request_.where_expr(), row, &match));
      is_match = match.bool_value();
    }
    if (is_match) {
      match_count++;
      if (request_.is_aggregate()) {
        RETURN_NOT_OK(EvalAggregate(row));
      } else {
        RETURN_NOT_OK(PopulateResultSet(row, resultset));
      }
    }

    // Check every row_count_limit matches whether we've exceeded our scan time.
    if (match_count % row_count_limit == 0) {
      const MonoDelta elapsed_time = MonoTime::Now().GetDeltaSince(start_time);
      scan_time_exceeded = elapsed_time.ToMilliseconds() > scan_time_limit;
    }
  }

  if (request_.is_aggregate() && match_count > 0) {
    RETURN_NOT_OK(PopulateAggregate(row, resultset));
  }

  if (FLAGS_trace_docdb_calls) {
    TRACE("Fetched $0 rows.", resultset->rsrow_count());
  }
  *restart_read_ht = iter->RestartReadHt();

  return SetPagingStateIfNecessary(iter, resultset, row_count_limit, scan_time_exceeded);
}

Status PgsqlReadOperation::SetPagingStateIfNecessary(const common::YQLRowwiseIteratorIf* iter,
                                                     const PgsqlResultSet* resultset,
                                                     const size_t row_count_limit,
                                                     const bool scan_time_exceeded) {
  if (resultset->rsrow_count() >= row_count_limit || scan_time_exceeded) {
    SubDocKey next_row_key;
    RETURN_NOT_OK(iter->GetNextReadSubDocKey(&next_row_key));
    // When the "limit" number of rows are returned and we are asked to return the paging state,
    // return the partition key and row key of the next row to read in the paging state if there are
    // still more rows to read. Otherwise, leave the paging state empty which means we are done
    // reading from this tablet.
    if (request_.return_paging_state()) {
      if (!next_row_key.doc_key().empty()) {
        PgsqlPagingStatePB* paging_state = response_.mutable_paging_state();
        paging_state->set_next_partition_key(
            PartitionSchema::EncodeMultiColumnHashValue(next_row_key.doc_key().hash()));
        paging_state->set_next_row_key(next_row_key.Encode().data());
      }
    }
  }

  return Status::OK();
}

Status PgsqlReadOperation::PopulateResultSet(const QLTableRow::SharedPtr& table_row,
                                             PgsqlResultSet *resultset) {
  PgsqlRSRow *rsrow = resultset->AllocateRSRow(request_.targets().size());
  int rscol_index = 0;
  for (const PgsqlExpressionPB& expr : request_.targets()) {
    RETURN_NOT_OK(EvalExpr(expr, table_row, rsrow->rscol(rscol_index)));
    rscol_index++;
  }
  return Status::OK();
}

Status PgsqlReadOperation::GetTupleId(QLValue *result) const {
  // Get row key and save to QLValue.
  // TODO(neil) Check if we need to append a table_id and other info to TupleID. For example, we
  // might need info to make sure the TupleId by itself is a valid reference to a specific row of
  // a valid table.
  const Slice tuple_id = VERIFY_RESULT(table_iter_->GetTupleId());
  result->set_binary_value(tuple_id.data(), tuple_id.size());
  return Status::OK();
}

Status PgsqlReadOperation::EvalAggregate(const QLTableRow::SharedPtr& table_row) {
  if (aggr_result_.empty()) {
    int column_count = request_.targets().size();
    aggr_result_.resize(column_count);
  }

  int aggr_index = 0;
  for (const PgsqlExpressionPB& expr : request_.targets()) {
    RETURN_NOT_OK(EvalExpr(expr, table_row, &aggr_result_[aggr_index]));
    aggr_index++;
  }
  return Status::OK();
}

Status PgsqlReadOperation::PopulateAggregate(const QLTableRow::SharedPtr& table_row,
                                             PgsqlResultSet *resultset) {
  int column_count = request_.targets().size();
  PgsqlRSRow *rsrow = resultset->AllocateRSRow(column_count);
  for (int rscol_index = 0; rscol_index < column_count; rscol_index++) {
    *rsrow->rscol(rscol_index) = aggr_result_[rscol_index];
  }
  return Status::OK();
}

Status PgsqlReadOperation::GetIntents(const Schema& schema, KeyValueWriteBatchPB* out) {
  auto pair = out->mutable_read_pairs()->Add();

  if (request_.partition_column_values().empty()) {
    // Empty components mean that we don't have primary key at all, but request
    // could still contain hash_code as part of tablet routing.
    // So we should ignore it.
    DocKey doc_key(schema.cotable_id());
    pair->set_key(doc_key.Encode().data());
  } else {
    std::vector<PrimitiveValue> hashed_components;
    RETURN_NOT_OK(InitKeyColumnPrimitiveValues(
        request_.partition_column_values(), schema, 0 /* start_idx */, &hashed_components));

    DocKey doc_key(schema.cotable_id(), request_.hash_code(), hashed_components);
    pair->set_key(doc_key.Encode().data());
  }

  pair->set_value(std::string(1, ValueTypeAsChar::kNullLow));
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb

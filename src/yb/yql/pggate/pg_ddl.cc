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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_ddl.h"

#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/namespace_alterer.h"
#include "yb/client/yb_op.h"

#include "yb/common/entity_ids.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace std::literals;  // NOLINT

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;

// TODO(neil) This should be derived from a GFLAGS.
static MonoDelta kSessionTimeout = 60s;

//--------------------------------------------------------------------------------------------------
// PgCreateDatabase
//--------------------------------------------------------------------------------------------------

PgCreateDatabase::PgCreateDatabase(PgSession::ScopedRefPtr pg_session,
                                   const char *database_name,
                                   const PgOid database_oid,
                                   const PgOid source_database_oid,
                                   const PgOid next_oid,
                                   const bool colocated)
    : PgDdl(std::move(pg_session)),
      database_name_(database_name),
      database_oid_(database_oid),
      source_database_oid_(source_database_oid),
      next_oid_(next_oid),
      colocated_(colocated) {
}

PgCreateDatabase::~PgCreateDatabase() {
}

Status PgCreateDatabase::Exec() {
  return pg_session_->CreateDatabase(database_name_, database_oid_, source_database_oid_,
                                     next_oid_, colocated_);
}

PgDropDatabase::PgDropDatabase(PgSession::ScopedRefPtr pg_session,
                               const char *database_name,
                               PgOid database_oid)
    : PgDdl(pg_session),
      database_name_(database_name),
      database_oid_(database_oid) {
}

PgDropDatabase::~PgDropDatabase() {
}

Status PgDropDatabase::Exec() {
  return pg_session_->DropDatabase(database_name_, database_oid_);
}

PgAlterDatabase::PgAlterDatabase(PgSession::ScopedRefPtr pg_session,
                               const char *database_name,
                               PgOid database_oid)
    : PgDdl(pg_session),
      namespace_alterer_(pg_session_->NewNamespaceAlterer(database_name, database_oid)) {
}

PgAlterDatabase::~PgAlterDatabase() {
  delete namespace_alterer_;
}

Status PgAlterDatabase::Exec() {
  return namespace_alterer_->SetDatabaseType(YQL_DATABASE_PGSQL)->Alter();
}

Status PgAlterDatabase::RenameDatabase(const char *newname) {
  namespace_alterer_->RenameTo(newname);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// PgCreateTable
//--------------------------------------------------------------------------------------------------

PgCreateTable::PgCreateTable(PgSession::ScopedRefPtr pg_session,
                             const char *database_name,
                             const char *schema_name,
                             const char *table_name,
                             const PgObjectId& table_id,
                             bool is_shared_table,
                             bool if_not_exist,
                             bool add_primary_key)
    : PgDdl(pg_session),
      table_name_(YQL_DATABASE_PGSQL,
                  GetPgsqlNamespaceId(table_id.database_oid),
                  database_name,
                  table_name),
      table_id_(table_id),
      num_tablets_(-1),
      is_pg_catalog_table_(strcmp(schema_name, "pg_catalog") == 0 ||
                           strcmp(schema_name, "information_schema") == 0),
      is_shared_table_(is_shared_table),
      if_not_exist_(if_not_exist) {
  // Add internal primary key column to a Postgres table without a user-specified primary key.
  if (add_primary_key) {
    // For regular user table, ybrowid should be a hash key because ybrowid is a random uuid.
    // For sys catalog table, it should be a range key because sys catalog table is an
    // unpartitioned table in a single tablet.
    bool is_hash = !is_pg_catalog_table_;
    CHECK_OK(AddColumn("ybrowid", static_cast<int32_t>(PgSystemAttrNum::kYBRowId),
                       YB_YQL_DATA_TYPE_BINARY, is_hash, true /* is_range */));
  }
}

PgCreateTable::~PgCreateTable() {
}

Status PgCreateTable::AddColumnImpl(const char *attr_name,
                                    int attr_num,
                                    int attr_ybtype,
                                    bool is_hash,
                                    bool is_range,
                                    ColumnSchema::SortingType sorting_type) {
  shared_ptr<QLType> yb_type = QLType::Create(static_cast<DataType>(attr_ybtype));
  client::YBColumnSpec* col = schema_builder_.AddColumn(attr_name)->Type(yb_type)->Order(attr_num);
  if (is_hash) {
    if (!range_columns_.empty()) {
      return STATUS(InvalidArgument, "Hash column not allowed after an ASC/DESC column");
    }
    if (sorting_type != ColumnSchema::SortingType::kNotSpecified) {
      return STATUS(InvalidArgument, "Hash column can't have sorting order");
    }
    col->HashPrimaryKey();
    hash_schema_ = YBHashSchema::kPgsqlHash;
  } else if (is_range) {
    col->PrimaryKey();
    range_columns_.emplace_back(attr_name);
  }
  col->SetSortingType(sorting_type);
  return Status::OK();
}

Status PgCreateTable::SetNumTablets(int32_t num_tablets) {
  if (num_tablets > FLAGS_max_num_tablets_for_table) {
    return STATUS(InvalidArgument, "num_tablets exceeds system limit");
  }
  num_tablets_ = num_tablets;
  return Status::OK();
}

void PgCreateTable::SetColocated(bool colocated) {
  colocated_ = colocated;
}

Status PgCreateTable::Exec() {
  // Construct schema.
  client::YBSchema schema;

  TableProperties table_properties;
  const char* pg_txn_enabled_env_var = getenv("YB_PG_TRANSACTIONS_ENABLED");
  const bool transactional =
      !pg_txn_enabled_env_var || strcmp(pg_txn_enabled_env_var, "1") == 0;
  LOG(INFO) << Format(
      "PgCreateTable: creating a $0 table: $1",
      transactional ? "transactional" : "non-transactional", table_name_.ToString());
  if (transactional) {
    table_properties.SetTransactional(true);
    schema_builder_.SetTableProperties(table_properties);
  }

  RETURN_NOT_OK(schema_builder_.Build(&schema));

  // Create table.
  shared_ptr<client::YBTableCreator> table_creator(pg_session_->NewTableCreator());
  table_creator->table_name(table_name_).table_type(client::YBTableType::PGSQL_TABLE_TYPE)
                .table_id(table_id_.GetYBTableId())
                .num_tablets(num_tablets_)
                .schema(&schema)
                .colocated(colocated_);
  if (is_pg_catalog_table_) {
    table_creator->is_pg_catalog_table();
  }
  if (is_shared_table_) {
    table_creator->is_pg_shared_table();
  }
  if (hash_schema_) {
    table_creator->hash_schema(*hash_schema_);
  } else if (!is_pg_catalog_table_) {
    table_creator->set_range_partition_columns(range_columns_);
  }

  // For index, set indexed (base) table id.
  if (indexed_table_id()) {
    table_creator->indexed_table_id(indexed_table_id()->GetYBTableId());
  }
  if (is_unique_index()) {
    table_creator->is_unique_index(true);
  }

  const Status s = table_creator->Create();
  if (PREDICT_FALSE(!s.ok())) {
    if (s.IsAlreadyPresent()) {
      if (if_not_exist_) {
        return Status::OK();
      }
      return STATUS(InvalidArgument, "Duplicate table");
    }
    if (s.IsNotFound()) {
      return STATUS(InvalidArgument, "Database not found", table_name_.namespace_name());
    }
    return STATUS_FORMAT(
        InvalidArgument, "Invalid table definition: $0",
        s.ToString(false /* include_file_and_line */, false /* include_code */));
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// PgDropTable
//--------------------------------------------------------------------------------------------------

PgDropTable::PgDropTable(PgSession::ScopedRefPtr pg_session,
                         const PgObjectId& table_id,
                         bool if_exist)
    : PgDdl(pg_session),
      table_id_(table_id),
      if_exist_(if_exist) {
}

PgDropTable::~PgDropTable() {
}

Status PgDropTable::Exec() {
  Status s = pg_session_->DropTable(table_id_);
  if (s.ok() || (s.IsNotFound() && if_exist_)) {
    return Status::OK();
  }
  return s;
}

//--------------------------------------------------------------------------------------------------
// PgTruncateTable
//--------------------------------------------------------------------------------------------------

PgTruncateTable::PgTruncateTable(PgSession::ScopedRefPtr pg_session,
                                 const PgObjectId& table_id)
    : PgDdl(pg_session),
      table_id_(table_id) {
}

PgTruncateTable::~PgTruncateTable() {
}

Status PgTruncateTable::Exec() {
  return pg_session_->TruncateTable(table_id_);
}

//--------------------------------------------------------------------------------------------------
// PgCreateIndex
//--------------------------------------------------------------------------------------------------

PgCreateIndex::PgCreateIndex(PgSession::ScopedRefPtr pg_session,
                             const char *database_name,
                             const char *schema_name,
                             const char *index_name,
                             const PgObjectId& index_id,
                             const PgObjectId& base_table_id,
                             bool is_shared_index,
                             bool is_unique_index,
                             bool if_not_exist,
                             bool colocated)
    : PgCreateTable(pg_session, database_name, schema_name, index_name, index_id,
                    is_shared_index, if_not_exist, false /* add_primary_key */),
      base_table_id_(base_table_id),
      is_unique_index_(is_unique_index) {
  SetColocated(colocated);
}

PgCreateIndex::~PgCreateIndex() {
}

Status PgCreateIndex::AddYBbasectidColumn() {
  // Add YBUniqueIdxKeySuffix column to store key suffix for handling multiple NULL values in column
  // with unique index.
  // Value of this column is set to ybctid (same as ybbasectid) for index row in case index
  // is unique and at least one of its key column is NULL.
  // In all other case value of this column is NULL.
  if (is_unique_index_) {
    RETURN_NOT_OK(
        PgCreateTable::AddColumnImpl("ybuniqueidxkeysuffix",
                                     to_underlying(PgSystemAttrNum::kYBUniqueIdxKeySuffix),
                                     YB_YQL_DATA_TYPE_BINARY,
                                     false /* is_hash */,
                                     true /* is_range */));
  }

  // Add ybbasectid column to store the ybctid of the rows in the indexed table. It should be added
  // at the end of the primary key of the index, i.e. either before any non-primary-key column if
  // any or before exec() below.
  RETURN_NOT_OK(PgCreateTable::AddColumnImpl("ybidxbasectid",
                                             to_underlying(PgSystemAttrNum::kYBIdxBaseTupleId),
                                             YB_YQL_DATA_TYPE_BINARY,
                                             false /* is_hash */,
                                             !is_unique_index_ /* is_range */));
  ybbasectid_added_ = true;
  return Status::OK();
}

Status PgCreateIndex::AddColumnImpl(const char *attr_name,
                                    int attr_num,
                                    int attr_ybtype,
                                    bool is_hash,
                                    bool is_range,
                                    ColumnSchema::SortingType sorting_type) {
  if (!is_hash && !is_range && !ybbasectid_added_) {
    RETURN_NOT_OK(AddYBbasectidColumn());
  }
  return PgCreateTable::AddColumnImpl(attr_name, attr_num, attr_ybtype,
      is_hash, is_range, sorting_type);
}

Status PgCreateIndex::Exec() {
  if (!ybbasectid_added_) {
    RETURN_NOT_OK(AddYBbasectidColumn());
  }
  return PgCreateTable::Exec();
}

//--------------------------------------------------------------------------------------------------
// PgDropIndex
//--------------------------------------------------------------------------------------------------

PgDropIndex::PgDropIndex(PgSession::ScopedRefPtr pg_session,
                         const PgObjectId& index_id,
                         bool if_exist)
    : PgDropTable(pg_session, index_id, if_exist) {
}

PgDropIndex::~PgDropIndex() {
}

Status PgDropIndex::Exec() {
  Status s = pg_session_->DropIndex(table_id_);
  if (s.ok() || (s.IsNotFound() && if_exist_)) {
    return Status::OK();
  }
  return s;
}

//--------------------------------------------------------------------------------------------------
// PgAlterTable
//--------------------------------------------------------------------------------------------------

PgAlterTable::PgAlterTable(PgSession::ScopedRefPtr pg_session,
                           const PgObjectId& table_id)
    : PgDdl(pg_session),
      table_id_(table_id),
      table_alterer(pg_session_->NewTableAlterer(table_id.GetYBTableId())) {
}

Status PgAlterTable::AddColumn(const char *name,
                               const YBCPgTypeEntity *attr_type,
                               int order,
                               bool is_not_null) {
  shared_ptr<QLType> yb_type = QLType::Create(static_cast<DataType>(attr_type->yb_type));

  client::YBColumnSpec* column = table_alterer->AddColumn(name)->Type(yb_type)->Order(order);
  if (is_not_null) column->NotNull();

  return Status::OK();
}

Status PgAlterTable::RenameColumn(const char *oldname, const char *newname) {
  table_alterer->AlterColumn(oldname)->RenameTo(newname);
  return Status::OK();
}

Status PgAlterTable::DropColumn(const char *name) {
  table_alterer->DropColumn(name);
  return Status::OK();
}

Status PgAlterTable::RenameTable(const char *db_name, const char *newname) {
  client::YBTableName new_table_name(YQL_DATABASE_PGSQL, db_name, newname);
  table_alterer->RenameTo(new_table_name);
  return Status::OK();
}

Status PgAlterTable::Exec() {
  Status s = table_alterer->Alter();
  pg_session_->InvalidateTableCache(table_id_);
  return s;
}

PgAlterTable::~PgAlterTable() {
}

}  // namespace pggate
}  // namespace yb

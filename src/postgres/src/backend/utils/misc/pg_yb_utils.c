/*-------------------------------------------------------------------------
 *
 * pg_yb_utils.c
 *	  Utilities for YugaByte/PostgreSQL integration that have to be defined on
 *	  the PostgreSQL side.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *	  src/backend/utils/misc/pg_yb_utils.c
 *
 *-------------------------------------------------------------------------
 */

#include <sys/types.h>
#include <unistd.h>

#include "postgres.h"
#include "miscadmin.h"
#include "access/sysattr.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "catalog/pg_database.h"
#include "utils/builtins.h"
#include "catalog/pg_type.h"
#include "catalog/catalog.h"
#include "commands/dbcommands.h"

#include "pg_yb_utils.h"
#include "catalog/ybctype.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "common/pg_yb_common.h"

#include "utils/resowner_private.h"

#include "fmgr.h"
#include "access/htup.h"
#include "access/htup_details.h"
#include "access/tupdesc.h"

YBCPgSession ybc_pg_session = NULL;

uint64 yb_catalog_cache_version = YB_CATCACHE_VERSION_UNINITIALIZED;

/** These values are lazily initialized based on corresponding environment variables. */
int ybc_pg_double_write = -1;
int ybc_disable_pg_locking = -1;

YBCStatus ybc_commit_status = NULL;

bool
IsYugaByteEnabled()
{
	/* We do not support Init/Bootstrap processing modes yet. */
	return ybc_pg_session != NULL;
}

void
CheckIsYBSupportedRelation(Relation relation)
{
	const char relkind = relation->rd_rel->relkind;
	CheckIsYBSupportedRelationByKind(relkind);
}

void
CheckIsYBSupportedRelationByKind(char relkind)
{
	if (!(relkind == RELKIND_RELATION || relkind == RELKIND_INDEX ||
		  relkind == RELKIND_VIEW || relkind == RELKIND_SEQUENCE ||
		  relkind == RELKIND_COMPOSITE_TYPE))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg("This feature is not supported in YugaByte.")));
}

bool
IsYBRelation(Relation relation)
{
	if (!IsYugaByteEnabled()) return false;

	const char relkind = relation->rd_rel->relkind;

	CheckIsYBSupportedRelationByKind(relkind);

	/* Currently only support regular tables and indexes.
	 * Temp tables and views are supported, but they are not YB relations. */
	return (relkind == RELKIND_RELATION || relkind == RELKIND_INDEX)
				 && relation->rd_rel->relpersistence != RELPERSISTENCE_TEMP;
}

bool
IsYBRelationById(Oid relid)
{
	Relation relation     = RelationIdGetRelation(relid);
	bool     is_supported = IsYBRelation(relation);
	RelationClose(relation);
	return is_supported;
}

bool
IsYBBackedRelation(Relation relation) 
{
	return IsYBRelation(relation) ||
		(relation->rd_rel->relkind == RELKIND_VIEW &&
		relation->rd_rel->relpersistence != RELPERSISTENCE_TEMP);
}

bool
YBNeedRetryAfterCacheRefresh(ErrorData *edata)
{
	// TODO Inspect error code to distinguish retryable errors.
	return true;
}

AttrNumber YBGetFirstLowInvalidAttributeNumber(Relation relation)
{
	return IsYBRelation(relation)
	       ? YBFirstLowInvalidAttributeNumber
	       : FirstLowInvalidHeapAttributeNumber;
}

AttrNumber YBGetFirstLowInvalidAttributeNumberFromOid(Oid relid)
{
	Relation   relation = RelationIdGetRelation(relid);
	AttrNumber attr_num = YBGetFirstLowInvalidAttributeNumber(relation);
	RelationClose(relation);
	return attr_num;
}

extern bool YBRelHasOldRowTriggers(Relation rel, CmdType operation)
{
	TriggerDesc *trigdesc = rel->trigdesc;
	return (trigdesc &&
		((operation == CMD_UPDATE &&
			(trigdesc->trig_update_after_row ||
			trigdesc->trig_update_before_row)) ||
		(operation == CMD_DELETE &&
			(trigdesc->trig_delete_after_row || 
			trigdesc->trig_delete_before_row))));
}

bool
YBRelHasSecondaryIndices(Relation relation)
{
	if (!relation->rd_rel->relhasindex)
		return false;

	bool	 has_indices = false;
	List	 *indexlist = RelationGetIndexList(relation);
	ListCell *lc;

	foreach(lc, indexlist)
	{
		if (lfirst_oid(lc) == relation->rd_pkindex)
			continue;
		has_indices = true;
		break;
	}

	list_free(indexlist);

	return has_indices;
}

bool
YBTransactionsEnabled()
{
	static int cached_value = -1;
	if (cached_value == -1)
	{
		cached_value = YBCIsEnvVarTrueWithDefault("YB_PG_TRANSACTIONS_ENABLED", true);
	}
	return IsYugaByteEnabled() && cached_value;
}

void
YBReportFeatureUnsupported(const char *msg)
{
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("%s", msg)));
}


static bool
YBShouldReportErrorStatus()
{
	static int cached_value = -1;
	if (cached_value == -1)
	{
		cached_value = YBCIsEnvVarTrue("YB_PG_REPORT_ERROR_STATUS");
	}

	return cached_value;
}

char* DupYBStatusMessage(YBCStatus status) {
  const char* code_as_cstring = YBCStatusCodeAsCString(status);
  size_t code_strlen = strlen(code_as_cstring);
	size_t status_len = YBCStatusMessageLen(status);
	char* msg_buf = palloc(code_strlen + status_len + 3);
	char* pos = msg_buf;
	memcpy(msg_buf, code_as_cstring, code_strlen);
	pos += code_strlen;
	*pos++ = ':';
	*pos++ = ' ';
	memcpy(pos, YBCStatusMessageBegin(status), status_len);
	pos[status_len] = 0;
	return msg_buf;
}

void
HandleYBStatus(YBCStatus status)
{
	if (!status) {
    return;
  }
	/* Copy the message to the current memory context and free the YBCStatus. */
	char* msg_buf = DupYBStatusMessage(status);

	if (YBShouldReportErrorStatus()) {
		YBC_LOG_ERROR("HandleYBStatus: %s", msg_buf);
	}
	const uint32_t pg_err_code  = YBCStatusPgsqlError(status);
	const uint16_t txn_err_code = YBCStatusTransactionError(status);
	YBCFreeStatus(status);
	ereport(ERROR,
			(errmsg("%s", msg_buf),
			 errcode(pg_err_code),
			 yb_txn_errcode(txn_err_code)));
}

void
HandleYBStmtStatus(YBCStatus status, YBCPgStatement ybc_stmt)
{
	if (!status)
		return;

	if (ybc_stmt)
	{
		HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));
	}
	HandleYBStatus(status);
}

void
HandleYBStmtStatusWithOwner(YBCStatus status,
                            YBCPgStatement ybc_stmt,
                            ResourceOwner owner)
{
	if (!status)
		return;

	if (ybc_stmt)
	{
		HandleYBStatus(YBCPgDeleteStatement(ybc_stmt));
		if (owner != NULL)
		{
			ResourceOwnerForgetYugaByteStmt(owner, ybc_stmt);
		}
	}
	HandleYBStatus(status);
}

void
HandleYBTableDescStatus(YBCStatus status, YBCPgTableDesc table)
{
	if (!status)
		return;

	if (table)
	{
		HandleYBStatus(YBCPgDeleteTableDesc(table));
	}
	HandleYBStatus(status);
}

void
YBInitPostgresBackend(
	const char *program_name,
	const char *db_name,
	const char *user_name)
{
	HandleYBStatus(YBCInit(program_name, palloc, cstring_to_text_with_len));

	/*
	 * Enable "YB mode" for PostgreSQL so that we will initiate a connection
	 * to the YugaByte cluster right away from every backend process. We only

	 * do this if this env variable is set, so we can still run the regular
	 * PostgreSQL "make check".
	 */
	if (YBIsEnabledInPostgresEnvVar())
	{
		const YBCPgTypeEntity *type_table;
		int count;
		YBCGetTypeTable(&type_table, &count);
		YBCInitPgGate(type_table, count);

		if (ybc_pg_session != NULL) {
			YBC_LOG_FATAL("Double initialization of ybc_pg_session");
		}
		/*
		 * For each process, we create one YBC session for PostgreSQL to use
		 * when accessing YugaByte storage.
		 *
		 * TODO: do we really need to DB name / username here?
		 */
		if (db_name != NULL)
		{
			HandleYBStatus(YBCPgCreateSession(
				/* pg_env */ NULL, db_name, &ybc_pg_session));
		}
		else if (user_name != NULL)
		{
			HandleYBStatus(YBCPgCreateSession(
				/* pg_env */ NULL, user_name, &ybc_pg_session));
		}
	}
}

void
YBOnPostgresBackendShutdown()
{
	static bool shutdown_done = false;

	if (shutdown_done)
	{
		return;
	}
	if (ybc_pg_session)
	{
		YBCPgDestroySession(ybc_pg_session);
		ybc_pg_session = NULL;
	}
	YBCDestroyPgGate();
	shutdown_done = true;
}

void
YBCRestartTransaction()
{
	if (!IsYugaByteEnabled())
		return;
	HandleYBStatus(YBCPgTxnManager_RestartTransaction_Status(YBCGetPgTxnManager()));
}

static void
YBCResetCommitStatus()
{
	if (ybc_commit_status)
	{
		YBCFreeStatus(ybc_commit_status);
		ybc_commit_status = NULL;
	}
}

bool
YBCCommitTransaction()
{
	if (!IsYugaByteEnabled())
		return true;

	YBCStatus status =
		YBCPgTxnManager_CommitTransaction_Status(YBCGetPgTxnManager());
	if (status != NULL) {
		YBCResetCommitStatus();
		ybc_commit_status = status;
		return false;
	}

	return true;
}

void
YBCHandleCommitError()
{
	YBCStatus status = ybc_commit_status;
	if (status != NULL) {
    char* msg = DupYBStatusMessage(status);
		YBCResetCommitStatus();
		ereport(ERROR,
				(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
				 errmsg("Error during commit: %s", msg)));
	}
}

bool
YBIsPgLockingEnabled()
{
	return !YBTransactionsEnabled();
}

static bool yb_preparing_templates = false;
void
YBSetPreparingTemplates() {
	yb_preparing_templates = true;
}

bool
YBIsPreparingTemplates() {
	return yb_preparing_templates;
}

const char*
YBPgTypeOidToStr(Oid type_id) {
	switch (type_id) {
		case BOOLOID: return "BOOL";
		case BYTEAOID: return "BYTEA";
		case CHAROID: return "CHAR";
		case NAMEOID: return "NAME";
		case INT8OID: return "INT8";
		case INT2OID: return "INT2";
		case INT2VECTOROID: return "INT2VECTOR";
		case INT4OID: return "INT4";
		case REGPROCOID: return "REGPROC";
		case TEXTOID: return "TEXT";
		case OIDOID: return "OID";
		case TIDOID: return "TID";
		case XIDOID: return "XID";
		case CIDOID: return "CID";
		case OIDVECTOROID: return "OIDVECTOR";
		case JSONOID: return "JSON";
		case XMLOID: return "XML";
		case PGNODETREEOID: return "PGNODETREE";
		case PGNDISTINCTOID: return "PGNDISTINCT";
		case PGDEPENDENCIESOID: return "PGDEPENDENCIES";
		case PGDDLCOMMANDOID: return "PGDDLCOMMAND";
		case POINTOID: return "POINT";
		case LSEGOID: return "LSEG";
		case PATHOID: return "PATH";
		case BOXOID: return "BOX";
		case POLYGONOID: return "POLYGON";
		case LINEOID: return "LINE";
		case FLOAT4OID: return "FLOAT4";
		case FLOAT8OID: return "FLOAT8";
		case ABSTIMEOID: return "ABSTIME";
		case RELTIMEOID: return "RELTIME";
		case TINTERVALOID: return "TINTERVAL";
		case UNKNOWNOID: return "UNKNOWN";
		case CIRCLEOID: return "CIRCLE";
		case CASHOID: return "CASH";
		case MACADDROID: return "MACADDR";
		case INETOID: return "INET";
		case CIDROID: return "CIDR";
		case MACADDR8OID: return "MACADDR8";
		case INT2ARRAYOID: return "INT2ARRAY";
		case INT4ARRAYOID: return "INT4ARRAY";
		case TEXTARRAYOID: return "TEXTARRAY";
		case OIDARRAYOID: return "OIDARRAY";
		case FLOAT4ARRAYOID: return "FLOAT4ARRAY";
		case ACLITEMOID: return "ACLITEM";
		case CSTRINGARRAYOID: return "CSTRINGARRAY";
		case BPCHAROID: return "BPCHAR";
		case VARCHAROID: return "VARCHAR";
		case DATEOID: return "DATE";
		case TIMEOID: return "TIME";
		case TIMESTAMPOID: return "TIMESTAMP";
		case TIMESTAMPTZOID: return "TIMESTAMPTZ";
		case INTERVALOID: return "INTERVAL";
		case TIMETZOID: return "TIMETZ";
		case BITOID: return "BIT";
		case VARBITOID: return "VARBIT";
		case NUMERICOID: return "NUMERIC";
		case REFCURSOROID: return "REFCURSOR";
		case REGPROCEDUREOID: return "REGPROCEDURE";
		case REGOPEROID: return "REGOPER";
		case REGOPERATOROID: return "REGOPERATOR";
		case REGCLASSOID: return "REGCLASS";
		case REGTYPEOID: return "REGTYPE";
		case REGROLEOID: return "REGROLE";
		case REGNAMESPACEOID: return "REGNAMESPACE";
		case REGTYPEARRAYOID: return "REGTYPEARRAY";
		case UUIDOID: return "UUID";
		case LSNOID: return "LSN";
		case TSVECTOROID: return "TSVECTOR";
		case GTSVECTOROID: return "GTSVECTOR";
		case TSQUERYOID: return "TSQUERY";
		case REGCONFIGOID: return "REGCONFIG";
		case REGDICTIONARYOID: return "REGDICTIONARY";
		case JSONBOID: return "JSONB";
		case INT4RANGEOID: return "INT4RANGE";
		case RECORDOID: return "RECORD";
		case RECORDARRAYOID: return "RECORDARRAY";
		case CSTRINGOID: return "CSTRING";
		case ANYOID: return "ANY";
		case ANYARRAYOID: return "ANYARRAY";
		case VOIDOID: return "VOID";
		case TRIGGEROID: return "TRIGGER";
		case EVTTRIGGEROID: return "EVTTRIGGER";
		case LANGUAGE_HANDLEROID: return "LANGUAGE_HANDLER";
		case INTERNALOID: return "INTERNAL";
		case OPAQUEOID: return "OPAQUE";
		case ANYELEMENTOID: return "ANYELEMENT";
		case ANYNONARRAYOID: return "ANYNONARRAY";
		case ANYENUMOID: return "ANYENUM";
		case FDW_HANDLEROID: return "FDW_HANDLER";
		case INDEX_AM_HANDLEROID: return "INDEX_AM_HANDLER";
		case TSM_HANDLEROID: return "TSM_HANDLER";
		case ANYRANGEOID: return "ANYRANGE";
		default: return "user_defined_type";
	}
}

const char*
YBCPgDataTypeToStr(YBCPgDataType yb_type) {
	switch (yb_type) {
		case YB_YQL_DATA_TYPE_NOT_SUPPORTED: return "NOT_SUPPORTED";
		case YB_YQL_DATA_TYPE_UNKNOWN_DATA: return "UNKNOWN_DATA";
		case YB_YQL_DATA_TYPE_NULL_VALUE_TYPE: return "NULL_VALUE_TYPE";
		case YB_YQL_DATA_TYPE_INT8: return "INT8";
		case YB_YQL_DATA_TYPE_INT16: return "INT16";
		case YB_YQL_DATA_TYPE_INT32: return "INT32";
		case YB_YQL_DATA_TYPE_INT64: return "INT64";
		case YB_YQL_DATA_TYPE_STRING: return "STRING";
		case YB_YQL_DATA_TYPE_BOOL: return "BOOL";
		case YB_YQL_DATA_TYPE_FLOAT: return "FLOAT";
		case YB_YQL_DATA_TYPE_DOUBLE: return "DOUBLE";
		case YB_YQL_DATA_TYPE_BINARY: return "BINARY";
		case YB_YQL_DATA_TYPE_TIMESTAMP: return "TIMESTAMP";
		case YB_YQL_DATA_TYPE_DECIMAL: return "DECIMAL";
		case YB_YQL_DATA_TYPE_VARINT: return "VARINT";
		case YB_YQL_DATA_TYPE_INET: return "INET";
		case YB_YQL_DATA_TYPE_LIST: return "LIST";
		case YB_YQL_DATA_TYPE_MAP: return "MAP";
		case YB_YQL_DATA_TYPE_SET: return "SET";
		case YB_YQL_DATA_TYPE_UUID: return "UUID";
		case YB_YQL_DATA_TYPE_TIMEUUID: return "TIMEUUID";
		case YB_YQL_DATA_TYPE_TUPLE: return "TUPLE";
		case YB_YQL_DATA_TYPE_TYPEARGS: return "TYPEARGS";
		case YB_YQL_DATA_TYPE_USER_DEFINED_TYPE: return "USER_DEFINED_TYPE";
		case YB_YQL_DATA_TYPE_FROZEN: return "FROZEN";
		case YB_YQL_DATA_TYPE_DATE: return "DATE";
		case YB_YQL_DATA_TYPE_TIME: return "TIME";
		case YB_YQL_DATA_TYPE_JSONB: return "JSONB";
		case YB_YQL_DATA_TYPE_UINT8: return "UINT8";
		case YB_YQL_DATA_TYPE_UINT16: return "UINT16";
		case YB_YQL_DATA_TYPE_UINT32: return "UINT32";
		case YB_YQL_DATA_TYPE_UINT64: return "UINT64";
		default: return "unknown";
	}
}

void
YBReportIfYugaByteEnabled()
{
	if (YBIsEnabledInPostgresEnvVar()) {
		ereport(LOG, (errmsg(
			"YugaByte is ENABLED in PostgreSQL. Transactions are %s.",
			YBCIsEnvVarTrue("YB_PG_TRANSACTIONS_ENABLED") ?
			"enabled" : "disabled")));
	} else {
		ereport(LOG, (errmsg("YugaByte is NOT ENABLED -- "
							"this is a vanilla PostgreSQL server!")));
	}
}

bool
YBShouldRestartAllChildrenIfOneCrashes() {
	if (!YBIsEnabledInPostgresEnvVar()) {
		ereport(LOG, (errmsg("YBShouldRestartAllChildrenIfOneCrashes returning 0, YBIsEnabledInPostgresEnvVar is false")));
		return true;
	}
	const char* flag_file_path =
		getenv("YB_PG_NO_RESTART_ALL_CHILDREN_ON_CRASH_FLAG_PATH");
	// We will use PostgreSQL's default behavior (restarting all children if one of them crashes)
	// if the flag env variable is not specified or the file pointed by it does not exist.
	return !flag_file_path || access(flag_file_path, F_OK) == -1;
}

bool
YBShouldLogStackTraceOnError()
{
	static int cached_value = -1;
	if (cached_value != -1)
	{
		return cached_value;
	}

	cached_value = YBCIsEnvVarTrue("YB_PG_STACK_TRACE_ON_ERROR");
	return cached_value;
}

const char*
YBPgErrorLevelToString(int elevel) {
	switch (elevel)
	{
		case DEBUG5: return "DEBUG5";
		case DEBUG4: return "DEBUG4";
		case DEBUG3: return "DEBUG3";
		case DEBUG2: return "DEBUG2";
		case DEBUG1: return "DEBUG1";
		case LOG: return "LOG";
		case LOG_SERVER_ONLY: return "LOG_SERVER_ONLY";
		case INFO: return "INFO";
		case WARNING: return "WARNING";
		case ERROR: return "ERROR";
		case FATAL: return "FATAL";
		case PANIC: return "PANIC";
		default: return "UNKNOWN";
	}
}

const char*
YBCGetDatabaseName(Oid relid)
{
	/*
	 * Hardcode the names for system db since the cache might not
	 * be initialized during initdb (bootstrap mode).
	 * For shared rels (e.g. pg_database) we may not have a database id yet,
	 * so assuming template1 in that case since that's where shared tables are
	 * stored in YB.
	 * TODO Eventually YB should switch to using oid's everywhere so
	 * that dbname and schemaname should not be needed at all.
	 */
	if (MyDatabaseId == TemplateDbOid || IsSharedRelation(relid))
		return "template1";
	else
		return get_database_name(MyDatabaseId);
}

const char*
YBCGetSchemaName(Oid schemaoid)
{
	/*
	 * Hardcode the names for system namespaces since the cache might not
	 * be initialized during initdb (bootstrap mode).
	 * TODO Eventually YB should switch to using oid's everywhere so
	 * that dbname and schemaname should not be needed at all.
	 */
	if (IsSystemNamespace(schemaoid))
		return "pg_catalog";
	else if (IsToastNamespace(schemaoid))
		return "pg_toast";
	else
		return get_namespace_name(schemaoid);
}

Oid
YBCGetDatabaseOid(Relation rel)
{
	return rel->rd_rel->relisshared ? TemplateDbOid : MyDatabaseId;
}

void
YBRaiseNotSupported(const char *msg, int issue_no)
{
	int signal_level = YBUnsupportedFeatureSignalLevel();
	if (issue_no > 0)
	{
		ereport(signal_level,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("%s", msg),
				 errhint("See https://github.com/YugaByte/yugabyte-db/issues/%d. "
						 "Click '+' on the description to raise its priority", issue_no)));
	}
	else
	{
		ereport(signal_level,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("%s", msg),
				 errhint("Please report the issue on "
						 "https://github.com/YugaByte/yugabyte-db/issues")));
	}
}

//------------------------------------------------------------------------------
// YB Debug utils.

bool yb_debug_mode = false;

const char*
YBDatumToString(Datum datum, Oid typid)
{
	Oid			typoutput = InvalidOid;
	bool		typisvarlena = false;

	getTypeOutputInfo(typid, &typoutput, &typisvarlena);
	return OidOutputFunctionCall(typoutput, datum);
}

const char*
YBHeapTupleToString(HeapTuple tuple, TupleDesc tupleDesc)
{
	Datum attr = (Datum) 0;
	int natts = tupleDesc->natts;
	bool isnull = false;
	StringInfoData buf;
	initStringInfo(&buf);

	appendStringInfoChar(&buf, '(');
	for (int attnum = 1; attnum <= natts; ++attnum) {
		attr = heap_getattr(tuple, attnum, tupleDesc, &isnull);
		if (isnull) 
		{
			appendStringInfoString(&buf, "null");
		}
		else
		{
			Oid typid = TupleDescAttr(tupleDesc, attnum - 1)->atttypid;
			appendStringInfoString(&buf, YBDatumToString(attr, typid));
		}
		if (attnum != natts) {
			appendStringInfoString(&buf, ", ");
		}
	}
	appendStringInfoChar(&buf, ')');
	return buf.data;
}

bool
YBIsInitDbAlreadyDone()
{
	bool done = false;
	HandleYBStatus(YBCPgIsInitDbDone(ybc_pg_session, &done));
	return done;
}

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
// Tree node definitions for INSERT statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_PTREE_PT_INSERT_H_
#define YB_YQL_CQL_QL_PTREE_PT_INSERT_H_

#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"
#include "yb/yql/cql/ql/ptree/pt_insert_json_clause.h"
#include "yb/yql/cql/ql/ptree/pt_insert_values_clause.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

class PTInsertStmt : public PTDmlStmt {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTInsertStmt> SharedPtr;
  typedef MCSharedPtr<const PTInsertStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTInsertStmt(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               PTQualifiedName::SharedPtr relation,
               PTQualifiedNameListNode::SharedPtr columns,
               const PTCollection::SharedPtr& inserting_value,
               PTExpr::SharedPtr if_clause = nullptr,
               bool else_error = false,
               PTDmlUsingClause::SharedPtr using_clause = nullptr,
               const bool returns_status = false);
  virtual ~PTInsertStmt();

  template<typename... TypeArgs>
  inline static PTInsertStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                   TypeArgs&&... args) {
    return MCMakeShared<PTInsertStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);
  ExplainPlanPB AnalysisResultToPB() override;

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTInsertStmt;
  }

  // Table name.
  client::YBTableName table_name() const override {
    return relation_->ToTableName();
  }

  // Returns location of table name.
  const YBLocation& table_loc() const override {
    return relation_->loc();
  }

  const PTCollection::SharedPtr& InsertingValue() const {
    return inserting_value_;
  }

 private:

  //
  // Analyze helper functions
  //

  CHECKED_STATUS AnalyzeInsertingValue(PTCollection* inserting_value,
                                       SemContext* sem_context);

  CHECKED_STATUS AnanlyzeValuesClause(PTInsertValuesClause* values_clause,
                                      SemContext* sem_context);

  CHECKED_STATUS AnanlyzeJsonClause(PTInsertJsonClause* json_clause,
                                    SemContext* sem_context);

  CHECKED_STATUS ProcessColumn(const MCSharedPtr<MCString>& mc_col_name,
                               const ColumnDesc* col_desc,
                               const PTExpr::SharedPtr& value_expr,
                               SemContext* sem_context);

  // Initialize all non-initialized columns according to their configured defaults
  CHECKED_STATUS InitRemainingColumns(bool is_json_clause,
                                      SemContext* sem_context);

  // --- The parser will decorate this node with the following information --

  PTQualifiedName::SharedPtr relation_;
  PTQualifiedNameListNode::SharedPtr columns_;
  PTCollection::SharedPtr inserting_value_;

  // -- The semantic analyzer will decorate this node with the following information --

};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_PT_INSERT_H_

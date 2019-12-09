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
// Tree node definitions for INSERT INTO ... JSON clause.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_PTREE_PT_INSERT_JSON_CLAUSE_H_
#define YB_YQL_CQL_QL_PTREE_PT_INSERT_JSON_CLAUSE_H_

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"

#include <boost/optional.hpp>
#include <rapidjson/document.h>

namespace yb {
namespace ql {

class PTInsertJsonClause: public PTCollection {
 public:
  // Public types.
  typedef MCSharedPtr<PTInsertJsonClause> SharedPtr;
  typedef MCSharedPtr<const PTInsertJsonClause> SharedPtrConst;

  // Constructor and destructor.
  PTInsertJsonClause(MemoryContext* memctx,
                     const YBLocation::SharedPtr& loc,
                     const PTExpr::SharedPtr& json_expr,
                     bool default_null);
  virtual ~PTInsertJsonClause();

  template<typename... TypeArgs>
  inline static PTInsertJsonClause::SharedPtr MakeShared(MemoryContext* memctx,
                                                         TypeArgs&& ... args) {
    return MCMakeShared<PTInsertJsonClause>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node type.
  TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTInsertJsonClause;
  }

  // Node semantics analysis.
  CHECKED_STATUS Analyze(SemContext* sem_context) override;
  void PrintSemanticAnalysisResult(SemContext* sem_context);

  // Initialize this clause with JSON string and parsed JSON document.
  // Note that you have to std::move the document here.
  CHECKED_STATUS PreExecInit(const std::string& json_string,
                             rapidjson::Document json_document) {
    DCHECK(!json_document_) << "Double call to PreExecInit!";
    DCHECK(json_document.IsObject()) << "Supplied JSON should be an object";
    json_document_ = std::move(json_document);
    json_string_   = json_string;
    return Status::OK();
  }

  bool IsDefaultNull() const {
    return default_null_;
  }

  const PTExpr::SharedPtr& Expr() const {
    return json_expr_;
  }

  const std::string& JsonString() const {
    DCHECK(json_document_) << "JSON not initialized!";
    return json_string_;
  }

  const rapidjson::Document& JsonDocument() const {
    DCHECK(json_document_) << "JSON not initialized!";
    return *json_document_;
  }

 private:
  // Whether non-mentioned columns should be set to NULL, or left unchanged
  bool                                 default_null_;

  // Expression representing raw JSON string, either a string constant or a bind variable.
  const PTExpr::SharedPtr              json_expr_;

  // Raw JSON string, only available after being set via PreExecInit.
  std::string                          json_string_;

  // Parsed JSON object, only available after being set via PreExecInit. Guaranteed to be an Object.
  boost::optional<rapidjson::Document> json_document_;
};

}  // namespace ql
}  // namespace yb

#endif // YB_YQL_CQL_QL_PTREE_PT_INSERT_JSON_CLAUSE_H_

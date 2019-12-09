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
// Tree node definitions for EXPLAIN statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_PTREE_PT_EXPLAIN_H_
#define YB_YQL_CQL_QL_PTREE_PT_EXPLAIN_H_

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

class PTExplainStmt : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTExplainStmt> SharedPtr;
  typedef MCSharedPtr<const PTExplainStmt> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTExplainStmt(MemoryContext *memctx,
                YBLocation::SharedPtr loc,
                TreeNode::SharedPtr stmt);
  virtual ~PTExplainStmt();

  template<typename... TypeArgs>
  inline static PTExplainStmt::SharedPtr MakeShared(MemoryContext *memctx,
                                                    TypeArgs&&... args) {
    return MCMakeShared<PTExplainStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

//  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTExplainStmt;
  }

  TreeNode::SharedPtr stmt() const {
    return stmt_;
  }
 private:
  // --- The parser will decorate this node with the following information --

  TreeNode::SharedPtr stmt_;

  // -- The semantic analyzer will decorate this node with the following information --

};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_PT_EXPLAIN_H_

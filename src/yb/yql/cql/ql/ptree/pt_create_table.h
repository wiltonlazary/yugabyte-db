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
// Tree node definitions for CREATE TABLE statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_PTREE_PT_CREATE_TABLE_H_
#define YB_YQL_CQL_QL_PTREE_PT_CREATE_TABLE_H_

#include "yb/common/schema.h"
#include "yb/master/master.pb.h"
#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_table_property.h"
#include "yb/yql/cql/ql/ptree/pt_column_definition.h"
#include "yb/yql/cql/ql/ptree/pt_type.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"
#include "yb/yql/cql/ql/ptree/pt_update.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// Constraints.

enum class PTConstraintType {
  kNone = 0,
  kPrimaryKey,
  kUnique,
  kNotNull,
};

class PTConstraint : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTConstraint> SharedPtr;
  typedef MCSharedPtr<const PTConstraint> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTConstraint(MemoryContext *memctx = nullptr, YBLocation::SharedPtr loc = nullptr)
      : TreeNode(memctx, loc) {
  }
  virtual ~PTConstraint() {
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTConstraint;
  }

  virtual PTConstraintType constraint_type() = 0;
};

class PTPrimaryKey : public PTConstraint {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTPrimaryKey> SharedPtr;
  typedef MCSharedPtr<const PTPrimaryKey> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTPrimaryKey(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               const PTListNode::SharedPtr& columns_ = nullptr);
  virtual ~PTPrimaryKey();

  virtual PTConstraintType constraint_type() override {
    return PTConstraintType::kPrimaryKey;
  }

  template<typename... TypeArgs>
  inline static PTPrimaryKey::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTPrimaryKey>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  // Predicate whether this PTPrimary node is a column constraint or a table constraint.
  // - Besides the datatype, certain constraints can also be specified when defining a column in
  //   the table. Those constraints are column constraints. The following key is column constraint.
  //     CREATE TABLE t(i int primary key, j int);
  //
  // - When creating table, besides column definitions, other elements of the table can also be
  //   specified. Those elements are table constraints. The following key is table constraint.
  //     CREATE TABLE t(i int, j int, primary key(i));
  bool is_table_element() const {
    return columns_ != nullptr;
  }

  bool is_column_element() const {
    return columns_ == nullptr;
  }

 private:
  PTListNode::SharedPtr columns_;
};

//--------------------------------------------------------------------------------------------------
// Static column qualifier.

class PTStatic : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTStatic> SharedPtr;
  typedef MCSharedPtr<const PTStatic> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTStatic(MemoryContext *memctx = nullptr, YBLocation::SharedPtr loc = nullptr)
      : TreeNode(memctx, loc) {
  }
  virtual ~PTStatic() {
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTStatic;
  }

  template<typename... TypeArgs>
  inline static PTStatic::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTStatic>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
};

//--------------------------------------------------------------------------------------------------
// CREATE TABLE statement.

class PTCreateTable : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCreateTable> SharedPtr;
  typedef MCSharedPtr<const PTCreateTable> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTCreateTable(MemoryContext *memctx,
                YBLocation::SharedPtr loc,
                const PTQualifiedName::SharedPtr& name,
                const PTListNode::SharedPtr& elements,
                bool create_if_not_exists,
                const PTTablePropertyListNode::SharedPtr& table_properties);
  virtual ~PTCreateTable();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTCreateTable;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTCreateTable::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTCreateTable>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // column lists.
  const MCList<PTColumnDefinition *>& columns() const {
    return columns_;
  }

  const MCList<PTColumnDefinition *>& primary_columns() const {
    return primary_columns_;
  }

  const MCList<PTColumnDefinition *>& hash_columns() const {
    return hash_columns_;
  }

  bool create_if_not_exists() const {
    return create_if_not_exists_;
  }

  CHECKED_STATUS AppendColumn(SemContext *sem_context,
                              PTColumnDefinition *column,
                              bool check_duplicate = false);

  CHECKED_STATUS AppendPrimaryColumn(SemContext *sem_context,
                                     PTColumnDefinition *column,
                                     bool check_duplicate = false);

  CHECKED_STATUS AppendHashColumn(SemContext *sem_context,
                                  PTColumnDefinition *column,
                                  bool check_duplicate = false);

  virtual CHECKED_STATUS CheckPrimaryType(SemContext *sem_context,
                                          const PTColumnDefinition *column) const;

  // Table name.
  const PTQualifiedName::SharedPtr& table_name() const {
    return relation_;
  }
  virtual client::YBTableName yb_table_name() const {
    return relation_->ToTableName();
  }

  PTTablePropertyListNode::SharedPtr table_properties() const {
    return table_properties_;
  }

  virtual CHECKED_STATUS ToTableProperties(TableProperties *table_properties) const;

  static bool ColumnExists(const MCList<PTColumnDefinition *>& columns,
                           const PTColumnDefinition* column);

 protected:
  PTQualifiedName::SharedPtr relation_;
  PTListNode::SharedPtr elements_;

  MCList<PTColumnDefinition *> columns_;
  MCList<PTColumnDefinition *> primary_columns_;
  MCList<PTColumnDefinition *> hash_columns_;

  bool create_if_not_exists_;
  bool contain_counters_;
  const PTTablePropertyListNode::SharedPtr table_properties_;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_PT_CREATE_TABLE_H_

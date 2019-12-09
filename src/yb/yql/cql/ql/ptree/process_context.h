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
// This generic context is used for all processes on parse tree such as parsing, semantics analysis,
// and code generation.
//
// The execution step operates on a read-only (const) parse tree and does not hold a unique_ptr to
// it. Accordingly, the execution context subclasses from ProcessContextBase which does not have
// the parse tree.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_PTREE_PROCESS_CONTEXT_H_
#define YB_YQL_CQL_QL_PTREE_PROCESS_CONTEXT_H_

#include "yb/yql/cql/ql/ptree/parse_tree.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"
#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/util/memory/mc_types.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

class ProcessContextBase {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ProcessContextBase> UniPtr;
  typedef std::unique_ptr<const ProcessContextBase> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  ProcessContextBase();
  virtual ~ProcessContextBase();

  // SQL statement being processed.
  virtual const std::string& stmt() const = 0;

  // Handling parsing warning.
  void Warn(const YBLocation& loc, const std::string& msg, ErrorCode error_code);

  // Handling parsing error.
  CHECKED_STATUS Error(const YBLocation& loc,
                       const char *msg,
                       ErrorCode error_code,
                       const char* token = nullptr);
  CHECKED_STATUS Error(const YBLocation& loc,
                       const std::string& msg,
                       ErrorCode error_code,
                       const char* token = nullptr);
  CHECKED_STATUS Error(const YBLocation& loc, const std::string& msg, const char* token = nullptr);
  CHECKED_STATUS Error(const YBLocation& loc, const char *msg, const char* token = nullptr);
  CHECKED_STATUS Error(const YBLocation& loc, ErrorCode error_code, const char* token = nullptr);

  // Variants of Error() that report location of tnode as the error location.
  CHECKED_STATUS Error(const TreeNode *tnode, ErrorCode error_code);
  CHECKED_STATUS Error(const TreeNode *tnode, const std::string& msg, ErrorCode error_code);
  CHECKED_STATUS Error(const TreeNode *tnode, const char *msg, ErrorCode error_code);
  CHECKED_STATUS Error(const TreeNode *tnode, const Status& s, ErrorCode error_code);

  CHECKED_STATUS Error(const TreeNode::SharedPtr& tnode, ErrorCode error_code);
  CHECKED_STATUS Error(const TreeNode::SharedPtr& tnode,
                       const std::string& msg,
                       ErrorCode error_code);
  CHECKED_STATUS Error(const TreeNode::SharedPtr& tnode, const char *msg, ErrorCode error_code);
  CHECKED_STATUS Error(const TreeNode::SharedPtr& tnode, const Status& s, ErrorCode error_code);

  // Memory pool for allocating and deallocating operating memory spaces during a process.
  MemoryContext *PTempMem() const {
    if (ptemp_mem_ == nullptr) {
      ptemp_mem_.reset(new Arena());
    }
    return ptemp_mem_.get();
  }

  // Access function for error_code_.
  ErrorCode error_code() const {
    return error_code_;
  }

  // Return status of a process.
  CHECKED_STATUS GetStatus();

 protected:
  MCString* error_msgs();

  // Temporary memory pool is used during a process. This pool is deleted as soon as the process is
  // completed. For performance, the temp arena and the error message that depends on it are created
  // only when needed.
  mutable std::unique_ptr<Arena> ptemp_mem_;

  // Latest error code.
  ErrorCode error_code_ = ErrorCode::SUCCESS;

  // Error messages. All reported error messages will be concatenated to the end.
  std::unique_ptr<MCString> error_msgs_;
};

//--------------------------------------------------------------------------------------------------

class ProcessContext : public ProcessContextBase {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ProcessContext> UniPtr;
  typedef std::unique_ptr<const ProcessContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  explicit ProcessContext(ParseTree::UniPtr parse_tree);
  virtual ~ProcessContext();

  // Saves the generated parse tree from the parsing process to this context.
  void SaveGeneratedParseTree(TreeNode::SharedPtr generated_parse_tree);

  // Returns the generated parse tree and release the ownership from this context.
  ParseTree::UniPtr AcquireParseTree() {
    return std::move(parse_tree_);
  }

  const std::string& stmt() const override {
    return parse_tree_->stmt();
  }

  ParseTree *parse_tree() {
    return parse_tree_.get();
  }

  // Memory pool for constructing the parse tree of a statement.
  MemoryContext *PTreeMem() const {
    return parse_tree_->PTreeMem();
  }

 protected:
  //------------------------------------------------------------------------------------------------
  // Generated parse tree (output).
  ParseTree::UniPtr parse_tree_;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_PROCESS_CONTEXT_H_

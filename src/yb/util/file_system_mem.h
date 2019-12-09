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

#ifndef YB_UTIL_FILE_SYSTEM_MEM_H
#define YB_UTIL_FILE_SYSTEM_MEM_H

#include "yb/util/file_system.h"
#include "yb/util/malloc.h"
#include "yb/util/size_literals.h"

using namespace yb::size_literals;

namespace yb {

class InMemoryFileState {
 public:
  explicit InMemoryFileState(std::string filename) : filename_(std::move(filename)), size_(0) {}

  ~InMemoryFileState() {
    for (uint8_t* block : blocks_) {
      delete[] block;
    }
  }

  InMemoryFileState(const InMemoryFileState&) = delete;
  void operator=(const InMemoryFileState&) = delete;

  uint64_t Size() const { return size_; }

  CHECKED_STATUS Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const;

  CHECKED_STATUS PreAllocate(uint64_t size);

  CHECKED_STATUS Append(const Slice& data);

  CHECKED_STATUS AppendRaw(const uint8_t *src, size_t src_len);

  const string& filename() const { return filename_; }

  size_t memory_footprint() const;

 private:
  static constexpr const size_t kBlockSize = 8_KB;

  const string filename_;

  // The following fields are not protected by any mutex. They are only mutable
  // while the file is being written, and concurrent access is not allowed
  // to writable files.
  std::vector<uint8_t*> blocks_;
  uint64_t size_;
};

class InMemorySequentialFile : public SequentialFile {
 public:
  explicit InMemorySequentialFile(std::shared_ptr<InMemoryFileState> file)
    : file_(std::move(file)), pos_(0) {}

  ~InMemorySequentialFile() {}

  CHECKED_STATUS Read(size_t n, Slice* result, uint8_t* scratch) override;

  CHECKED_STATUS Skip(uint64_t n) override;

  const string& filename() const override {
    return file_->filename();
  }

 private:
  const std::shared_ptr<InMemoryFileState> file_;
  size_t pos_;
};

class InMemoryRandomAccessFile : public RandomAccessFile {
 public:
  explicit InMemoryRandomAccessFile(std::shared_ptr<InMemoryFileState> file)
    : file_(std::move(file)) {}

  ~InMemoryRandomAccessFile() {}

  virtual Status Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const override {
    return file_->Read(offset, n, result, scratch);
  }

  Result<uint64_t> Size() const override {
    return file_->Size();
  }

  Result<uint64_t> INode() const override {
    return 0;
  }

  const string& filename() const override {
    return file_->filename();
  }

  size_t memory_footprint() const override {
    // The FileState is actually shared between multiple files, but the double
    // counting doesn't matter much since MemEnv is only used in tests.
    return malloc_usable_size(this) + file_->memory_footprint();
  }

 private:
  const std::shared_ptr<InMemoryFileState> file_;
};

} // namespace yb

#endif  // YB_UTIL_FILE_SYSTEM_MEM_H

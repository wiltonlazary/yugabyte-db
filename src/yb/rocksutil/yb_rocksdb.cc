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

#include <string>

#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/bytes_formatter.h"

using std::shared_ptr;
using std::string;
using strings::Substitute;

using yb::FormatBytesAsStr;
using yb::QuotesType;

namespace yb {

void InitRocksDBWriteOptions(rocksdb::WriteOptions* write_options) {
  // We disable the WAL in RocksDB because we already have the Raft log and we should
  // replay it during recovery.
  write_options->disableWAL = true;
  write_options->sync = false;
}

std::string FormatRocksDBSliceAsStr(const rocksdb::Slice& rocksdb_slice,
                                    const size_t max_length) {
  return FormatBytesAsStr(rocksdb_slice.cdata(),
                          rocksdb_slice.size(),
                          QuotesType::kDoubleQuotes,
                          max_length);
}

}  // namespace yb

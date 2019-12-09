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

#ifndef YB_UTIL_HEADER_MANAGER_H
#define YB_UTIL_HEADER_MANAGER_H

#include "yb/util/status.h"
#include "yb/util/result.h"

namespace yb {
namespace enterprise {

struct EncryptionParams;
typedef std::unique_ptr<EncryptionParams> EncryptionParamsPtr;

struct FileEncryptionStatus;

// Class for managing encryption headers of files.
class HeaderManager {
 public:
  virtual ~HeaderManager() {}

  // Slice starts from GetEncryptionMetadataStartIndex() and has length header_size from calling
  // GetFileEncryptionStatusFromPrefix. Generate encryption params for the given file, used when
  // creating a readable file.
  virtual Result<EncryptionParamsPtr> DecodeEncryptionParamsFromEncryptionMetadata(
      const Slice& s) = 0;
  // Given encryption params, create a file header. Used when creating a writable file.
  virtual Result<std::string> SerializeEncryptionParams(
      const EncryptionParams& encryption_info) = 0;
  // Start index of the encryption file metadata for the given file.
  virtual uint32_t GetEncryptionMetadataStartIndex() = 0;
  // Returns whether the file is encrypted and if so, what is the size of the header. Slice starts
  // from 0 and has length GetEncryptionMetadataStartIndex().
  virtual Result<FileEncryptionStatus> GetFileEncryptionStatusFromPrefix(const Slice& s) = 0;
  // Is encryption enabled for new files.
  virtual bool IsEncryptionEnabled() = 0;
};

} // namespace enterprise
} // namespace yb

#endif // YB_UTIL_HEADER_MANAGER_H

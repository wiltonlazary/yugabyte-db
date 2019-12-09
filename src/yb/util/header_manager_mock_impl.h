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
#ifndef YB_UTIL_HEADER_MANAGER_MOCK_IMPL_H
#define YB_UTIL_HEADER_MANAGER_MOCK_IMPL_H

#include <memory>

#include "yb/util/header_manager.h"

namespace yb {
namespace enterprise {

class HeaderManagerMockImpl : public yb::enterprise::HeaderManager {
 public:
  HeaderManagerMockImpl();
  void SetFileEncryption(bool file_encrypted);

  Result<string> SerializeEncryptionParams(
      const EncryptionParams& encryption_info) override;

  Result<EncryptionParamsPtr> DecodeEncryptionParamsFromEncryptionMetadata(
      const Slice& s) override;

  uint32_t GetEncryptionMetadataStartIndex() override;
  Result<FileEncryptionStatus> GetFileEncryptionStatusFromPrefix(const Slice& s) override;
  bool IsEncryptionEnabled() override;

 private:
  EncryptionParamsPtr encryption_params_;
  bool file_encrypted_ = false;
};

std::unique_ptr<HeaderManager> GetMockHeaderManager();

} // namespace enterprise
} // namespace yb


#endif // YB_UTIL_HEADER_MANAGER_MOCK_IMPL_H

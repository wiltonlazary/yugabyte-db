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

#include <openssl/rand.h>

#include <boost/pointer_cast.hpp>

#include "yb/util/header_manager_mock_impl.h"
#include "yb/util/encryption_util.h"

namespace yb {
namespace enterprise {

constexpr uint32_t kDefaultHeaderSize = 32;
constexpr uint32_t kEncryptionMetaStart = 16;

HeaderManagerMockImpl::HeaderManagerMockImpl() {}

void HeaderManagerMockImpl::SetFileEncryption(bool file_encrypted) {
  file_encrypted_ = file_encrypted;
}

Result<string> HeaderManagerMockImpl::SerializeEncryptionParams(
    const EncryptionParams& encryption_info) {
  string header;
  header.resize(kDefaultHeaderSize, 0);
  encryption_params_.reset(new EncryptionParams(encryption_info));
  return header;
}

Result<EncryptionParamsPtr>
HeaderManagerMockImpl::DecodeEncryptionParamsFromEncryptionMetadata(const yb::Slice& s) {
  auto encryption_params = std::make_unique<EncryptionParams>();
  memcpy(encryption_params.get(), encryption_params_.get(), sizeof(EncryptionParams));
  return encryption_params;
}

uint32_t HeaderManagerMockImpl::GetEncryptionMetadataStartIndex() {
  return kEncryptionMetaStart;
}

Result<FileEncryptionStatus> HeaderManagerMockImpl::GetFileEncryptionStatusFromPrefix(
    const Slice& s) {
  FileEncryptionStatus status;
  status.is_encrypted = file_encrypted_;
  status.header_size = kDefaultHeaderSize - GetEncryptionMetadataStartIndex();
  return status;
}

std::unique_ptr<HeaderManager> GetMockHeaderManager() {
  return std::make_unique<HeaderManagerMockImpl>();
}

bool HeaderManagerMockImpl::IsEncryptionEnabled() {
  return file_encrypted_;
}

} // namespace enterprise
} // namespace yb

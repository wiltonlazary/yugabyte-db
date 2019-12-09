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

#ifndef YB_UTIL_UNIVERSE_KEY_MANAGER_H
#define YB_UTIL_UNIVERSE_KEY_MANAGER_H

#include "yb/util/encryption.pb.h"
#include "yb/util/encryption_util.h"
#include "yb/util/result.h"

namespace yb {
namespace enterprise {

// Class is responsible for saving the universe key registry from master on heartbeat for use
// in creating new files and reading exising files.
class UniverseKeyManager {
 public:
  void SetUniverseKeyRegistry(const yb::UniverseKeyRegistryPB& universe_key_registry);
  // From an existing version id, generate encryption params. Used when creating readable files.
  Result<EncryptionParamsPtr> GetUniverseParamsWithVersion(
      const UniverseKeyId& version_id);
  // Get the latest universe key in the registry. Used when creating writable files.
  Result<UniverseKeyParams> GetLatestUniverseParams();
  bool IsEncryptionEnabled();

  // Returns once the master has heartbeated with its registry. Blocks calls to
  // GetUniverseParamsWithVersion() and GetLatestUniverseParams()
  MUST_USE_RESULT std::unique_lock<std::mutex> EnsureRegistryReceived();

 private:
  // Registry from master.
  yb::UniverseKeyRegistryPB universe_key_registry_;

  mutable std::mutex mutex_;
  std::condition_variable cond_;

  // Set to true once the registry has been received from master.
  bool received_registry_ = false;
};

} // namespace enterprise
} // namespace yb

#endif // YB_UTIL_UNIVERSE_KEY_MANAGER_H

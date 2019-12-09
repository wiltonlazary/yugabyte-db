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

#ifndef YB_MASTER_MASTER_FWD_H
#define YB_MASTER_MASTER_FWD_H

#include <memory>
#include <vector>

namespace yb {
namespace master {

class TSDescriptor;
typedef std::shared_ptr<TSDescriptor> TSDescriptorPtr;
typedef std::vector<TSDescriptorPtr> TSDescriptorVector;

class TSRegistrationPB;
class EncryptionManager;

class AddUniverseKeysRequestPB;
class AddUniverseKeysResponsePB;
class GetUniverseKeyRegistryRequestPB;
class GetUniverseKeyRegistryResponsePB;
class HasUniverseKeyInMemoryRequestPB;
class HasUniverseKeyInMemoryResponsePB;
class EncryptionInfoPB;
class ChangeEncryptionInfoRequestPB;
class ChangeEncryptionInfoResponsePB;
class IsEncryptionEnabledRequestPB;
class IsEncryptionEnabledResponsePB;
class TSHeartbeatResponsePB;

} // namespace master
} // namespace yb

#endif // YB_MASTER_MASTER_FWD_H

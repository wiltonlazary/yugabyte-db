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

#ifndef YB_MASTER_MASTER_SERVICE_BASE_H
#define YB_MASTER_MASTER_SERVICE_BASE_H

#include "yb/gutil/macros.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
class Status;

namespace rpc {
class RpcContext;
} // namespace rpc

namespace master {

class Master;
class CatalogManager;
class FlushManager;
class PermissionsManager;
class EncryptionManager;

// Tells HandleIn/HandleOnLeader to either acquire the lock briefly to check leadership (kFalse)
// or to hold it throughout the handler invocation (kTrue).
YB_STRONGLY_TYPED_BOOL(HoldCatalogLock);

// Base class for any master service with a few helpers.
class MasterServiceBase {
 public:
  explicit MasterServiceBase(Master* server) : server_(server) {}

 protected:
  template <class ReqType, class RespType, class FnType>
  void HandleOnLeader(const ReqType* req, RespType* resp, rpc::RpcContext* rpc, FnType f,
                      HoldCatalogLock hold_catalog_lock = HoldCatalogLock::kTrue);

  template <class HandlerType, class ReqType, class RespType>
  void HandleOnAllMasters(const ReqType* req, RespType* resp, rpc::RpcContext* rpc,
                          Status (HandlerType::*f)(const ReqType* req, RespType*));

  template <class HandlerType, class ReqType, class RespType>
  void HandleIn(const ReqType* req, RespType* resp, rpc::RpcContext* rpc,
      Status (HandlerType::*f)(RespType*),
      HoldCatalogLock hold_catalog_lock = HoldCatalogLock::kTrue);

  template <class HandlerType, class ReqType, class RespType>
  void HandleIn(const ReqType* req, RespType* resp, rpc::RpcContext* rpc,
      Status (HandlerType::*f)(const ReqType*, RespType*),
      HoldCatalogLock hold_catalog_lock = HoldCatalogLock::kTrue);

  template <class HandlerType, class ReqType, class RespType>
  void HandleIn(const ReqType* req, RespType* resp, rpc::RpcContext* rpc,
      Status (HandlerType::*f)(const ReqType*, RespType*, rpc::RpcContext*),
      HoldCatalogLock hold_catalog_lock = HoldCatalogLock::kTrue);

  enterprise::CatalogManager* handler(CatalogManager*);
  FlushManager* handler(FlushManager*);
  PermissionsManager* handler(PermissionsManager*);
  EncryptionManager* handler(EncryptionManager*);

  Master* server_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MasterServiceBase);
};

} // namespace master
} // namespace yb

#endif // YB_MASTER_MASTER_SERVICE_BASE_H

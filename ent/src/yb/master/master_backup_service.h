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

#ifndef ENT_SRC_YB_MASTER_MASTER_BACKUP_SERVICE_H
#define ENT_SRC_YB_MASTER_MASTER_BACKUP_SERVICE_H

#include "yb/master/master_backup.service.h"
#include "yb/master/master_service_base.h"

namespace yb {
namespace master {

// Implementation of the master backup service. See master_backup.proto.
class MasterBackupServiceImpl : public MasterBackupServiceIf,
                                public MasterServiceBase {
 public:
  explicit MasterBackupServiceImpl(Master* server);

  void CreateSnapshot(
      const CreateSnapshotRequestPB* req, CreateSnapshotResponsePB* resp,
      rpc::RpcContext rpc) override;

  void ListSnapshots(
      const ListSnapshotsRequestPB* req, ListSnapshotsResponsePB* resp,
      rpc::RpcContext rpc) override;

  void ListSnapshotRestorations(
      const ListSnapshotRestorationsRequestPB* req, ListSnapshotRestorationsResponsePB* resp,
      rpc::RpcContext rpc) override;

  void RestoreSnapshot(
      const RestoreSnapshotRequestPB* req, RestoreSnapshotResponsePB* resp,
      rpc::RpcContext rpc) override;

  void DeleteSnapshot(
      const DeleteSnapshotRequestPB* req, DeleteSnapshotResponsePB* resp,
      rpc::RpcContext rpc) override;

  void ImportSnapshotMeta(
      const ImportSnapshotMetaRequestPB* req, ImportSnapshotMetaResponsePB* resp,
      rpc::RpcContext rpc) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(MasterBackupServiceImpl);
};

} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_MASTER_BACKUP_SERVICE_H

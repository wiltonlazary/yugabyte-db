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

#include "yb/client/table.h"

#include "yb/client/client.h"
#include "yb/client/client-internal.h"
#include "yb/client/yb_op.h"

#include "yb/master/master.pb.h"
#include "yb/master/master.proxy.h"

#include "yb/util/backoff_waiter.h"

DEFINE_int32(
    max_num_tablets_for_table, 50,
    "Max number of tablets that can be specified in a CREATE TABLE statement");

namespace yb {
namespace client {

Status YBTable::PBToClientTableType(
    TableType table_type_from_pb,
    YBTableType* client_table_type) {
  switch (table_type_from_pb) {
    case TableType::YQL_TABLE_TYPE:
      *client_table_type = YBTableType::YQL_TABLE_TYPE;
      return Status::OK();
    case TableType::REDIS_TABLE_TYPE:
      *client_table_type = YBTableType::REDIS_TABLE_TYPE;
      return Status::OK();
    case TableType::PGSQL_TABLE_TYPE:
      *client_table_type = YBTableType::PGSQL_TABLE_TYPE;
      return Status::OK();
    case TableType::TRANSACTION_STATUS_TABLE_TYPE:
      *client_table_type = YBTableType::TRANSACTION_STATUS_TABLE_TYPE;
      return Status::OK();
  }

  *client_table_type = YBTableType::UNKNOWN_TABLE_TYPE;
  return STATUS(InvalidArgument, strings::Substitute(
    "Invalid table type from master response: $0", table_type_from_pb));
}

TableType YBTable::ClientToPBTableType(YBTableType table_type) {
  switch (table_type) {
    case YBTableType::YQL_TABLE_TYPE:
      return TableType::YQL_TABLE_TYPE;
    case YBTableType::REDIS_TABLE_TYPE:
      return TableType::REDIS_TABLE_TYPE;
    case YBTableType::PGSQL_TABLE_TYPE:
      return TableType::PGSQL_TABLE_TYPE;
    case YBTableType::TRANSACTION_STATUS_TABLE_TYPE:
      return TableType::TRANSACTION_STATUS_TABLE_TYPE;
    case YBTableType::UNKNOWN_TABLE_TYPE:
      break;
  }
  FATAL_INVALID_ENUM_VALUE(YBTableType, table_type);
  // Returns a dummy value to avoid compilation warning.
  return TableType::DEFAULT_TABLE_TYPE;
}

YBTable::YBTable(client::YBClient* client, const YBTableInfo& info)
    : client_(client),
      // The table type is set after the table is opened.
      table_type_(YBTableType::UNKNOWN_TABLE_TYPE),
      info_(info) {
}

YBTable::~YBTable() {
}

//--------------------------------------------------------------------------------------------------

const YBTableName& YBTable::name() const {
  return info_.table_name;
}

YBTableType YBTable::table_type() const {
  return table_type_;
}

const string& YBTable::id() const {
  return info_.table_id;
}

YBClient* YBTable::client() const {
  return client_;
}

const YBSchema& YBTable::schema() const {
  return info_.schema;
}

const Schema& YBTable::InternalSchema() const {
  return internal::GetSchema(info_.schema);
}

const IndexMap& YBTable::index_map() const {
  return info_.index_map;
}

bool YBTable::IsIndex() const {
  return info_.index_info != boost::none;
}

const IndexInfo& YBTable::index_info() const {
  CHECK(info_.index_info);
  return *info_.index_info;
}

const PartitionSchema& YBTable::partition_schema() const {
  return info_.partition_schema;
}

const std::vector<std::string>& YBTable::GetPartitions() const {
  return partitions_;
}

//--------------------------------------------------------------------------------------------------

YBqlWriteOp* YBTable::NewQLWrite() {
  return new YBqlWriteOp(shared_from_this());
}

YBqlWriteOp* YBTable::NewQLInsert() {
  return YBqlWriteOp::NewInsert(shared_from_this());
}

YBqlWriteOp* YBTable::NewQLUpdate() {
  return YBqlWriteOp::NewUpdate(shared_from_this());
}

YBqlWriteOp* YBTable::NewQLDelete() {
  return YBqlWriteOp::NewDelete(shared_from_this());
}

YBqlReadOp* YBTable::NewQLSelect() {
  return YBqlReadOp::NewSelect(shared_from_this());
}

YBqlReadOp* YBTable::NewQLRead() {
  return new YBqlReadOp(shared_from_this());
}

const std::string& YBTable::FindPartitionStart(
    const std::string& partition_key, size_t group_by) const {
  auto it = std::lower_bound(partitions_.begin(), partitions_.end(), partition_key);
  if (it == partitions_.end() || *it > partition_key) {
    DCHECK(it != partitions_.begin());
    --it;
  }
  if (group_by <= 1) {
    return *it;
  }
  size_t idx = (it - partitions_.begin()) / group_by * group_by;
  return partitions_[idx];
}

Status YBTable::Open() {
  // TODO: fetch the schema from the master here once catalog is available.
  master::GetTableLocationsRequestPB req;
  req.set_max_returned_locations(std::numeric_limits<int32_t>::max());
  master::GetTableLocationsResponsePB resp;

  auto deadline = CoarseMonoClock::Now() + client_->default_admin_operation_timeout();

  req.mutable_table()->set_table_id(info_.table_id);
  req.set_require_tablets_running(true);
  Status s;

  CoarseBackoffWaiter waiter(deadline, std::chrono::seconds(1) /* max_wait */);
  // TODO: replace this with Async RPC-retrier based RPC in the next revision,
  // adding exponential backoff and allowing this to be used safely in a
  // a reactor thread.
  while (true) {
    rpc::RpcController rpc;

    // Have we already exceeded our deadline?
    auto now = CoarseMonoClock::Now();

    // See YBClient::Data::SyncLeaderMasterRpc().
    auto rpc_deadline = now + client_->default_rpc_timeout();
    rpc.set_deadline(std::min(rpc_deadline, deadline));

    s = client_->data_->master_proxy()->GetTableLocations(req, &resp, &rpc);
    if (!s.ok()) {
      // Various conditions cause us to look for the leader master again.
      // It's ok if that eventually fails; we'll retry over and over until
      // the deadline is reached.

      if (s.IsNetworkError()) {
        LOG(WARNING) << "Network error talking to the leader master ("
                     << client_->data_->leader_master_hostport().ToString() << "): "
                     << s.ToString();
        if (client_->IsMultiMaster()) {
          LOG(INFO) << "Determining the leader master again and retrying.";
          WARN_NOT_OK(client_->data_->SetMasterServerProxy(client_, deadline),
                      "Failed to determine new Master");
          continue;
        }
      }

      if (s.IsTimedOut() && CoarseMonoClock::Now() < deadline) {
        // If the RPC timed out and the operation deadline expired, we'll loop
        // again and time out for good above.
        LOG(WARNING) << "Timed out talking to the leader master ("
                     << client_->data_->leader_master_hostport().ToString() << "): "
                     << s.ToString();
        if (client_->IsMultiMaster()) {
          LOG(INFO) << "Determining the leader master again and retrying.";
          WARN_NOT_OK(client_->data_->SetMasterServerProxy(client_, deadline),
                      "Failed to determine new Master");
          continue;
        }
      }
    }
    if (s.ok() && resp.has_error()) {
      if (resp.error().code() == master::MasterErrorPB::NOT_THE_LEADER ||
          resp.error().code() == master::MasterErrorPB::CATALOG_MANAGER_NOT_INITIALIZED) {
        LOG(WARNING) << "Master " << client_->data_->leader_master_hostport().ToString()
                     << " is no longer the leader master.";
        if (client_->IsMultiMaster()) {
          LOG(INFO) << "Determining the leader master again and retrying.";
          WARN_NOT_OK(client_->data_->SetMasterServerProxy(client_, deadline),
                      "Failed to determine new Master");
          continue;
        }
      }
      if (s.ok()) {
        s = StatusFromPB(resp.error().status());
      }
    }
    if (!s.ok()) {
      YB_LOG_EVERY_N_SECS(WARNING, 10) << "Error getting table locations: " << s << ", retrying.";
    } else if (resp.tablet_locations_size() > 0) {
      DCHECK(partitions_.empty());
      partitions_.clear();
      partitions_.reserve(resp.tablet_locations().size());
      for (const auto& tablet_location : resp.tablet_locations()) {
        partitions_.push_back(tablet_location.partition().partition_key_start());
      }
      std::sort(partitions_.begin(), partitions_.end());
      break;
    }

    if (!waiter.Wait()) {
      const char* msg = "OpenTable timed out";
      LOG(ERROR) << msg;
      return STATUS(TimedOut, msg);
    }
  }


  RETURN_NOT_OK_PREPEND(PBToClientTableType(resp.table_type(), &table_type_),
    strings::Substitute("Invalid table type for table '$0'", info_.table_name.ToString()));

  VLOG(1) << "Open Table " << info_.table_name.ToString() << ", found "
          << resp.tablet_locations_size() << " tablets";
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

YBPgsqlWriteOp* YBTable::NewPgsqlWrite() {
  return new YBPgsqlWriteOp(shared_from_this());
}

YBPgsqlWriteOp* YBTable::NewPgsqlInsert() {
  return YBPgsqlWriteOp::NewInsert(shared_from_this());
}

YBPgsqlWriteOp* YBTable::NewPgsqlUpdate() {
  return YBPgsqlWriteOp::NewUpdate(shared_from_this());
}

YBPgsqlWriteOp* YBTable::NewPgsqlDelete() {
  return YBPgsqlWriteOp::NewDelete(shared_from_this());
}

YBPgsqlReadOp* YBTable::NewPgsqlSelect() {
  return YBPgsqlReadOp::NewSelect(shared_from_this());
}

YBPgsqlReadOp* YBTable::NewPgsqlRead() {
  return new YBPgsqlReadOp(shared_from_this());
}

} // namespace client
} // namespace yb

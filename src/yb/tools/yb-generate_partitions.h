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

#ifndef YB_TOOLS_YB_GENERATE_PARTITIONS_H
#define YB_TOOLS_YB_GENERATE_PARTITIONS_H

#include "yb/client/client.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/partition.h"
#include "yb/common/schema.h"
#include "yb/master/master.pb.h"
#include "yb/tools/bulk_load_utils.h"

namespace yb {
namespace tools {

typedef std::map<std::string, master::TabletLocationsPB> TabletMap;


// YBPartitionGenerator is a useful utility to look up the appropriate tablet id for a given row
// in a table. Given a line in a csv file, it is able to compute the appropriate partitions and
// give us the appropriate tablet id.
class YBPartitionGenerator {
 public:
  explicit YBPartitionGenerator(const client::YBTableName& table_name,
                                const vector<std::string>& master_addresses);
  CHECKED_STATUS Init();
  // Retrieves the partition_key and tablet_id for a given row, which is a string of comma
  // separated values. The format of the comma separated values should be similar to the Schema
  // object where we first have the hash keys, then the range keys and finally the regular
  // columns of the table.
  CHECKED_STATUS LookupTabletId(const std::string &row,
                                std::string *tablet_id,
                                std::string* partition_key);
  CHECKED_STATUS LookupTabletId(const std::string &row,
                                const std::set<int>& skipped_cols,
                                std::string *tablet_id,
                                std::string* partition_key);
  CHECKED_STATUS LookupTabletIdWithTokenizer(const CsvTokenizer& tokenizer,
                                             const std::set<int>& skipped_cols,
                                             std::string *tablet_id,
                                             std::string* partition_key);

 private:
  CHECKED_STATUS BuildTabletMap(
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB> &tablets);

  TabletMap tablet_map_;
  client::YBTableName table_name_;
  std::vector<std::string> master_addresses_;
  std::unique_ptr<client::YBClient> client_;
  std::shared_ptr<client::YBTable> table_;
};

} // namespace tools
} // namespace yb
#endif // YB_TOOLS_YB_GENERATE_PARTITIONS_H

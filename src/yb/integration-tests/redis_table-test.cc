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

#include <chrono>
#include <thread>

#include "yb/integration-tests/redis_table_test_base.h"

#include <glog/logging.h>

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/integration-tests/yb_table_test_base.h"
#include "yb/yql/redis/redisserver/redis_parser.h"

using std::string;
using std::vector;
using std::unique_ptr;

namespace yb {
namespace integration_tests {

class RedisTableTest : public RedisTableTestBase {
};

using client::YBRedisWriteOp;
using client::YBColumnSchema;
using client::YBTableCreator;
using client::YBSchemaBuilder;
using client::YBColumnSchema;
using client::YBTableType;
using client::YBSession;

using integration_tests::YBTableTestBase;

TEST_F(RedisTableTest, SimpleRedisSetTest) {
  ASSERT_NO_FATALS(RedisSimpleSetCommands());
}

TEST_F(RedisTableTest, SimpleRedisGetTest) {
  ASSERT_NO_FATALS(RedisSimpleSetCommands());
  ASSERT_NO_FATALS(RedisSimpleGetCommands());
}

TEST_F(RedisTableTest, RedisTtlTest) {
  ASSERT_NO_FATALS(RedisTtlSetCommands());
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));
  ASSERT_NO_FATALS(RedisTtlGetCommands());
}

TEST_F(RedisTableTest, RedisOverWriteTest) {
  // What happens when values are deleted, overwritten, rewritten with a timestamp, etc.
  // All writes operate in the default upsert mode
  // TODO: to be implemented, planning to do this on followup diff, because I want to also implement
  // delete command.
}

}  // namespace integration_tests
}  // namespace yb

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/rpc/reactor.h"

#include <thread>

#include "yb/rpc/rpc-test-base.h"
#include "yb/util/countdown_latch.h"

using std::shared_ptr;
using namespace std::literals;
using namespace std::placeholders;

namespace yb {
namespace rpc {

MessengerOptions MakeMessengerOptions() {
  auto result = kDefaultClientMessengerOptions;
  result.n_reactors = 4;
  return result;
}

class ReactorTest : public RpcTestBase {
 public:
  ReactorTest()
    : messenger_(CreateMessenger("my_messenger", MakeMessengerOptions()).release()),
      latch_(1) {
  }

  void ScheduledTask(const Status& status, const Status& expected_status) {
    ASSERT_EQ(expected_status.CodeAsString(), status.CodeAsString());
    latch_.CountDown();
  }

  void ScheduledTaskCheckThread(const Status& status, const Thread* thread) {
    ASSERT_OK(status);
    ASSERT_EQ(thread, Thread::current_thread());
    latch_.CountDown();
  }

  void ScheduledTaskScheduleAgain(const Status& status) {
    auto task_id = messenger_->ScheduleOnReactor(
        std::bind(&ReactorTest::ScheduledTaskCheckThread, this, _1, Thread::current_thread()),
        0s, SOURCE_LOCATION(), nullptr /* messenger */);
    ASSERT_EQ(task_id, 0);
    latch_.CountDown();
  }

 protected:
  AutoShutdownMessengerHolder messenger_;
  CountDownLatch latch_;
};

TEST_F(ReactorTest, TestFunctionIsCalled) {
  auto task_id = messenger_->ScheduleOnReactor(
      std::bind(&ReactorTest::ScheduledTask, this, _1, Status::OK()), 0s,
      SOURCE_LOCATION(), nullptr /* messenger */);
  ASSERT_EQ(task_id, 0);
  latch_.Wait();
}

TEST_F(ReactorTest, TestFunctionIsCalledAtTheRightTime) {
  MonoTime before = MonoTime::Now();
  auto task_id = messenger_->ScheduleOnReactor(
      std::bind(&ReactorTest::ScheduledTask, this, _1, Status::OK()),
      100ms, SOURCE_LOCATION(), nullptr /* messenger */);
  ASSERT_EQ(task_id, 0);
  latch_.Wait();
  MonoTime after = MonoTime::Now();
  MonoDelta delta = after.GetDeltaSince(before);
  CHECK_GE(delta.ToMilliseconds(), 100);
}

TEST_F(ReactorTest, TestFunctionIsCalledIfReactorShutdown) {
  auto task_id = messenger_->ScheduleOnReactor(
      std::bind(&ReactorTest::ScheduledTask, this, _1, STATUS(Aborted, "doesn't matter")),
      60s, SOURCE_LOCATION(), nullptr /* messenger */);
  ASSERT_EQ(task_id, 0);
  messenger_->Shutdown();
  latch_.Wait();
}

TEST_F(ReactorTest, TestReschedulesOnSameReactorThread) {
  // Our scheduled task will schedule yet another task.
  latch_.Reset(2);

  auto task_id = messenger_->ScheduleOnReactor(
      std::bind(&ReactorTest::ScheduledTaskScheduleAgain, this, _1), 0s,
      SOURCE_LOCATION(), nullptr /* messenger */);
  ASSERT_EQ(task_id, 0);
  latch_.Wait();
  latch_.Wait();
}

} // namespace rpc
} // namespace yb

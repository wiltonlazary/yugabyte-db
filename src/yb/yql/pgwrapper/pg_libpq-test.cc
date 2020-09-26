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

#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

#include "yb/client/client_fwd.h"
#include "yb/common/common.pb.h"
#include "yb/common/pgsql_error.h"
#include "yb/master/catalog_manager.h"

using namespace std::literals;

DECLARE_int64(external_mini_cluster_max_log_bytes);

METRIC_DECLARE_entity(tablet);
METRIC_DECLARE_counter(transaction_not_found);

METRIC_DECLARE_entity(server);
METRIC_DECLARE_counter(rpc_inbound_calls_created);

namespace yb {
namespace pgwrapper {

class PgLibPqTest : public LibPqTestBase {
 protected:
  void TestMultiBankAccount(IsolationLevel isolation);

  void DoIncrement(int key, int num_increments, IsolationLevel isolation);

  void TestParallelCounter(IsolationLevel isolation);

  void TestConcurrentCounter(IsolationLevel isolation);

  void TestOnConflict(bool kill_master, const MonoDelta& duration);

  void TestCacheRefreshRetry(const bool is_retry_disabled);
};

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(Simple)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (1, 'hello')"));

  auto res = ASSERT_RESULT(conn.Fetch("SELECT * FROM t"));

  {
    auto lines = PQntuples(res.get());
    ASSERT_EQ(1, lines);

    auto columns = PQnfields(res.get());
    ASSERT_EQ(2, columns);

    auto key = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
    ASSERT_EQ(key, 1);
    auto value = ASSERT_RESULT(GetString(res.get(), 0, 1));
    ASSERT_EQ(value, "hello");
  }
}

// Test that repeats example from this article:
// https://blogs.msdn.microsoft.com/craigfr/2007/05/16/serializable-vs-snapshot-isolation-level/
//
// Multiple rows with values 0 and 1 are stored in table.
// Two concurrent transaction fetches all rows from table and does the following.
// First transaction changes value of all rows with value 0 to 1.
// Second transaction changes value of all rows with value 1 to 0.
// As outcome we should have rows with the same value.
//
// The described prodecure is repeated multiple times to increase probability of catching bug,
// w/o running test multiple times.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(SerializableColoring)) {
  static const std::string kTryAgain = "Try again.";
  constexpr auto kKeys = RegularBuildVsSanitizers(10, 20);
  constexpr auto kColors = 2;
  constexpr auto kIterations = 20;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, color INT)"));

  auto iterations_left = kIterations;

  for (int iteration = 0; iterations_left > 0; ++iteration) {
    auto iteration_title = Format("Iteration: $0", iteration);
    SCOPED_TRACE(iteration_title);
    LOG(INFO) << iteration_title;

    auto status = conn.Execute("DELETE FROM t");
    if (!status.ok()) {
      ASSERT_STR_CONTAINS(status.ToString(), kTryAgain);
      continue;
    }
    for (int k = 0; k != kKeys; ++k) {
      int32_t color = RandomUniformInt(0, kColors - 1);
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (key, color) VALUES ($0, $1)", k, color));
    }

    std::atomic<int> complete{ 0 };
    std::vector<std::thread> threads;
    for (int i = 0; i != kColors; ++i) {
      int32_t color = i;
      threads.emplace_back([this, color, kKeys, &complete] {
        auto conn = ASSERT_RESULT(Connect());

        ASSERT_OK(conn.Execute("BEGIN"));
        ASSERT_OK(conn.Execute("SET TRANSACTION ISOLATION LEVEL SERIALIZABLE"));

        auto res = conn.Fetch("SELECT * FROM t");
        if (!res.ok()) {
          auto msg = res.status().message().ToBuffer();
          ASSERT_STR_CONTAINS(res.status().ToString(), kTryAgain);
          return;
        }
        auto columns = PQnfields(res->get());
        ASSERT_EQ(2, columns);

        auto lines = PQntuples(res->get());
        ASSERT_EQ(kKeys, lines);
        for (int i = 0; i != lines; ++i) {
          if (ASSERT_RESULT(GetInt32(res->get(), i, 1)) == color) {
            continue;
          }

          auto key = ASSERT_RESULT(GetInt32(res->get(), i, 0));
          auto status = conn.ExecuteFormat("UPDATE t SET color = $1 WHERE key = $0", key, color);
          if (!status.ok()) {
            auto msg = status.message().ToBuffer();
            // Missing metadata means that transaction was aborted and cleaned.
            ASSERT_TRUE(msg.find("Try again.") != std::string::npos ||
                        msg.find("Missing metadata") != std::string::npos) << status;
            break;
          }
        }

        auto status = conn.Execute("COMMIT");
        if (!status.ok()) {
          auto msg = status.message().ToBuffer();
          ASSERT_TRUE(msg.find("Operation expired") != std::string::npos) << status;
          return;
        }

        ++complete;
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }

    if (complete == 0) {
      continue;
    }

    auto res = ASSERT_RESULT(conn.Fetch("SELECT * FROM t"));
    auto columns = PQnfields(res.get());
    ASSERT_EQ(2, columns);

    auto lines = PQntuples(res.get());
    ASSERT_EQ(kKeys, lines);

    std::vector<int32_t> zeroes, ones;
    for (int i = 0; i != lines; ++i) {
      auto key = ASSERT_RESULT(GetInt32(res.get(), i, 0));
      auto current = ASSERT_RESULT(GetInt32(res.get(), i, 1));
      if (current == 0) {
        zeroes.push_back(key);
      } else {
        ones.push_back(key);
      }
    }

    std::sort(ones.begin(), ones.end());
    std::sort(zeroes.begin(), zeroes.end());

    LOG(INFO) << "Zeroes: " << yb::ToString(zeroes) << ", ones: " << yb::ToString(ones);
    ASSERT_TRUE(zeroes.empty() || ones.empty());

    --iterations_left;
  }
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(SerializableReadWriteConflict)) {
  const auto kKeys = RegularBuildVsSanitizers(20, 5);
  const auto kNumTries = RegularBuildVsSanitizers(4, 1);
  auto tries = 1;
  for (; tries <= kNumTries; ++tries) {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("DROP TABLE IF EXISTS t"));
    ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));

    size_t reads_won = 0, writes_won = 0;
    for (int i = 0; i != kKeys; ++i) {
      auto read_conn = ASSERT_RESULT(Connect());
      ASSERT_OK(read_conn.Execute("BEGIN ISOLATION LEVEL SERIALIZABLE"));
      auto res = read_conn.FetchFormat("SELECT * FROM t WHERE key = $0", i);
      auto read_status = ResultToStatus(res);

      auto write_conn = ASSERT_RESULT(Connect());
      ASSERT_OK(write_conn.Execute("BEGIN ISOLATION LEVEL SERIALIZABLE"));
      auto write_status = write_conn.ExecuteFormat("INSERT INTO t (key) VALUES ($0)", i);

      std::thread read_commit_thread([&read_conn, &read_status] {
        if (read_status.ok()) {
          read_status = read_conn.Execute("COMMIT");
        }
      });

      std::thread write_commit_thread([&write_conn, &write_status] {
        if (write_status.ok()) {
          write_status = write_conn.Execute("COMMIT");
        }
      });

      read_commit_thread.join();
      write_commit_thread.join();

      LOG(INFO) << "Read: " << read_status << ", write: " << write_status;

      if (!read_status.ok()) {
        ASSERT_OK(write_status);
        ++writes_won;
      } else {
        ASSERT_NOK(write_status);
        ++reads_won;
      }
    }

    LOG(INFO) << "Reads won: " << reads_won << ", writes won: " << writes_won
              << " (" << tries << "/" << kNumTries << ")";
    // always pass for TSAN, we're just looking for memory issues
    if (RegularBuildVsSanitizers(false, true)) {
      break;
    }
    // break (succeed) if we hit 25% on our "coin toss" transaction conflict above
    if (reads_won >= kKeys / 4 && writes_won >= kKeys / 4) {
      break;
    }
    // otherwise, retry and see if this is consistent behavior
  }
  ASSERT_LE(tries, kNumTries);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(ReadRestart)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));

  std::atomic<bool> stop(false);
  std::atomic<int> last_written(0);

  std::thread write_thread([this, &stop, &last_written] {
    auto write_conn = ASSERT_RESULT(Connect());
    int write_key = 1;
    while (!stop.load(std::memory_order_acquire)) {
      SCOPED_TRACE(Format("Writing: $0", write_key));

      ASSERT_OK(write_conn.Execute("BEGIN"));
      auto status = write_conn.ExecuteFormat("INSERT INTO t (key) VALUES ($0)", write_key);
      if (status.ok()) {
        status = write_conn.Execute("COMMIT");
      }
      if (status.ok()) {
        last_written.store(write_key, std::memory_order_release);
        ++write_key;
      } else {
        LOG(INFO) << "Write " << write_key << " failed: " << status;
      }
    }
  });

  auto se = ScopeExit([&stop, &write_thread] {
    stop.store(true, std::memory_order_release);
    write_thread.join();
  });

  auto deadline = CoarseMonoClock::now() + 30s;

  while (CoarseMonoClock::now() < deadline) {
    int read_key = last_written.load(std::memory_order_acquire);
    if (read_key == 0) {
      std::this_thread::sleep_for(100ms);
      continue;
    }

    SCOPED_TRACE(Format("Reading: $0", read_key));

    ASSERT_OK(conn.Execute("BEGIN"));

    auto res = ASSERT_RESULT(conn.FetchFormat("SELECT * FROM t WHERE key = $0", read_key));
    auto columns = PQnfields(res.get());
    ASSERT_EQ(1, columns);

    auto lines = PQntuples(res.get());
    ASSERT_EQ(1, lines);

    auto key = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
    ASSERT_EQ(key, read_key);

    ASSERT_OK(conn.Execute("ROLLBACK"));
  }

  ASSERT_GE(last_written.load(std::memory_order_acquire), 100);
}

// Concurrently insert records into tables with foreign key relationship while truncating both.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(ConcurrentInsertTruncateForeignKey)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("DROP TABLE IF EXISTS t2"));
  ASSERT_OK(conn.Execute("DROP TABLE IF EXISTS t1"));
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (k int primary key, v int)"));
  ASSERT_OK(conn.Execute(
        "CREATE TABLE t2 (k int primary key, t1_k int, FOREIGN KEY (t1_k) REFERENCES t1 (k))"));

  const int kMaxKeys = 1 << 20;

  constexpr auto kWriteThreads = 4;
  constexpr auto kTruncateThreads = 2;

  TestThreadHolder thread_holder;
  for (int i = 0; i != kWriteThreads; ++i) {
    thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
      auto write_conn = ASSERT_RESULT(Connect());
      while (!stop.load(std::memory_order_acquire)) {
        int t1_k = RandomUniformInt(0, kMaxKeys - 1);
        int t1_v = RandomUniformInt(0, kMaxKeys - 1);
        auto status = write_conn.ExecuteFormat("INSERT INTO t1 VALUES ($0, $1)", t1_k, t1_v);
        int t2_k = RandomUniformInt(0, kMaxKeys - 1);
        status = write_conn.ExecuteFormat("INSERT INTO t2 VALUES ($0, $1)", t2_k, t1_k);
      }
    });
  }

  for (int i = 0; i != kTruncateThreads; ++i) {
    thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
      auto truncate_conn = ASSERT_RESULT(Connect());
      int idx = 0;
      while (!stop.load(std::memory_order_acquire)) {
        auto status = truncate_conn.Execute("TRUNCATE TABLE t1, t2 CASCADE");
        ++idx;
        std::this_thread::sleep_for(100ms);
      }
    });
  }

  thread_holder.WaitAndStop(30s);
}

// Concurrently insert records to table with index.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(ConcurrentIndexInsert)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE IF NOT EXISTS users(id text, ename text, age int, PRIMARY KEY(id))"));

  ASSERT_OK(conn.Execute(
      "CREATE INDEX IF NOT EXISTS name_idx ON users(ename)"));

  constexpr auto kWriteThreads = 4;

  std::atomic<bool> stop(false);
  std::vector<std::thread> write_threads;

  while (write_threads.size() != kWriteThreads) {
    write_threads.emplace_back([this, &stop] {
      auto write_conn = ASSERT_RESULT(Connect());
      auto this_thread_id = std::this_thread::get_id();
      auto tid = std::hash<decltype(this_thread_id)>()(this_thread_id);
      int idx = 0;
      while (!stop.load(std::memory_order_acquire)) {
        ASSERT_OK(write_conn.ExecuteFormat(
            "INSERT INTO users (id, ename, age) VALUES ('user-$0-$1', 'name-$1', $2)",
            tid, idx, 20 + (idx % 50)));
        ++idx;
      }
    });
  }

  auto se = ScopeExit([&stop, &write_threads] {
    stop.store(true, std::memory_order_release);
    for (auto& thread : write_threads) {
      thread.join();
    }
  });

  std::this_thread::sleep_for(30s);
}

Result<int64_t> ReadSumBalance(
    PGConn* conn, int accounts, IsolationLevel isolation,
    std::atomic<int>* counter) {
  RETURN_NOT_OK(conn->StartTransaction(isolation));
  bool failed = true;
  auto se = ScopeExit([conn, &failed] {
    if (failed) {
      EXPECT_OK(conn->Execute("ROLLBACK"));
    }
  });

  std::string query = "";
  for (int i = 1; i <= accounts; ++i) {
    if (!query.empty()) {
      query += " UNION ";
    }
    query += Format("SELECT balance, id FROM account_$0 WHERE id = $0", i);
  }

  auto res = VERIFY_RESULT(conn->FetchMatrix(query, accounts, 2));
  int64_t sum = 0;
  for (int i = 0; i != accounts; ++i) {
    sum += VERIFY_RESULT(GetValue<int64_t>(res.get(), i, 0));
  }

  failed = false;
  RETURN_NOT_OK(conn->Execute("COMMIT"));
  return sum;
}

void PgLibPqTest::TestMultiBankAccount(IsolationLevel isolation) {
  constexpr int kAccounts = RegularBuildVsSanitizers(20, 10);
  constexpr int64_t kInitialBalance = 100;

#ifndef NDEBUG
  const auto kTimeout = 180s;
  constexpr int kThreads = RegularBuildVsSanitizers(12, 5);
#else
  const auto kTimeout = 60s;
  constexpr int kThreads = 5;
#endif

  PGConn conn = ASSERT_RESULT(Connect());
  std::vector<PGConn> thread_connections;
  for (int i = 0; i < kThreads; ++i) {
    thread_connections.push_back(ASSERT_RESULT(Connect()));
  }

  for (int i = 1; i <= kAccounts; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE account_$0 (id int, balance bigint, PRIMARY KEY(id))", i));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO account_$0 (id, balance) VALUES ($0, $1)", i, kInitialBalance));
  }

  std::atomic<int> writes(0);
  std::atomic<int> reads(0);

  constexpr auto kRequiredReads = RegularBuildVsSanitizers(5, 2);
  constexpr auto kRequiredWrites = RegularBuildVsSanitizers(1000, 500);

  std::atomic<int> counter(100000);
  TestThreadHolder thread_holder;
  for (int i = 1; i <= kThreads; ++i) {
    thread_holder.AddThreadFunctor(
        [&conn = thread_connections[i - 1], &writes, &isolation,
         &stop_flag = thread_holder.stop_flag()]() {
      while (!stop_flag.load(std::memory_order_acquire)) {
        int from = RandomUniformInt(1, kAccounts);
        int to = RandomUniformInt(1, kAccounts - 1);
        if (to >= from) {
          ++to;
        }
        int64_t amount = RandomUniformInt(1, 10);
        ASSERT_OK(conn.StartTransaction(isolation));
        auto status = conn.ExecuteFormat(
              "UPDATE account_$0 SET balance = balance - $1 WHERE id = $0", from, amount);
        if (status.ok()) {
          status = conn.ExecuteFormat(
              "UPDATE account_$0 SET balance = balance + $1 WHERE id = $0", to, amount);
        }
        if (status.ok()) {
          status = conn.Execute("COMMIT;");
        } else {
          ASSERT_OK(conn.Execute("ROLLBACK;"));
        }
        if (!status.ok()) {
          ASSERT_TRUE(TransactionalFailure(status)) << status;
        } else {
          LOG(INFO) << "Updated: " << from << " => " << to << " by " << amount;
          ++writes;
        }
      }
    });
  }

  thread_holder.AddThreadFunctor(
      [this, &counter, &reads, &writes, isolation, &stop_flag = thread_holder.stop_flag()]() {
    SetFlagOnExit set_flag_on_exit(&stop_flag);
    auto conn = ASSERT_RESULT(Connect());
    auto failures_in_row = 0;
    while (!stop_flag.load(std::memory_order_acquire)) {
      if (isolation == IsolationLevel::SERIALIZABLE_ISOLATION) {
        auto lower_bound = reads.load() * kRequiredWrites < writes.load() * kRequiredReads
            ? 1.0 - 1.0 / (1ULL << failures_in_row) : 0.0;
        ASSERT_OK(conn.ExecuteFormat("SET yb_transaction_priority_lower_bound = $0", lower_bound));
      }
      auto sum = ReadSumBalance(&conn, kAccounts, isolation, &counter);
      if (!sum.ok()) {
        // Do not overflow long when doing bitshift above.
        failures_in_row = std::min(failures_in_row + 1, 63);
        ASSERT_TRUE(TransactionalFailure(sum.status())) << sum.status();
      } else {
        failures_in_row = 0;
        ASSERT_EQ(*sum, kAccounts * kInitialBalance);
        ++reads;
      }
    }
  });

  auto wait_status = WaitFor([&reads, &writes, &stop = thread_holder.stop_flag()] {
    return stop.load() || (writes.load() >= kRequiredWrites && reads.load() >= kRequiredReads);
  }, kTimeout, Format("At least $0 reads and $1 writes", kRequiredReads, kRequiredWrites));

  LOG(INFO) << "Writes: " << writes.load() << ", reads: " << reads.load();

  ASSERT_OK(wait_status);

  thread_holder.Stop();

  ASSERT_OK(WaitFor([&conn, isolation, &counter]() -> Result<bool> {
    auto sum = ReadSumBalance(&conn, kAccounts, isolation, &counter);
    if (!sum.ok()) {
      if (!TransactionalFailure(sum.status())) {
        return sum.status();
      }
      return false;
    }
    EXPECT_EQ(*sum, kAccounts * kInitialBalance);
    return true;
  }, 10s, "Final read"));

  auto total_not_found = 0;
  for (auto* tserver : cluster_->tserver_daemons()) {
    auto tablets = ASSERT_RESULT(cluster_->GetTabletIds(tserver));
    for (const auto& tablet : tablets) {
      auto result = tserver->GetInt64Metric(
          &METRIC_ENTITY_tablet, tablet.c_str(), &METRIC_transaction_not_found, "value");
      if (result.ok()) {
        total_not_found += *result;
      } else {
        ASSERT_TRUE(result.status().IsNotFound()) << result.status();
      }
    }
  }

  LOG(INFO) << "Total not found: " << total_not_found;
  // Check that total not found is not too big.
  ASSERT_LE(total_not_found, 200);
}

class PgLibPqSmallClockSkewTest : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Use small clock skew, to decrease number of read restarts.
    options->extra_tserver_flags.push_back("--max_clock_skew_usec=5000");
  }
};

TEST_F_EX(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(MultiBankAccountSnapshot),
          PgLibPqSmallClockSkewTest) {
  TestMultiBankAccount(IsolationLevel::SNAPSHOT_ISOLATION);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(MultiBankAccountSerializable)) {
  TestMultiBankAccount(IsolationLevel::SERIALIZABLE_ISOLATION);
}

void PgLibPqTest::DoIncrement(int key, int num_increments, IsolationLevel isolation) {
  auto conn = ASSERT_RESULT(Connect());

  // Perform increments
  int succeeded_incs = 0;
  while (succeeded_incs < num_increments) {
    ASSERT_OK(conn.StartTransaction(isolation));
    bool committed = false;
    auto exec_status = conn.ExecuteFormat("UPDATE t SET value = value + 1 WHERE key = $0", key);
    if (exec_status.ok()) {
      auto commit_status = conn.Execute("COMMIT");
      if (commit_status.ok()) {
        succeeded_incs++;
        committed = true;
      }
    }
    if (!committed) {
      ASSERT_OK(conn.Execute("ROLLBACK"));
    }
  }
}

void PgLibPqTest::TestParallelCounter(IsolationLevel isolation) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT, value INT)"));

  const auto kThreads = RegularBuildVsSanitizers(3, 2);
  const auto kIncrements = RegularBuildVsSanitizers(100, 20);

  // Make a counter for each thread and have each thread increment it
  std::vector<std::thread> threads;
  while (threads.size() != kThreads) {
    int key = threads.size();
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (key, value) VALUES ($0, 0)", key));

    threads.emplace_back([this, key, isolation] {
      DoIncrement(key, kIncrements, isolation);
    });
  }

  // Wait for completion
  for (auto& thread : threads) {
    thread.join();
  }

  // Check each counter
  for (int i = 0; i < kThreads; i++) {
    auto res = ASSERT_RESULT(conn.FetchFormat("SELECT value FROM t WHERE key = $0", i));

    auto row_val = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
    ASSERT_EQ(row_val, kIncrements);
  }
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TestParallelCounterSerializable)) {
  TestParallelCounter(IsolationLevel::SERIALIZABLE_ISOLATION);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TestParallelCounterRepeatableRead)) {
  TestParallelCounter(IsolationLevel::SNAPSHOT_ISOLATION);
}

void PgLibPqTest::TestConcurrentCounter(IsolationLevel isolation) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT, value INT)"));

  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (0, 0)"));

  const auto kThreads = RegularBuildVsSanitizers(3, 2);
  const auto kIncrements = RegularBuildVsSanitizers(100, 20);

  // Have each thread increment the same already-created counter
  std::vector<std::thread> threads;
  while (threads.size() != kThreads) {
    threads.emplace_back([this, isolation] {
      DoIncrement(0, kIncrements, isolation);
    });
  }

  // Wait for completion
  for (auto& thread : threads) {
    thread.join();
  }

  // Check that we incremented exactly the desired number of times
  auto res = ASSERT_RESULT(conn.Fetch("SELECT value FROM t WHERE key = 0"));

  auto row_val = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
  ASSERT_EQ(row_val, kThreads * kIncrements);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TestConcurrentCounterSerializable)) {
  TestConcurrentCounter(IsolationLevel::SERIALIZABLE_ISOLATION);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TestConcurrentCounterRepeatableRead)) {
  TestConcurrentCounter(IsolationLevel::SNAPSHOT_ISOLATION);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(SecondaryIndexInsertSelect)) {
  constexpr int kThreads = 4;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (a INT PRIMARY KEY, b INT)"));
  ASSERT_OK(conn.Execute("CREATE INDEX ON t (b, a)"));

  TestThreadHolder holder;
  std::array<std::atomic<int>, kThreads> written;
  for (auto& w : written) {
    w.store(0, std::memory_order_release);
  }

  for (int i = 0; i != kThreads; ++i) {
    holder.AddThread([this, i, &stop = holder.stop_flag(), &written] {
      auto conn = ASSERT_RESULT(Connect());
      SetFlagOnExit set_flag_on_exit(&stop);
      int key = 0;

      while (!stop.load(std::memory_order_acquire)) {
        if (RandomUniformBool()) {
          int a = i * 1000000 + key;
          int b = key;
          ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (a, b) VALUES ($0, $1)", a, b));
          written[i].store(++key, std::memory_order_release);
        } else {
          int writer_index = RandomUniformInt(0, kThreads - 1);
          int num_written = written[writer_index].load(std::memory_order_acquire);
          if (num_written == 0) {
            continue;
          }
          int read_key = num_written - 1;
          int b = read_key;
          int read_a = ASSERT_RESULT(conn.FetchValue<int32_t>(
              Format("SELECT a FROM t WHERE b = $0 LIMIT 1", b)));
          ASSERT_EQ(read_a % 1000000, read_key);
        }
      }
    });
  }

  holder.WaitAndStop(60s);
}

void AssertRows(PGConn *conn, int expected_num_rows) {
  auto res = ASSERT_RESULT(conn->Fetch("SELECT * FROM test"));
  ASSERT_EQ(PQntuples(res.get()), expected_num_rows);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(InTxnDelete)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE test (pk int PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1)"));
  ASSERT_NO_FATALS(AssertRows(&conn, 1));
  ASSERT_OK(conn.Execute("DELETE FROM test"));
  ASSERT_NO_FATALS(AssertRows(&conn, 0));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1)"));
  ASSERT_NO_FATALS(AssertRows(&conn, 1));
  ASSERT_OK(conn.Execute("COMMIT"));

  ASSERT_NO_FATALS(AssertRows(&conn, 1));
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(CompoundKeyColumnOrder)) {
  const string table_name = "test";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (r2 int, r1 int, h int, v2 int, v1 int, primary key (h, r1, r2))",
      table_name));
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  yb::client::YBSchema schema;
  PartitionSchema partition_schema;
  bool table_found = false;
  // TODO(dmitry): Find table by name instead of checking all the tables when catalog_mangager
  // will be able to find YSQL tables
  const auto tables = ASSERT_RESULT(client->ListTables());
  for (const auto& t : tables) {
    if (t.namespace_type() == YQLDatabase::YQL_DATABASE_PGSQL && t.table_name() == table_name) {
      table_found = true;
      ASSERT_OK(client->GetTableSchema(t, &schema, &partition_schema));
      const auto& columns = schema.columns();
      std::array<string, 5> expected_column_names{"h", "r1", "r2", "v2", "v1"};
      ASSERT_EQ(expected_column_names.size(), columns.size());
      for (size_t i = 0; i < expected_column_names.size(); ++i) {
        ASSERT_EQ(columns[i].name(), expected_column_names[i]);
      }
      break;
    }
  }
  ASSERT_TRUE(table_found);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(BulkCopy)) {
  const std::string kTableName = "customer";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE CUSTOMER ( CUSTKEY     INTEGER NOT NULL PRIMARY KEY,\n"
      "                        NAME        VARCHAR(25) NOT NULL,\n"
      "                        ADDRESS     VARCHAR(40) NOT NULL,\n"
      "                        NATIONKEY   INTEGER NOT NULL,\n"
      "                        PHONE       CHAR(15) NOT NULL,\n"
      "                        MKTSEGMENT  CHAR(10) NOT NULL,\n"
      "                        COMMENT     VARCHAR(117) NOT NULL);",
      kTableName));

  constexpr int kNumBatches = 10;
  constexpr int kBatchSize = 1000;

  int customer_key = 0;
  for (int i = 0; i != kNumBatches; ++i) {
    ASSERT_OK(conn.CopyBegin(Format("COPY $0 FROM STDIN WITH BINARY", kTableName)));
    for (int j = 0; j != kBatchSize; ++j) {
      conn.CopyStartRow(7);
      conn.CopyPutInt32(++customer_key);
      conn.CopyPutString(Format("Name $0 $1", i, j));
      conn.CopyPutString(Format("Address $0 $1", i, j));
      conn.CopyPutInt32(i);
      conn.CopyPutString(std::to_string(999999876543210 + customer_key));
      conn.CopyPutString(std::to_string(9876543210 + customer_key));
      conn.CopyPutString(Format("Comment $0 $1", i, j));
    }

    ASSERT_OK(conn.CopyEnd());
  }

  LOG(INFO) << "Finished copy";
  for (;;) {
    auto result = conn.FetchFormat("SELECT COUNT(*) FROM $0", kTableName);
    if (result.ok()) {
      LogResult(result->get());
      auto count = ASSERT_RESULT(GetInt64(result->get(), 0, 0));
      LOG(INFO) << "Total count: " << count;
      ASSERT_EQ(count, kNumBatches * kBatchSize);
      break;
    } else {
      auto message = result.status().ToString();
      ASSERT_TRUE(message.find("Snaphost too old") != std::string::npos) << result.status();
    }
  }
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(CatalogManagerMapsTest)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test_db"));
  {
    auto test_conn = ASSERT_RESULT(ConnectToDB("test_db"));
    ASSERT_OK(test_conn.Execute("CREATE TABLE foo (a int PRIMARY KEY)"));
    ASSERT_OK(test_conn.Execute("ALTER TABLE foo RENAME TO bar"));
    ASSERT_OK(test_conn.Execute("ALTER TABLE bar RENAME COLUMN a to b"));
  }
  ASSERT_OK(conn.Execute("ALTER DATABASE test_db RENAME TO test_db_renamed"));

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  Result<bool> result(false);
  result = client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, "test_db_renamed", "bar"));
  ASSERT_OK(result);
  ASSERT_TRUE(result.get());
  result = client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, "test_db_renamed", "foo"));
  ASSERT_OK(result);
  ASSERT_FALSE(result.get());
  result = client->NamespaceExists("test_db_renamed", YQL_DATABASE_PGSQL);
  ASSERT_OK(result);
  ASSERT_TRUE(result.get());
  result = client->NamespaceExists("test_db", YQL_DATABASE_PGSQL);
  ASSERT_OK(result);
  ASSERT_FALSE(result.get());

  string ns_id;
  auto list_result = client->ListNamespaces(YQL_DATABASE_PGSQL);
  ASSERT_OK(list_result);
  for (const auto& ns : list_result.get()) {
    if (ns.name() == "test_db_renamed") {
      ns_id = ns.id();
    }
  }

  client::YBSchema schema;
  PartitionSchema partition_schema;
  ASSERT_OK(client->GetTableSchema(
      {YQL_DATABASE_PGSQL, ns_id, "test_db_renamed", "bar"}, &schema, &partition_schema));
  ASSERT_EQ(schema.num_columns(), 1);
  ASSERT_EQ(schema.Column(0).name(), "b");
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TestSystemTableRollback)) {
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE pktable (ptest1 int PRIMARY KEY);"));
  Status s = conn1.Execute("CREATE TABLE fktable (ftest1 inet REFERENCES pktable);");
  LOG(INFO) << "Status of second table creation: " << s;
  auto res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM pg_class WHERE relname='fktable'"));
  ASSERT_EQ(0, PQntuples(res.get()));
}

namespace {
Result<master::TabletLocationsPB> GetColocatedTabletLocations(
    client::YBClient* client,
    std::string database_name,
    MonoDelta timeout) {
  std::string ns_id;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;

  bool exists = VERIFY_RESULT(client->NamespaceExists(database_name, YQL_DATABASE_PGSQL));
  if (!exists) {
    return STATUS(NotFound, "namespace does not exist");
  }

  // Get namespace id.
  for (const auto& ns : VERIFY_RESULT(client->ListNamespaces(YQL_DATABASE_PGSQL))) {
    if (ns.name() == database_name) {
      ns_id = ns.id();
      break;
    }
  }
  if (ns_id.empty()) {
    return STATUS(NotFound, "namespace not found");
  }

  // Get TabletLocations for the colocated tablet.
  RETURN_NOT_OK(WaitFor(
      [&]() -> Result<bool> {
        Status s = client->GetTabletsFromTableId(
            ns_id + master::kColocatedParentTableIdSuffix,
            0 /* max_tablets */,
            &tablets);
        if (s.ok()) {
          return tablets.size() == 1;
        } else if (s.IsNotFound()) {
          return false;
        } else {
          return s;
        }
      },
      timeout,
      "wait for colocated parent tablet"));

  return tablets[0];
}

Result<master::TabletLocationsPB> GetTablegroupTabletLocations(
    client::YBClient* client,
    std::string database_name,
    std::string tablegroup_id,
    MonoDelta timeout) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;

  bool exists = VERIFY_RESULT(client->TablegroupExists(database_name, tablegroup_id));
  if (!exists) {
    return STATUS(NotFound, "tablegroup does not exist");
  }

  // Get TabletLocations for the tablegroup tablet.
  RETURN_NOT_OK(WaitFor(
      [&]() -> Result<bool> {
        Status s = client->GetTabletsFromTableId(
            tablegroup_id + master::kTablegroupParentTableIdSuffix,
            0 /* max_tablets */,
            &tablets);
        if (s.ok()) {
          return tablets.size() == 1;
        } else if (s.IsNotFound()) {
          return false;
        } else {
          return s;
        }
      },
      timeout,
      "wait for tablegroup parent tablet"));

  return tablets[0];
}

Result<string> GetTableIdByTableName(
    client::YBClient* client, const string& namespace_name, const string& table_name) {
  const auto tables = VERIFY_RESULT(client->ListTables());
  for (const auto& t : tables) {
    if (t.namespace_name() == namespace_name && t.table_name() == table_name) {
      return t.table_id();
    }
  }
  return STATUS(NotFound, "The table does not exist");
}
} // namespace

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TableColocation)) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseName = "test_db";
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_bar_index;
  string ns_id;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", kDatabaseName));
  conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));

  // A parent table with one tablet should be created when the database is created.
  auto colocated_tablet_id = ASSERT_RESULT(GetColocatedTabletLocations(
      client.get(),
      kDatabaseName,
      30s))
    .tablet_id();

  // Create a range partition table, the table should share the tablet with the parent table.
  ASSERT_OK(conn.Execute("CREATE TABLE foo (a INT, PRIMARY KEY (a ASC))"));
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "foo"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), colocated_tablet_id);

  // Create a colocated index table.
  ASSERT_OK(conn.Execute("CREATE INDEX foo_index1 ON foo (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "foo_index1"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), colocated_tablet_id);

  // Create a hash partition table and opt out of using the parent tablet.
  ASSERT_OK(conn.Execute("CREATE TABLE bar (a INT) WITH (colocated = false)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "bar"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  for (auto& tablet : tablets) {
    ASSERT_NE(tablet.tablet_id(), colocated_tablet_id);
  }

  // Create an index on the non-colocated table. The index should follow the table and opt out of
  // colocation.
  ASSERT_OK(conn.Execute("CREATE INDEX bar_index ON bar (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "bar_index"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  for (auto& tablet : tablets) {
    ASSERT_NE(tablet.tablet_id(), colocated_tablet_id);
  }
  tablets_bar_index.Swap(&tablets);

  // Create a range partition table without specifying primary key.
  ASSERT_OK(conn.Execute("CREATE TABLE baz (a INT)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "baz"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), colocated_tablet_id);

  // Create another table and index.
  ASSERT_OK(conn.Execute("CREATE TABLE qux (a INT, PRIMARY KEY (a ASC)) WITH (colocated = true)"));
  ASSERT_OK(conn.Execute("CREATE INDEX qux_index ON qux (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "qux_index"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));

  // Drop a table in the parent tablet.
  ASSERT_OK(conn.Execute("DROP TABLE qux"));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "qux"))));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "qux_index"))));

  // Drop a table that is opted out.
  ASSERT_OK(conn.Execute("DROP TABLE bar"));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "bar"))));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "bar_index"))));

  // The tablets for bar_index should be deleted.
  std::vector<bool> tablet_founds(tablets_bar_index.size(), true);
  ASSERT_OK(WaitFor(
      [&] {
        for (int i = 0; i < tablets_bar_index.size(); ++i) {
          client->LookupTabletById(
              tablets_bar_index[i].tablet_id(),
              CoarseMonoClock::Now() + 30s,
              [&, i](const Result<client::internal::RemoteTabletPtr>& result) {
                tablet_founds[i] = result.ok();
              },
              client::UseCache::kFalse);
        }
        return std::all_of(
            tablet_founds.cbegin(),
            tablet_founds.cend(),
            [](bool tablet_found) {
              return !tablet_found;
            });
      },
      30s, "Drop table opted out of colocation"));

  // Drop the database.
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kDatabaseName));
  ASSERT_FALSE(ASSERT_RESULT(client->NamespaceExists(kDatabaseName, YQL_DATABASE_PGSQL)));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "foo"))));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "foo_index1"))));

  // The colocation tablet should be deleted.
  bool tablet_found = true;
  int rpc_calls = 0;
  ASSERT_OK(WaitFor(
      [&] {
        rpc_calls++;
        client->LookupTabletById(
            colocated_tablet_id,
            CoarseMonoClock::Now() + 30s,
            [&](const Result<client::internal::RemoteTabletPtr>& result) {
              tablet_found = result.ok();
              rpc_calls--;
            },
            client::UseCache::kFalse);
        return !tablet_found;
      },
      30s, "Drop colocated database"));
  // To prevent an "AddressSanitizer: stack-use-after-scope", do not return from this function until
  // all callbacks are done.
  ASSERT_OK(WaitFor(
      [&rpc_calls] {
        LOG(INFO) << "Waiting for " << rpc_calls << " RPCs to run callbacks";
        return rpc_calls == 0;
      },
      30s, "Drop colocated database (wait for RPCs to finish)"));
}

// Test for ensuring that transaction conflicts work as expected for colocated tables.
// Related to https://github.com/yugabyte/yugabyte-db/issues/3251.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TxnConflictsForColocatedTables)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test_db WITH colocated = true"));

  auto conn1 = ASSERT_RESULT(ConnectToDB("test_db"));
  auto conn2 = ASSERT_RESULT(ConnectToDB("test_db"));

  ASSERT_OK(conn1.Execute("CREATE TABLE t (a INT, PRIMARY KEY (a ASC))"));
  ASSERT_OK(conn1.Execute("INSERT INTO t(a) VALUES(1)"));

  // From conn1, select the row in UPDATE row lock mode. From conn2, delete the row.
  // Ensure that conn1's transaction will detect a conflict at the time of commit.
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  auto res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM t FOR UPDATE"));
  ASSERT_EQ(PQntuples(res.get()), 1);

  auto status = conn2.Execute("DELETE FROM t WHERE a = 1");
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(PgsqlError(status), YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "Conflicts with higher priority transaction");

  ASSERT_OK(conn1.CommitTransaction());

  // Ensure that reads to separate tables in a colocated database do not conflict.
  ASSERT_OK(conn1.Execute("CREATE TABLE t2 (a INT, PRIMARY KEY (a ASC))"));
  ASSERT_OK(conn1.Execute("INSERT INTO t2(a) VALUES(1)"));

  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));

  res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM t FOR UPDATE"));
  ASSERT_EQ(PQntuples(res.get()), 1);
  res = ASSERT_RESULT(conn2.Fetch("SELECT * FROM t2 FOR UPDATE"));
  ASSERT_EQ(PQntuples(res.get()), 1);

  ASSERT_OK(conn1.CommitTransaction());
  ASSERT_OK(conn2.CommitTransaction());
}

class PgLibPqTablegroupTest : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Enable tablegroup beta feature
    options->extra_tserver_flags.push_back("--ysql_beta_feature_tablegroup=true");
    options->extra_master_flags.push_back("--ysql_beta_feature_tablegroup=true");
  }
};

TEST_F_EX(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(ColocatedTablegroups),
          PgLibPqTablegroupTest) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseName ="tgroup_test_db";
  const string kTablegroupName ="test_tgroup";
  const string kTablegroupAltName ="test_alt_tgroup";
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_bar_index;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDatabaseName));
  conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLEGROUP $0", kTablegroupName));

  // A parent table with one tablet should be created when the tablegroup is created.
  auto res = ASSERT_RESULT(conn.FetchFormat("SELECT oid FROM pg_database WHERE datname=\'$0\'",
                                            kDatabaseName));
  int database_oid = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
  res = ASSERT_RESULT(conn.FetchFormat("SELECT oid FROM pg_tablegroup WHERE grpname=\'$0\'",
                                       kTablegroupName));
  int tablegroup_oid = ASSERT_RESULT(GetInt32(res.get(), 0, 0));

  auto tablegroup_tablet_id = ASSERT_RESULT(GetTablegroupTabletLocations(
      client.get(),
      kDatabaseName,
      GetPgsqlTablegroupId(database_oid, tablegroup_oid),
      30s))
    .tablet_id();

  // Create a range partition table, the table should share the tablet with the parent table.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE foo (a INT, PRIMARY KEY (a ASC)) TABLEGROUP $0",
                               kTablegroupName));
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "foo"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup_tablet_id);

  // Create a index table that uses the tablegroup by default.
  ASSERT_OK(conn.Execute("CREATE INDEX foo_index1 ON foo (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "foo_index1"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup_tablet_id);

  // Create a hash partition table and dont use tablegroup.
  ASSERT_OK(conn.Execute("CREATE TABLE bar (a INT)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "bar"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  for (auto& tablet : tablets) {
    ASSERT_NE(tablet.tablet_id(), tablegroup_tablet_id);
  }

  // Create an index on the table not in a tablegroup. The index should follow the table
  // and opt out of the tablegroup.
  ASSERT_OK(conn.Execute("CREATE INDEX bar_index ON bar (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "bar_index"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  for (auto& tablet : tablets) {
    ASSERT_NE(tablet.tablet_id(), tablegroup_tablet_id);
  }
  tablets_bar_index.Swap(&tablets);

  // Create a range partition table without specifying primary key.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE baz (a INT) TABLEGROUP $0", kTablegroupName));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "baz"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup_tablet_id);

  // Create another table and index.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE qux (a INT, PRIMARY KEY (a ASC)) TABLEGROUP $0",
                               kTablegroupName));
  ASSERT_OK(conn.Execute("CREATE INDEX qux_index ON qux (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "qux"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup_tablet_id);
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "qux_index"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup_tablet_id);

  // Now create a second tablegroup.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLEGROUP $0", kTablegroupAltName));

  // A parent table with one tablet should be created when the tablegroup is created.
  res = ASSERT_RESULT(conn.FetchFormat("SELECT oid FROM pg_tablegroup WHERE grpname=\'$0\'",
                                       kTablegroupAltName));
  tablegroup_oid = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
  auto tablegroup_alt_tablet_id = ASSERT_RESULT(GetTablegroupTabletLocations(
      client.get(),
      kDatabaseName,
      GetPgsqlTablegroupId(database_oid, tablegroup_oid),
      30s))
    .tablet_id();

  // Create another range partition table - should be part of the second tablegroup
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE quuz (a INT, PRIMARY KEY (a ASC)) TABLEGROUP $0",
                               kTablegroupAltName));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "quuz"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup_alt_tablet_id);

    // Drop a table in the parent tablet.
  ASSERT_OK(conn.Execute("DROP TABLE quuz"));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "quuz"))));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "quuz_index"))));

  // Drop a table that is opted out.
  ASSERT_OK(conn.Execute("DROP TABLE bar"));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "bar"))));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "bar_index"))));

  // The tablets for bar_index should be deleted.
  std::vector<bool> tablet_founds(tablets_bar_index.size(), true);
  ASSERT_OK(WaitFor(
      [&] {
        for (int i = 0; i < tablets_bar_index.size(); ++i) {
          client->LookupTabletById(
              tablets_bar_index[i].tablet_id(),
              CoarseMonoClock::Now() + 30s,
              [&, i](const Result<client::internal::RemoteTabletPtr>& result) {
                tablet_founds[i] = result.ok();
              },
              client::UseCache::kFalse);
        }
        return std::all_of(
            tablet_founds.cbegin(),
            tablet_founds.cend(),
            [](bool tablet_found) {
              return !tablet_found;
            });
      },
      30s, "Drop table did not use tablegroups"));

  // Drop a tablegroup.
  ASSERT_OK(conn.ExecuteFormat("DROP TABLEGROUP $0", kTablegroupAltName));
  ASSERT_FALSE(ASSERT_RESULT(client->TablegroupExists(kDatabaseName, kTablegroupAltName)));

  // The alt tablegroup tablet should be deleted after dropping the tablegroup.
  bool alt_tablet_found = true;
  int rpc_calls = 0;
  ASSERT_OK(WaitFor(
      [&] {
        rpc_calls++;
        client->LookupTabletById(
            tablegroup_alt_tablet_id,
            CoarseMonoClock::Now() + 30s,
            [&](const Result<client::internal::RemoteTabletPtr>& result) {
              alt_tablet_found = result.ok();
              rpc_calls--;
            },
            client::UseCache::kFalse);
        return !alt_tablet_found;
      },
      30s, "Drop tablegroup"));

  // Recreate that tablegroup. Being able to recreate it and add tables to it tests that it was
  // properly cleaned up from catalog manager maps and postgres metadata at time of DROP.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLEGROUP $0", kTablegroupAltName));

  // A parent table with one tablet should be created when the tablegroup is created.
  res = ASSERT_RESULT(conn.FetchFormat("SELECT oid FROM pg_tablegroup WHERE grpname=\'$0\'",
                                       kTablegroupAltName));
  tablegroup_oid = ASSERT_RESULT(GetInt32(res.get(), 0, 0));

  tablegroup_alt_tablet_id = ASSERT_RESULT(GetTablegroupTabletLocations(
      client.get(),
      kDatabaseName,
      GetPgsqlTablegroupId(database_oid, tablegroup_oid),
      30s))
    .tablet_id();
  // Add a table back in and ensure that it is part of the recreated tablegroup.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE quuz (a INT, PRIMARY KEY (a ASC)) TABLEGROUP $0",
                               kTablegroupAltName));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "quuz"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup_alt_tablet_id);

  // Drop the database.
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kDatabaseName));
  ASSERT_FALSE(ASSERT_RESULT(client->NamespaceExists(kDatabaseName, YQL_DATABASE_PGSQL)));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "foo"))));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "foo_index1"))));

  // The original tablegroup tablet should be deleted after dropping the database.
  bool orig_tablet_found = true;
  ASSERT_OK(WaitFor(
      [&] {
        rpc_calls++;
        client->LookupTabletById(
            tablegroup_tablet_id,
            CoarseMonoClock::Now() + 30s,
            [&](const Result<client::internal::RemoteTabletPtr>& result) {
              orig_tablet_found = result.ok();
              rpc_calls--;
            },
            client::UseCache::kFalse);
        return !orig_tablet_found;
      },
      30s, "Drop database with tablegroup"));

  // The second tablegroup tablet should also be deleted after dropping the database.
  bool second_tablet_found = true;
  ASSERT_OK(WaitFor(
      [&] {
        rpc_calls++;
        client->LookupTabletById(
            tablegroup_alt_tablet_id,
            CoarseMonoClock::Now() + 30s,
            [&](const Result<client::internal::RemoteTabletPtr>& result) {
              second_tablet_found = result.ok();
              rpc_calls--;
            },
            client::UseCache::kFalse);
        return !second_tablet_found;
      },
      30s, "Drop database with tablegroup"));

  // To prevent an "AddressSanitizer: stack-use-after-scope", do not return from this function until
  // all callbacks are done.
  ASSERT_OK(WaitFor(
      [&rpc_calls] {
        LOG(INFO) << "Waiting for " << rpc_calls << " RPCs to run callbacks";
        return rpc_calls == 0;
      },
      30s, "Drop database with tablegroup (wait for RPCs to finish)"));
}

// Test that the number of RPCs sent to master upon first connection is not too high.
// See https://github.com/yugabyte/yugabyte-db/issues/3049
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(NumberOfInitialRpcs)) {
  auto get_master_inbound_rpcs_created = [&cluster_ = this->cluster_]() -> Result<int64_t> {
    int64_t m_in_created = 0;
    for (auto* master : cluster_->master_daemons()) {
      m_in_created += VERIFY_RESULT(master->GetInt64Metric(
          &METRIC_ENTITY_server, "yb.master", &METRIC_rpc_inbound_calls_created, "value"));
    }
    return m_in_created;
  };

  int64_t rpcs_before = ASSERT_RESULT(get_master_inbound_rpcs_created());
  ASSERT_RESULT(Connect());
  int64_t rpcs_after  = ASSERT_RESULT(get_master_inbound_rpcs_created());
  int64_t rpcs_during = rpcs_after - rpcs_before;

  // Real-world numbers (debug build, local Mac): 328 RPCs before, 95 after the fix for #3049
  LOG(INFO) << "Master inbound RPC during connection: " << rpcs_during;
  // RPC counter is affected no only by table read/write operations but also by heartbeat mechanism.
  // As far as ASAN/TSAN builds are slower they can receive more heartbeats while
  // processing requests. As a result RPC count might be higher in comparison to other build types.
  ASSERT_LT(rpcs_during, RegularBuildVsSanitizers(150, 200));
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(RangePresplit)) {
  const string kDatabaseName ="yugabyte";
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  string ns_id;

  auto conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn.Execute("CREATE TABLE range(a int, PRIMARY KEY(a ASC)) " \
      "SPLIT AT VALUES ((100), (1000))"));

  // Get database and table IDs
  for (const auto& ns : ASSERT_RESULT(client->ListNamespaces(YQL_DATABASE_PGSQL))) {
    if (ns.name() == kDatabaseName) {
      ns_id = ns.id();
      break;
    }
  }
  ASSERT_FALSE(ns_id.empty());

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "range"));

  // Validate that number of tablets created is 3.
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 3);
}

// Override the base test to start a cluster that kicks out unresponsive tservers faster.
class PgLibPqTestSmallTSTimeout : public PgLibPqTest {
 public:
  PgLibPqTestSmallTSTimeout() {
    more_master_flags.push_back("--tserver_unresponsive_timeout_ms=8000");
    more_master_flags.push_back("--unresponsive_ts_rpc_timeout_ms=10000");
    more_tserver_flags.push_back("--follower_unavailable_considered_failed_sec=10");
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.insert(
        std::end(options->extra_master_flags),
        std::begin(more_master_flags),
        std::end(more_master_flags));
    options->extra_tserver_flags.insert(
        std::end(options->extra_tserver_flags),
        std::begin(more_tserver_flags),
        std::end(more_tserver_flags));
  }

 protected:
  std::vector<std::string> more_master_flags;
  std::vector<std::string> more_tserver_flags;
};

// Test that adding a tserver and removing a tserver causes the colocation tablet to adjust raft
// configuration off the old tserver and onto the new tserver.
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(LoadBalanceSingleColocatedDB),
          PgLibPqTestSmallTSTimeout) {
  const std::string kDatabaseName = "co";
  const auto kTimeout = 60s;
  const int starting_num_tablet_servers = cluster_->num_tablet_servers();
  std::string ns_id;
  std::map<std::string, int> ts_loads;

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", kDatabaseName));

  // Collect colocation tablet replica locations.
  {
    master::TabletLocationsPB tablet_locations = ASSERT_RESULT(GetColocatedTabletLocations(
        client.get(),
        kDatabaseName,
        kTimeout));
    for (const auto& replica : tablet_locations.replicas()) {
      ts_loads[replica.ts_info().permanent_uuid()]++;
    }
  }

  // Ensure each tserver has exactly one colocation tablet replica.
  ASSERT_EQ(ts_loads.size(), starting_num_tablet_servers);
  for (const auto& entry : ts_loads) {
    ASSERT_NOTNULL(cluster_->tablet_server_by_uuid(entry.first));
    ASSERT_EQ(entry.second, 1);
    LOG(INFO) << "found ts " << entry.first << " has " << entry.second << " replicas";
  }

  // Add a tablet server.
  ASSERT_OK(cluster_->AddTabletServer(ExternalMiniClusterOptions::kDefaultStartCqlProxy,
                                      more_tserver_flags));
  ASSERT_OK(cluster_->WaitForTabletServerCount(starting_num_tablet_servers + 1, kTimeout));

  // Wait for load balancing.  This should move some tablet-peers (e.g. of the colocation tablet,
  // system.transactions tablets) to the new tserver.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        bool is_idle = VERIFY_RESULT(client->IsLoadBalancerIdle());
        return !is_idle;
      },
      kTimeout,
      "wait for load balancer to be active"));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return client->IsLoadBalancerIdle();
      },
      kTimeout,
      "wait for load balancer to be idle"));

  // Remove a tablet server.
  cluster_->tablet_server(0)->Shutdown();

  // Wait for load balancing.  This should move the remaining tablet-peers off the dead tserver.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        bool is_idle = VERIFY_RESULT(client->IsLoadBalancerIdle());
        return !is_idle;
      },
      kTimeout,
      "wait for load balancer to be active"));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return client->IsLoadBalancerIdle();
      },
      kTimeout,
      "wait for load balancer to be idle"));

  // Collect colocation tablet replica locations.
  {
    master::TabletLocationsPB tablet_locations = ASSERT_RESULT(GetColocatedTabletLocations(
        client.get(),
        kDatabaseName,
        kTimeout));
    ts_loads.clear();
    for (const auto& replica : tablet_locations.replicas()) {
      ts_loads[replica.ts_info().permanent_uuid()]++;
    }
  }

  // Ensure each colocation tablet replica is on the three tablet servers excluding the first one,
  // which is shut down.
  ASSERT_EQ(ts_loads.size(), starting_num_tablet_servers);
  for (const auto& entry : ts_loads) {
    ExternalTabletServer* ts = cluster_->tablet_server_by_uuid(entry.first);
    ASSERT_NOTNULL(ts);
    ASSERT_NE(ts, cluster_->tablet_server(0));
    ASSERT_EQ(entry.second, 1);
    LOG(INFO) << "found ts " << entry.first << " has " << entry.second << " replicas";
  }
}

// Test that adding a tserver causes colocation tablets to offload tablet-peers to the new tserver.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(LoadBalanceMultipleColocatedDB)) {
  constexpr int kNumDatabases = 3;
  const auto kTimeout = 60s;
  const int starting_num_tablet_servers = cluster_->num_tablet_servers();
  const std::string kDatabasePrefix = "co";
  std::map<std::string, int> ts_loads;

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());

  for (int i = 0; i < kNumDatabases; ++i) {
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0$1 WITH colocated = true", kDatabasePrefix, i));
  }

  // Add a tablet server.
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(starting_num_tablet_servers + 1, kTimeout));

  // Wait for load balancing.  This should move some tablet-peers (e.g. of the colocation tablets,
  // system.transactions tablets) to the new tserver.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        bool is_idle = VERIFY_RESULT(client->IsLoadBalancerIdle());
        return !is_idle;
      },
      kTimeout,
      "wait for load balancer to be active"));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return client->IsLoadBalancerIdle();
      },
      kTimeout,
      "wait for load balancer to be idle"));

  // Collect colocation tablets' replica locations.
  for (int i = 0; i < kNumDatabases; ++i) {
    master::TabletLocationsPB tablet_locations = ASSERT_RESULT(GetColocatedTabletLocations(
        client.get(),
        Format("$0$1", kDatabasePrefix, i),
        kTimeout));
    for (const auto& replica : tablet_locations.replicas()) {
      ts_loads[replica.ts_info().permanent_uuid()]++;
    }
  }

  // Ensure that the load is properly distributed.
  int min_load = kNumDatabases;
  int max_load = 0;
  for (const auto& entry : ts_loads) {
    if (entry.second < min_load) {
      min_load = entry.second;
    } else if (entry.second > max_load) {
      max_load = entry.second;
    }
  }
  LOG(INFO) << "Found max_load on a TS = " << max_load << ", and min_load on a ts = " << min_load;
  ASSERT_LT(max_load - min_load, 2);
  ASSERT_EQ(ts_loads.size(), kNumDatabases + 1);
}

// Override the base test to start a cluster with index backfill enabled.
class PgLibPqTestIndexBackfill : public PgLibPqTest {
 public:
  PgLibPqTestIndexBackfill() {
    more_master_flags.push_back("--ysql_disable_index_backfill=false");
    more_tserver_flags.push_back("--ysql_disable_index_backfill=false");
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.insert(
        std::end(options->extra_master_flags),
        std::begin(more_master_flags),
        std::end(more_master_flags));
    options->extra_tserver_flags.insert(
        std::end(options->extra_tserver_flags),
        std::begin(more_tserver_flags),
        std::end(more_tserver_flags));
  }

 protected:
  std::vector<std::string> more_master_flags;
  std::vector<std::string> more_tserver_flags;
};

// Make sure that backfill works.
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(BackfillSimple),
          PgLibPqTestIndexBackfill) {
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (c char, i int, p point)", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ('a', 0, '(1, 2)')", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ('y', -5, '(0, -2)')", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ('b', 100, '(868, 9843)')", kTableName));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0 (c ASC)", kTableName));

  // Index scan to verify contents of index table.
  const std::string query = Format("SELECT * FROM $0 ORDER BY c", kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  auto res = ASSERT_RESULT(conn.Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 3);
  ASSERT_EQ(PQnfields(res.get()), 3);
  std::array<int, 3> values = {
    ASSERT_RESULT(GetInt32(res.get(), 0, 1)),
    ASSERT_RESULT(GetInt32(res.get(), 1, 1)),
    ASSERT_RESULT(GetInt32(res.get(), 2, 1)),
  };
  ASSERT_EQ(values[0], 0);
  ASSERT_EQ(values[1], 100);
  ASSERT_EQ(values[2], -5);
}

// Make sure that partial indexes work for index backfill.
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(BackfillPartial),
          PgLibPqTestIndexBackfill) {
  constexpr int kNumRows = 7;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(-1, -$1, -1))",
      kTableName,
      kNumRows));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0 (i ASC) WHERE j > -5", kTableName));

  // Index scan to verify contents of index table.
  {
    const std::string query = Format("SELECT j FROM $0 WHERE j > -3 ORDER BY i", kTableName);
    ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
    auto res = ASSERT_RESULT(conn.Fetch(query));
    ASSERT_EQ(PQntuples(res.get()), 2);
    ASSERT_EQ(PQnfields(res.get()), 1);
    std::array<int, 2> values = {
      ASSERT_RESULT(GetInt32(res.get(), 0, 0)),
      ASSERT_RESULT(GetInt32(res.get(), 1, 0)),
    };
    ASSERT_EQ(values[0], -1);
    ASSERT_EQ(values[1], -2);
  }
  {
    const std::string query = Format(
        "SELECT i FROM $0 WHERE j > -5 ORDER BY i DESC LIMIT 2",
        kTableName);
    ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
    auto res = ASSERT_RESULT(conn.Fetch(query));
    ASSERT_EQ(PQntuples(res.get()), 2);
    ASSERT_EQ(PQnfields(res.get()), 1);
    std::array<int, 2> values = {
      ASSERT_RESULT(GetInt32(res.get(), 0, 0)),
      ASSERT_RESULT(GetInt32(res.get(), 1, 0)),
    };
    ASSERT_EQ(values[0], 4);
    ASSERT_EQ(values[1], 3);
  }
}

// Make sure that expression indexes work for index backfill.
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(BackfillExpression),
          PgLibPqTestIndexBackfill) {
  constexpr int kNumRows = 9;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0 ((j % i))", kTableName));

  // Index scan to verify contents of index table.
  const std::string query = Format(
      "SELECT j, i, j % i as mod FROM $0 WHERE j % i = 2 ORDER BY i",
      kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  auto res = ASSERT_RESULT(conn.Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 2);
  ASSERT_EQ(PQnfields(res.get()), 3);
  std::array<std::array<int, 3>, 2> values = {{
    {
      ASSERT_RESULT(GetInt32(res.get(), 0, 0)),
      ASSERT_RESULT(GetInt32(res.get(), 0, 1)),
      ASSERT_RESULT(GetInt32(res.get(), 0, 2)),
    },
    {
      ASSERT_RESULT(GetInt32(res.get(), 1, 0)),
      ASSERT_RESULT(GetInt32(res.get(), 1, 1)),
      ASSERT_RESULT(GetInt32(res.get(), 1, 2)),
    },
  }};
  ASSERT_EQ(values[0][0], 14);
  ASSERT_EQ(values[0][1], 4);
  ASSERT_EQ(values[0][2], 2);
  ASSERT_EQ(values[1][0], 18);
  ASSERT_EQ(values[1][1], 8);
  ASSERT_EQ(values[1][2], 2);
}

// Make sure that unique indexes work when index backfill is enabled (skips backfill for now)
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(BackfillUnique),
          PgLibPqTestIndexBackfill) {
  constexpr int kNumRows = 3;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));
  // Add row that would make j not unique.
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (99, 11)",
      kTableName,
      kNumRows));

  // Create unique index without failure.
  ASSERT_OK(conn.ExecuteFormat("CREATE UNIQUE INDEX ON $0 (i ASC)", kTableName));
  // Index scan to verify contents of index table.
  const std::string query = Format(
      "SELECT * FROM $0 ORDER BY i",
      kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  auto res = ASSERT_RESULT(conn.Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 4);
  ASSERT_EQ(PQnfields(res.get()), 2);

  // Create unique index with failure.
  Status status = conn.ExecuteFormat("CREATE UNIQUE INDEX ON $0 (j ASC)", kTableName);
  ASSERT_NOK(status);
  auto msg = status.message().ToBuffer();
  ASSERT_TRUE(msg.find("duplicate key value") != std::string::npos) << status;
}

// Make sure that indexes created in postgres nested DDL work and skip backfill (optimization).
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(BackfillNestedDdl),
          PgLibPqTestIndexBackfill) {
  constexpr int kNumRows = 3;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int, UNIQUE (j))", kTableName));

  // Make sure that the index create was not multi-stage.
  std::string table_id =
      ASSERT_RESULT(GetTableIdByTableName(client.get(), kNamespaceName, kTableName));
  std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
  Synchronizer sync;
  ASSERT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
  ASSERT_OK(sync.Wait());
  ASSERT_EQ(table_info->schema.version(), 1);

  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));

  // Add row that violates unique constraint on j.
  Status status = conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (99, 11)",
      kTableName,
      kNumRows);
  ASSERT_NOK(status);
  auto msg = status.message().ToBuffer();
  ASSERT_TRUE(msg.find("duplicate key value") != std::string::npos) << status;
}

// Make sure that drop index works when index backfill is enabled (skips online schema migration for
// now)
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(BackfillDrop),
          PgLibPqTestIndexBackfill) {
  constexpr int kNumRows = 5;
  const std::string kNamespaceName = "yugabyte";
  const std::string kIndexName = "i";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));

  // Create index.
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i ASC)", kIndexName, kTableName));

  // Drop index.
  ASSERT_OK(conn.ExecuteFormat("DROP INDEX $0", kIndexName));

  // Ensure index is not used for scan.
  const std::string query = Format(
      "SELECT * FROM $0 ORDER BY i",
      kTableName);
  ASSERT_FALSE(ASSERT_RESULT(conn.HasIndexScan(query)));
}

// Make sure deletes to nonexistent rows look like noops to clients.  This may seem too obvious to
// necessitate a test, but logic for backfill is special in that it wants nonexistent index deletes
// to be applied for the backfill process to use them.  This test guards against that logic being
// implemented incorrectly.
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(BackfillNonexistentDelete),
          PgLibPqTestIndexBackfill) {
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int PRIMARY KEY)", kTableName));

  // Delete to nonexistent row should return no rows.
  auto res = ASSERT_RESULT(conn.FetchFormat("DELETE FROM $0 WHERE i = 1 RETURNING i", kTableName));
  ASSERT_EQ(PQntuples(res.get()), 0);
  ASSERT_EQ(PQnfields(res.get()), 1);
}

// Make sure that index backfill on large tables backfills all data.
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(BackfillLarge),
          PgLibPqTestIndexBackfill) {
  constexpr int kNumRows = 10000;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  // Insert bunch of rows.
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1))",
      kTableName,
      kNumRows));

  // Create index.
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0 (i ASC)", kTableName));

  // All rows should be in the index.
  const std::string query = Format(
      "SELECT COUNT(*) FROM $0 WHERE i > 0",
      kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  auto res = ASSERT_RESULT(conn.Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 1);
  ASSERT_EQ(PQnfields(res.get()), 1);
  int actual_num_rows = ASSERT_RESULT(GetInt64(res.get(), 0, 0));
  ASSERT_EQ(actual_num_rows, kNumRows);
}

// Override the index backfill test to disable transparent retries on cache version mismatch.
class PgLibPqTestIndexBackfillNoRetry : public PgLibPqTestIndexBackfill {
 public:
  PgLibPqTestIndexBackfillNoRetry() {
    more_tserver_flags.push_back("--TEST_ysql_disable_transparent_cache_refresh_retry=true");
  }
};

TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(BackfillDropNoRetry),
          PgLibPqTestIndexBackfillNoRetry) {
  constexpr int kNumRows = 5;
  const std::string kNamespaceName = "yugabyte";
  const std::string kIndexName = "i";
  const std::string kTableName = "t";

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));

  // Create index.
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i ASC)", kIndexName, kTableName));

  // Update the table cache entry for the indexed table.
  ASSERT_OK(conn.FetchFormat("SELECT * FROM $0", kTableName));

  // Drop index.
  ASSERT_OK(conn.ExecuteFormat("DROP INDEX $0", kIndexName));

  // Ensure that there is no schema version mismatch for the indexed table.  This is because the
  // above `DROP INDEX` should have invalidated the corresponding table cache entry.  (There also
  // should be no catalog version mismatch because it is updated for the same session after DDL.)
  ASSERT_OK(conn.FetchFormat("SELECT * FROM $0", kTableName));
}

// Override the index backfill test to have slower backfill-related operations
class PgLibPqTestIndexBackfillSlow : public PgLibPqTestIndexBackfill {
 public:
  PgLibPqTestIndexBackfillSlow() {
    more_master_flags.push_back("--TEST_slowdown_backfill_alter_table_rpcs_ms=7000");
    more_tserver_flags.push_back("--TEST_slowdown_backfill_by_ms=7000");
  }
};

// Make sure that read time (and write time) for backfill works.  Simulate this situation:
//   Session A                                    Session B
//   --------------------------                   ---------------------------------
//   CREATE INDEX
//   - DELETE_ONLY perm
//   - WRITE_DELETE perm
//   - BACKFILL perm
//     - get safe time for read
//                                                UPDATE a row of the indexed table
//     - do the actual backfill
//   - READ_WRITE_DELETE perm
// The backfill should use the values before update when writing to the index.  The update should
// write and delete to the index because of permissions.  Since backfill writes with an ancient
// timestamp, the update should appear to have happened after the backfill.
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(BackfillReadTime),
          PgLibPqTestIndexBackfillSlow) {
  const std::string kIndexName = "rn_idx";
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "rn";

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int, PRIMARY KEY (i ASC))", kTableName));
  for (auto pair = std::make_pair(0, 10); pair.first < 6; ++pair.first, ++pair.second) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, $2)",
                                 kTableName,
                                 pair.first,
                                 pair.second));
  }

  std::vector<std::thread> threads;
  threads.emplace_back([&] {
    auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (j ASC)", kIndexName, kTableName));
    {
      // Index scan to verify contents of index table.
      const std::string query = Format("SELECT * FROM $0 WHERE j = 113", kTableName);
      ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
      auto res = ASSERT_RESULT(conn.Fetch(query));
      auto lines = PQntuples(res.get());
      ASSERT_EQ(1, lines);
      auto columns = PQnfields(res.get());
      ASSERT_EQ(2, columns);
      auto key = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
      ASSERT_EQ(key, 3);
      // Make sure that the update is visible.
      auto value = ASSERT_RESULT(GetInt32(res.get(), 0, 1));
      ASSERT_EQ(value, 113);
    }
  });
  threads.emplace_back([&] {
    // Sleep to avoid querying for index too early.
    std::this_thread::sleep_for(7s * 2);

    std::string table_id =
        ASSERT_RESULT(GetTableIdByTableName(client.get(), kNamespaceName, kTableName));
    std::string index_id =
        ASSERT_RESULT(GetTableIdByTableName(client.get(), kNamespaceName, kIndexName));

    // Wait for backfill stage.
    {
      IndexPermissions actual_permissions =
          ASSERT_RESULT(client->WaitUntilIndexPermissionsAtLeast(
            table_id,
            index_id,
            IndexPermissions::INDEX_PERM_DO_BACKFILL));
      ASSERT_LE(actual_permissions, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE)
          << "index creation failed";
      ASSERT_NE(actual_permissions, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE)
          << "index finished backfilling too quickly";
    }

    // Give the backfill stage enough time to get a read time.
    // TODO(jason): come up with some way to wait until the read time is chosen rather than relying
    // on a brittle sleep.
    std::this_thread::sleep_for(5s);

    auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
    ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET j = j + 100 WHERE i = 3", kTableName));

    // It should still be in the backfill stage, hopefully before the actual backfill started.
    {
      IndexPermissions actual_permissions =
          ASSERT_RESULT(client->GetIndexPermissions(
            table_id,
            index_id));
      ASSERT_EQ(
          actual_permissions,
          IndexPermissions::INDEX_PERM_DO_BACKFILL);
    }
  });

  for (auto& thread : threads) {
    thread.join();
  }
}

// Make sure that updates at each stage of multi-stage index create work.  Simulate this situation:
//   Session A                                    Session B
//   --------------------------                   ---------------------------------
//   CREATE INDEX
//   - DELETE_ONLY perm
//                                                UPDATE a row of the indexed table
//   - WRITE_DELETE perm
//                                                UPDATE a row of the indexed table
//   - BACKFILL perm
//                                                UPDATE a row of the indexed table
//   - READ_WRITE_DELETE perm
//                                                UPDATE a row of the indexed table
// Updates should succeed and get written to the index.
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(BackfillPermissions),
          PgLibPqTestIndexBackfillSlow) {
  const auto kGetTableIdWaitTime = 10s;
  const auto kThreadWaitTime = 60s;
  const std::array<std::pair<IndexPermissions, int>, 4> permission_key_pairs = {
    std::make_pair(IndexPermissions::INDEX_PERM_DELETE_ONLY, 2),
    std::make_pair(IndexPermissions::INDEX_PERM_WRITE_AND_DELETE, 3),
    std::make_pair(IndexPermissions::INDEX_PERM_DO_BACKFILL, 4),
    std::make_pair(IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE, 5),
  };
  const std::string kIndexName = "rn_idx";
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "rn";

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (i int, j int, PRIMARY KEY (i ASC))", kTableName));
  for (auto pair = std::make_pair(0, 10); pair.first < 6; ++pair.first, ++pair.second) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, $2)",
                                 kTableName,
                                 pair.first,
                                 pair.second));
  }

  auto wait_for_perm =
      [&](const TableId& table_id,
          const TableId &index_id,
          const IndexPermissions& target_permission) -> Status {
        IndexPermissions actual_permission =
            VERIFY_RESULT(client->WaitUntilIndexPermissionsAtLeast(
              table_id,
              index_id,
              target_permission));
        if (actual_permission > target_permission) {
          return STATUS(RuntimeError, "Exceeded target permission");
        } else {
          return Status::OK();
        }
      };
  auto assert_perm =
      [&](const TableId& table_id,
          const TableId &index_id,
          const IndexPermissions& target_permission) {
        IndexPermissions actual_permission =
            ASSERT_RESULT(client->GetIndexPermissions(
              table_id,
              index_id));
        ASSERT_EQ(
            actual_permission,
            target_permission);
      };

  std::atomic<int> updates(0);
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([&] {
    auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (j ASC)", kIndexName, kTableName));
  });
  thread_holder.AddThreadFunctor([&] {
    // Wait to avoid querying for index too early.
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          if (GetTableIdByTableName(client.get(), kNamespaceName, kIndexName).ok()) {
            return true;
          } else {
            return false;
          }
        },
        kGetTableIdWaitTime,
        "Wait to get index table id by name"));

    std::string table_id =
        ASSERT_RESULT(GetTableIdByTableName(client.get(), kNamespaceName, kTableName));
    std::string index_id =
        ASSERT_RESULT(GetTableIdByTableName(client.get(), kNamespaceName, kIndexName));

    for (auto pair : permission_key_pairs) {
      IndexPermissions permission = pair.first;
      int key = pair.second;

      ASSERT_OK(wait_for_perm(table_id, index_id, permission));

      // Create a new connection every loop iteration to avoid stale table cache issues.
      // TODO(jason): no longer create new connections after closing issue #4828 (move this outside
      // the loop).
      auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
      LOG(INFO) << "running UPDATE on i = " << key;
      ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET j = j + 100 WHERE i = $1", kTableName, key));

      assert_perm(table_id, index_id, permission);
      updates++;
    }
  });

  thread_holder.WaitAndStop(kThreadWaitTime);

  ASSERT_EQ(updates.load(std::memory_order_acquire), permission_key_pairs.size());

  for (auto pair : permission_key_pairs) {
    int expected_key = pair.second;

    // Verify contents of index table.
    const std::string query = Format(
        "WITH j_idx AS (SELECT * FROM $0 ORDER BY j) SELECT j FROM j_idx WHERE i = $1",
        kTableName,
        expected_key);
    ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
    auto res = ASSERT_RESULT(conn.Fetch(query));
    int lines = PQntuples(res.get());
    ASSERT_EQ(1, lines);
    int columns = PQnfields(res.get());
    ASSERT_EQ(1, columns);
    // Make sure that the update is visible.
    int value = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
    ASSERT_EQ(value, expected_key + 110);
  }
}

// Override the base test to start a cluster with transparent retries on cache version mismatch
// disabled.
class PgLibPqTestNoRetry : public PgLibPqTest {
 public:
  PgLibPqTestNoRetry() {
    more_tserver_flags.push_back("--TEST_ysql_disable_transparent_cache_refresh_retry=true");
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.insert(
        std::end(options->extra_master_flags),
        std::begin(more_master_flags),
        std::end(more_master_flags));
    options->extra_tserver_flags.insert(
        std::end(options->extra_tserver_flags),
        std::begin(more_tserver_flags),
        std::end(more_tserver_flags));
  }

 protected:
  std::vector<std::string> more_master_flags;
  std::vector<std::string> more_tserver_flags;
};

// This test is like "TestPgCacheConsistency#testVersionMismatchWithFailedRetry".  That one gets
// failures because the queries are "parse" message types, and we don't consider retry for those.
// These queries are "simple query" message types, so they should be considered for transparent
// retry.  The last factor is whether `--TEST_ysql_disable_transparent_cache_refresh_retry` is
// specified.
void PgLibPqTest::TestCacheRefreshRetry(const bool is_retry_disabled) {
  constexpr int kNumTries = 5;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";
  int num_successes = 0;
  std::array<PGConn, 2> conns = {
    ASSERT_RESULT(ConnectToDB(kNamespaceName)),
    ASSERT_RESULT(ConnectToDB(kNamespaceName)),
  };

  ASSERT_OK(conns[0].ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  // Make the catalog version cache up to date.
  ASSERT_OK(conns[1].FetchFormat("SELECT * FROM $0", kTableName));

  for (int i = 0; i < kNumTries; ++i) {
    ASSERT_OK(conns[0].ExecuteFormat("ALTER TABLE $0 ADD COLUMN j$1 int", kTableName, i));
    auto res = conns[1].FetchFormat("SELECT * FROM $0", kTableName);
    if (is_retry_disabled) {
      // Ensure that we fall under one of two cases:
      // 1. tserver gets updated catalog version before SELECT (rare)
      //    - YBCheckSharedCatalogCacheVersion causes YBRefreshCache
      //    - trying the SELECT requires getting the table schema, but it will be a cache miss since
      //      the whole cache was invalidated, so we get the up-to-date table schema and succeed
      // 2. tserver doesn't get updated catalog version before SELECT (common)
      //    - trying the SELECT causes catalog version mismatch
      if (res.ok()) {
        LOG(WARNING) << "SELECT was ok";
        num_successes++;
        continue;
      }
      auto msg = res.status().message().ToBuffer();
      ASSERT_TRUE(msg.find("Catalog Version Mismatch") != std::string::npos) << res.status();
    } else {
      // Ensure that the request is successful (thanks to retry).
      if (!res.ok()) {
        LOG(WARNING) << "SELECT was not ok: " << res.status();
        continue;
      }
      num_successes++;
    }
    // Make the catalog version cache up to date, if needed.
    ASSERT_OK(conns[1].FetchFormat("SELECT * FROM $0", kTableName));
  }

  LOG(INFO) << "number of successes: " << num_successes << "/" << kNumTries;
  if (is_retry_disabled) {
    // Expect at least half of the tries to fail with catalog version mismatch.  There can be some
    // successes because, between the ALTER and SELECT, the catalog version could have propogated
    // through shared memory (see `YBCheckSharedCatalogCacheVersion`).
    const int num_failures = kNumTries - num_successes;
    ASSERT_GE(num_failures, kNumTries / 2);
  } else {
    // Expect all the tries to succeed.  This is because it is unacceptable to fail when retries are
    // enabled.
    ASSERT_EQ(num_successes, kNumTries);
  }
}

TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(CacheRefreshRetryDisabled),
          PgLibPqTestNoRetry) {
  TestCacheRefreshRetry(true /* is_retry_disabled */);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(CacheRefreshRetryEnabled)) {
  TestCacheRefreshRetry(false /* is_retry_disabled */);
}

} // namespace pgwrapper
} // namespace yb

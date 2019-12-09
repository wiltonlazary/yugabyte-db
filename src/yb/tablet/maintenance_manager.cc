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

#include "yb/tablet/maintenance_manager.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include <gflags/gflags.h>

#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/debug/trace_logging.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/stopwatch.h"
#include "yb/util/thread.h"

using std::pair;
using std::shared_ptr;
using strings::Substitute;

DEFINE_int32(maintenance_manager_num_threads, 1,
       "Size of the maintenance manager thread pool. Beyond a value of '1', one thread is "
       "reserved for emergency flushes. For spinning disks, the number of threads should "
       "not be above the number of devices.");
TAG_FLAG(maintenance_manager_num_threads, stable);

DEFINE_int32(maintenance_manager_polling_interval_ms, 250,
       "Polling interval for the maintenance manager scheduler, "
       "in milliseconds.");
TAG_FLAG(maintenance_manager_polling_interval_ms, hidden);

DEFINE_int32(maintenance_manager_history_size, 8,
       "Number of completed operations the manager is keeping track of.");
TAG_FLAG(maintenance_manager_history_size, hidden);

DEFINE_bool(enable_maintenance_manager, true,
       "Enable the maintenance manager, runs compaction and tablet cleaning tasks.");
TAG_FLAG(enable_maintenance_manager, unsafe);

namespace yb {

using yb::tablet::MaintenanceManagerStatusPB;
using yb::tablet::MaintenanceManagerStatusPB_CompletedOpPB;
using yb::tablet::MaintenanceManagerStatusPB_MaintenanceOpPB;

MaintenanceOpStats::MaintenanceOpStats() {
  Clear();
}

void MaintenanceOpStats::Clear() {
  valid_ = false;
  runnable_ = false;
  ram_anchored_ = 0;
  logs_retained_bytes_ = 0;
  perf_improvement_ = 0;
}

MaintenanceOp::MaintenanceOp(std::string name, IOUsage io_usage)
    : name_(std::move(name)), running_(0), io_usage_(io_usage) {}

MaintenanceOp::~MaintenanceOp() {
  CHECK(!manager_.get()) << "You must unregister the " << name_
         << " Op before destroying it.";
}

void MaintenanceOp::Unregister() {
  CHECK(manager_.get()) << "Op " << name_ << " was never registered.";
  manager_->UnregisterOp(this);
}

const MaintenanceManager::Options MaintenanceManager::DEFAULT_OPTIONS = {
  0,
  0,
  0,
  shared_ptr<MemTracker>(),
};

MaintenanceManager::MaintenanceManager(const Options& options)
  : num_threads_(options.num_threads <= 0 ?
      FLAGS_maintenance_manager_num_threads : options.num_threads),
    cond_(&lock_),
    shutdown_(false),
    running_ops_(0),
    polling_interval_ms_(options.polling_interval_ms <= 0 ?
          FLAGS_maintenance_manager_polling_interval_ms :
          options.polling_interval_ms),
    completed_ops_count_(0),
    parent_mem_tracker_(!options.parent_mem_tracker ?
        MemTracker::GetRootTracker() : options.parent_mem_tracker) {
  CHECK_OK(ThreadPoolBuilder("MaintenanceMgr").set_min_threads(num_threads_)
               .set_max_threads(num_threads_).Build(&thread_pool_));
  uint32_t history_size = options.history_size == 0 ?
                          FLAGS_maintenance_manager_history_size :
                          options.history_size;
  completed_ops_.resize(history_size);
}

MaintenanceManager::~MaintenanceManager() {
  Shutdown();
}

Status MaintenanceManager::Init() {
  RETURN_NOT_OK(Thread::Create(
      "maintenance", "maintenance_scheduler",
      std::bind(&MaintenanceManager::RunSchedulerThread, this), &monitor_thread_));
  return Status::OK();
}

void MaintenanceManager::Shutdown() {
  {
    std::lock_guard<Mutex> guard(lock_);
    if (shutdown_) {
      return;
    }
    shutdown_ = true;
    cond_.Broadcast();
  }
  if (monitor_thread_.get()) {
    CHECK_OK(ThreadJoiner(monitor_thread_.get()).Join());
    monitor_thread_.reset();
    thread_pool_->Shutdown();
  }
}

void MaintenanceManager::RegisterOp(MaintenanceOp* op) {
  std::lock_guard<Mutex> guard(lock_);
  CHECK(!op->manager_.get()) << "Tried to register " << op->name()
          << ", but it was already registered.";
  pair<OpMapTy::iterator, bool> val
    (ops_.insert(OpMapTy::value_type(op, MaintenanceOpStats())));
  CHECK(val.second)
      << "Tried to register " << op->name()
      << ", but it already exists in ops_.";
  op->manager_ = shared_from_this();
  op->cond_.reset(new ConditionVariable(&lock_));
  VLOG_AND_TRACE("maintenance", 1) << "Registered " << op->name();
}

void MaintenanceManager::UnregisterOp(MaintenanceOp* op) {
  {
    std::lock_guard<Mutex> guard(lock_);
    CHECK(op->manager_.get() == this) << "Tried to unregister " << op->name()
          << ", but it is not currently registered with this maintenance manager.";
    auto iter = ops_.find(op);
    CHECK(iter != ops_.end()) << "Tried to unregister " << op->name()
        << ", but it was never registered";
    // While the op is running, wait for it to be finished.
    if (iter->first->running_ > 0) {
      VLOG_AND_TRACE("maintenance", 1) << "Waiting for op " << op->name() << " to finish so "
            << "we can unregister it.";
    }
    while (iter->first->running_ > 0) {
      op->cond_->Wait();
      iter = ops_.find(op);
      CHECK(iter != ops_.end()) << "Tried to unregister " << op->name()
          << ", but another thread unregistered it while we were "
          << "waiting for it to complete";
    }
    ops_.erase(iter);
  }
  LOG(INFO) << "Unregistered op " << op->name();
  op->cond_.reset();
  // Remove the op's shared_ptr reference to us.  This might 'delete this'.
  op->manager_.reset();
}

void MaintenanceManager::RunSchedulerThread() {
  MonoDelta polling_interval = MonoDelta::FromMilliseconds(polling_interval_ms_);

  std::unique_lock<Mutex> guard(lock_);
  while (true) {
    // Loop until we are shutting down or it is time to run another op.
    cond_.TimedWait(polling_interval);
    if (shutdown_) {
      VLOG_AND_TRACE("maintenance", 1) << "Shutting down maintenance manager.";
      return;
    }

    // Find the best op.
    MaintenanceOp* op = FindBestOp();
    if (!op) {
      VLOG_AND_TRACE("maintenance", 2) << "No maintenance operations look worth doing.";
      continue;
    }

    // Prepare the maintenance operation.
    op->running_++;
    running_ops_++;
    guard.unlock();
    bool ready = op->Prepare();
    guard.lock();
    if (!ready) {
      LOG(INFO) << "Prepare failed for " << op->name()
                << ".  Re-running scheduler.";
      op->running_--;
      op->cond_->Signal();
      continue;
    }

    // Run the maintenance operation.
    Status s = thread_pool_->SubmitFunc(std::bind(&MaintenanceManager::LaunchOp, this, op));
    CHECK(s.ok());
  }
}

// Finding the best operation goes through four filters:
// - If there's an Op that we can run quickly that frees log retention, we run it.
// - If we've hit the overall process memory limit (note: this includes memory that the Ops cannot
//   free), we run the Op with the highest RAM usage.
// - If there are Ops that retain logs, we run the one that has the highest retention (and if many
//   qualify, then we run the one that also frees up the most RAM).
// - Finally, if there's nothing else that we really need to do, we run the Op that will improve
//   performance the most.
//
// The reason it's done this way is that we want to prioritize limiting the amount of resources we
// hold on to. Low IO Ops go first since we can quickly run them, then we can look at memory usage.
// Reversing those can starve the low IO Ops when the system is under intense memory pressure.
//
// In the third priority we're at a point where nothing's urgent and there's nothing we can run
// quickly.
// TODO We currently optimize for freeing log retention but we could consider having some sort of
// sliding priority between log retention and RAM usage. For example, is an Op that frees
// 128MB of log retention and 12MB of RAM always better than an op that frees 12MB of log retention
// and 128MB of RAM? Maybe a more holistic approach would be better.
MaintenanceOp* MaintenanceManager::FindBestOp() {
  TRACE_EVENT0("maintenance", "MaintenanceManager::FindBestOp");
  if (!FLAGS_enable_maintenance_manager) {
    VLOG_AND_TRACE("maintenance", 1) << "Maintenance manager is disabled. Doing nothing";
    return nullptr;
  }
  size_t free_threads = num_threads_ - running_ops_;
  if (free_threads == 0) {
    VLOG_AND_TRACE("maintenance", 1) << "there are no free threads, so we can't run anything.";
    return nullptr;
  }

  int64_t low_io_most_logs_retained_bytes = 0;
  MaintenanceOp* low_io_most_logs_retained_bytes_op = nullptr;

  uint64_t most_mem_anchored = 0;
  MaintenanceOp* most_mem_anchored_op = nullptr;

  int64_t most_logs_retained_bytes = 0;
  int64_t most_logs_retained_bytes_ram_anchored = 0;
  MaintenanceOp* most_logs_retained_bytes_op = nullptr;

  double best_perf_improvement = 0;
  MaintenanceOp* best_perf_improvement_op = nullptr;
  for (OpMapTy::value_type &val : ops_) {
    MaintenanceOp* op(val.first);
    MaintenanceOpStats& stats(val.second);
    // Update op stats.
    stats.Clear();
    op->UpdateStats(&stats);
    if (!stats.valid() || !stats.runnable()) {
      continue;
    }
    if (stats.logs_retained_bytes() > low_io_most_logs_retained_bytes &&
        op->io_usage_ == MaintenanceOp::LOW_IO_USAGE) {
      low_io_most_logs_retained_bytes_op = op;
      low_io_most_logs_retained_bytes = stats.logs_retained_bytes();
    }

    if (stats.ram_anchored() > most_mem_anchored) {
      most_mem_anchored_op = op;
      most_mem_anchored = stats.ram_anchored();
    }
    // We prioritize ops that can free more logs, but when it's the same we pick the one that
    // also frees up the most memory.
    if (stats.logs_retained_bytes() > 0 &&
        (stats.logs_retained_bytes() > most_logs_retained_bytes ||
            (stats.logs_retained_bytes() == most_logs_retained_bytes &&
                stats.ram_anchored() > most_logs_retained_bytes_ram_anchored))) {
      most_logs_retained_bytes_op = op;
      most_logs_retained_bytes = stats.logs_retained_bytes();
      most_logs_retained_bytes_ram_anchored = stats.ram_anchored();
    }
    if ((!best_perf_improvement_op) ||
        (stats.perf_improvement() > best_perf_improvement)) {
      best_perf_improvement_op = op;
      best_perf_improvement = stats.perf_improvement();
    }
  }

  // Look at ops that we can run quickly that free up log retention.
  if (low_io_most_logs_retained_bytes_op) {
    if (low_io_most_logs_retained_bytes > 0) {
      VLOG_AND_TRACE("maintenance", 1)
                    << "Performing " << low_io_most_logs_retained_bytes_op->name() << ", "
                    << "because it can free up more logs "
                    << "at " << low_io_most_logs_retained_bytes
                    << " bytes with a low IO cost";
      return low_io_most_logs_retained_bytes_op;
    }
  }

  // Look at free memory. If it is dangerously low, we must select something
  // that frees memory-- the op with the most anchored memory.
  auto soft_limit_exceeded_result = parent_mem_tracker_->AnySoftLimitExceeded(0.0 /* score */);
  if (soft_limit_exceeded_result.exceeded) {
    if (!most_mem_anchored_op) {
      string msg = StringPrintf("we have exceeded our soft memory limit "
          "(current capacity is %.2f%%).  However, there are no ops currently "
          "runnable which would free memory.", soft_limit_exceeded_result.current_capacity_pct);
      YB_LOG_EVERY_N_SECS(INFO, 5) << msg;
      return nullptr;
    }
    VLOG_AND_TRACE("maintenance", 1) << "we have exceeded our soft memory limit "
            << "(current capacity is " << soft_limit_exceeded_result.current_capacity_pct << "%). "
            << "Running the op which anchors the most memory: " << most_mem_anchored_op->name();
    return most_mem_anchored_op;
  }

  if (most_logs_retained_bytes_op) {
    VLOG_AND_TRACE("maintenance", 1)
            << "Performing " << most_logs_retained_bytes_op->name() << ", "
            << "because it can free up more logs " << "at " << most_logs_retained_bytes
            << " bytes";
    return most_logs_retained_bytes_op;
  }

  if (best_perf_improvement_op) {
    if (best_perf_improvement > 0) {
      VLOG_AND_TRACE("maintenance", 1) << "Performing " << best_perf_improvement_op->name() << ", "
                 << "because it had the best perf_improvement score, "
                 << "at " << best_perf_improvement;
      return best_perf_improvement_op;
    }
  }
  return nullptr;
}

void MaintenanceManager::LaunchOp(MaintenanceOp* op) {
  MonoTime start_time(MonoTime::Now());
  op->RunningGauge()->Increment();
  LOG_TIMING(INFO, Substitute("running $0", op->name())) {
    TRACE_EVENT1("maintenance", "MaintenanceManager::LaunchOp",
                 "name", op->name());
    op->Perform();
  }
  op->RunningGauge()->Decrement();
  MonoTime end_time(MonoTime::Now());
  MonoDelta delta(end_time.GetDeltaSince(start_time));
  std::lock_guard<Mutex> guard(lock_);

  CompletedOp& completed_op = completed_ops_[completed_ops_count_ % completed_ops_.size()];
  completed_op.name = op->name();
  completed_op.duration = delta;
  completed_op.start_mono_time = start_time;
  completed_ops_count_++;

  op->DurationHistogram()->Increment(delta.ToMilliseconds());

  running_ops_--;
  op->running_--;
  op->cond_->Signal();
}

void MaintenanceManager::GetMaintenanceManagerStatusDump(MaintenanceManagerStatusPB* out_pb) {
  DCHECK(out_pb != nullptr);
  std::lock_guard<Mutex> guard(lock_);
  MaintenanceOp* best_op = FindBestOp();
  for (MaintenanceManager::OpMapTy::value_type& val : ops_) {
    MaintenanceManagerStatusPB_MaintenanceOpPB* op_pb = out_pb->add_registered_operations();
    MaintenanceOp* op(val.first);
    MaintenanceOpStats& stat(val.second);
    op_pb->set_name(op->name());
    op_pb->set_running(op->running());
    if (stat.valid()) {
      op_pb->set_runnable(stat.runnable());
      op_pb->set_ram_anchored_bytes(stat.ram_anchored());
      op_pb->set_logs_retained_bytes(stat.logs_retained_bytes());
      op_pb->set_perf_improvement(stat.perf_improvement());
    } else {
      op_pb->set_runnable(false);
      op_pb->set_ram_anchored_bytes(0);
      op_pb->set_logs_retained_bytes(0);
      op_pb->set_perf_improvement(0);
    }

    if (best_op == op) {
      out_pb->mutable_best_op()->CopyFrom(*op_pb);
    }
  }

  for (const CompletedOp& completed_op : completed_ops_) {
    if (!completed_op.name.empty()) {
      MaintenanceManagerStatusPB_CompletedOpPB* completed_pb = out_pb->add_completed_operations();
      completed_pb->set_name(completed_op.name);
      completed_pb->set_duration_millis(completed_op.duration.ToMilliseconds());

      MonoDelta delta(MonoTime::Now().GetDeltaSince(completed_op.start_mono_time));
      completed_pb->set_secs_since_start(delta.ToSeconds());
    }
  }
}

} // namespace yb

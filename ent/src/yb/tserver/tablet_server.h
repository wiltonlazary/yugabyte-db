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

#ifndef ENT_SRC_YB_TSERVER_TABLET_SERVER_H
#define ENT_SRC_YB_TSERVER_TABLET_SERVER_H

#include "../../../../src/yb/tserver/tablet_server.h"

namespace yb {

class UniverseKeyRegistryPB;

namespace enterprise {

class UniverseKeyManager;

}

namespace rpc {

class SecureContext;

}

namespace tserver {
namespace enterprise {

class CDCConsumer;

class TabletServer : public yb::tserver::TabletServer {
  typedef yb::tserver::TabletServer super;
 public:
  explicit TabletServer(const TabletServerOptions& opts);
  TabletServer(const TabletServer&) = delete;
  void operator=(const TabletServer&) = delete;
  ~TabletServer();

  Env* GetEnv() override;
  rocksdb::Env* GetRocksDBEnv() override;
  void Shutdown() override;

  yb::enterprise::UniverseKeyManager* GetUniverseKeyManager();
  CHECKED_STATUS SetUniverseKeyRegistry(
      const yb::UniverseKeyRegistryPB& universe_key_registry) override;
  CHECKED_STATUS SetConfigVersionAndConsumerRegistry(int32_t cluster_config_version,
      const cdc::ConsumerRegistryPB* consumer_registry);

  int32_t cluster_config_version() const override;

  CDCConsumer* GetCDCConsumer();

 protected:
  CHECKED_STATUS RegisterServices() override;
  CHECKED_STATUS SetupMessengerBuilder(rpc::MessengerBuilder* builder) override;

 private:

  CHECKED_STATUS CreateCDCConsumer() REQUIRES(cdc_consumer_mutex_);

  std::unique_ptr<rpc::SecureContext> secure_context_;
  // Object that manages the universe key registry used for encrypting and decrypting data keys.
  // Copies are given to each Env.
  std::shared_ptr<yb::enterprise::UniverseKeyManager> universe_key_manager_;
  // Encrypted env for all non-rocksdb file i/o operations.
  std::unique_ptr<yb::Env> env_;
  // Encrypted env for all rocksdb file i/o operations.
  std::unique_ptr<rocksdb::Env> rocksdb_env_;
  // CDC consumer.
  mutable std::mutex cdc_consumer_mutex_;
  std::unique_ptr<CDCConsumer> cdc_consumer_ GUARDED_BY(cdc_consumer_mutex_);
};

} // namespace enterprise
} // namespace tserver
} // namespace yb

#endif // ENT_SRC_YB_TSERVER_TABLET_SERVER_H

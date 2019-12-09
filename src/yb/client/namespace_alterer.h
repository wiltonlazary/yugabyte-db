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

#ifndef YB_CLIENT_NAMESPACE_ALTERER_H
#define YB_CLIENT_NAMESPACE_ALTERER_H

#include <string>

#include <boost/optional.hpp>

#include "yb/client/client_fwd.h"
#include "yb/util/status.h"

#include "yb/master/master.pb.h"

namespace yb {
namespace client {

class YBNamespaceAlterer {
 public:
  ~YBNamespaceAlterer();

  YBNamespaceAlterer* RenameTo(const std::string& new_name);
  YBNamespaceAlterer* SetDatabaseType(YQLDatabase type);

  CHECKED_STATUS Alter();

 private:
  friend class YBClient;

  YBNamespaceAlterer(
      YBClient* client, const std::string& namespace_name, const std::string& namespace_id);

  CHECKED_STATUS ToRequest(master::AlterNamespaceRequestPB* req);

  YBClient* const client_;
  const std::string namespace_name_;
  const std::string namespace_id_;

  Status status_;

  boost::optional<std::string> rename_to_;
  boost::optional<YQLDatabase> database_type_;

  DISALLOW_COPY_AND_ASSIGN(YBNamespaceAlterer);
};

}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_NAMESPACE_ALTERER_H

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

#ifndef YB_MASTER_CATALOG_MANAGER_UTIL_H
#define YB_MASTER_CATALOG_MANAGER_UTIL_H

#include <unordered_map>
#include <vector>

#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/util/status.h"

// Utility functions that can be shared between test and code for catalog manager.
namespace yb {
namespace master {

using ZoneToDescMap = std::unordered_map<string, TSDescriptorVector>;

class CatalogManagerUtil {
 public:
  // For the given set of descriptors, checks if the load is considered balanced across AZs in
  // multi AZ setup, else checks load distribution across tservers (single AZ).
  static CHECKED_STATUS IsLoadBalanced(const TSDescriptorVector& ts_descs);

  // For the given set of descriptors, checks if every tserver that shouldn't have leader load
  // actually has no leader load.
  static CHECKED_STATUS AreLeadersOnPreferredOnly(const TSDescriptorVector& ts_descs,
                                                  const ReplicationInfoPB& replication_info);

  // For the given set of descriptors, returns the map from each placement AZ to list of tservers
  // running in that zone.
  static CHECKED_STATUS GetPerZoneTSDesc(const TSDescriptorVector& ts_descs,
                                         ZoneToDescMap* zone_to_ts);

  // For the given placement info, checks whether a given cloud info is contained within it.
  static CHECKED_STATUS DoesPlacementInfoContainCloudInfo(const PlacementInfoPB& placement_info,
                                                          const CloudInfoPB& cloud_info);

 private:
  CatalogManagerUtil();

  DISALLOW_COPY_AND_ASSIGN(CatalogManagerUtil);
};

} // namespace master
} // namespace yb

#endif // YB_MASTER_CATALOG_MANAGER_UTIL_H

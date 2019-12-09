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

package org.yb.client;

import java.util.Comparator;
import java.util.List;
import java.util.TreeSet;

import org.yb.annotations.InterfaceAudience;
import org.yb.Common.HostPortPB;
import org.yb.master.Master;


@InterfaceAudience.Public
public class ModifyMasterClusterConfigBlacklist extends AbstractModifyMasterClusterConfig {
  private List<HostPortPB> modifyHosts;
  private boolean isAdd;
  private boolean isLeaderBlacklist;

  public ModifyMasterClusterConfigBlacklist(YBClient client, List<HostPortPB> modifyHosts,
      boolean isAdd) {
    super(client);
    this.modifyHosts = modifyHosts;
    this.isAdd = isAdd;
    this.isLeaderBlacklist = false;
  }

  public ModifyMasterClusterConfigBlacklist(YBClient client, List<HostPortPB> modifyHosts,
      boolean isAdd, boolean isLeaderBlacklist) {
    super(client);
    this.modifyHosts = modifyHosts;
    this.isAdd = isAdd;
    this.isLeaderBlacklist = isLeaderBlacklist;
  }

  @Override
  protected Master.SysClusterConfigEntryPB modifyConfig(Master.SysClusterConfigEntryPB config) {
    // Modify the blacklist.
    Master.SysClusterConfigEntryPB.Builder configBuilder =
        Master.SysClusterConfigEntryPB.newBuilder(config);

    // Use a TreeSet so we can prune duplicates while keeping HostPortPB as storage.
    TreeSet<HostPortPB> finalHosts =
      new TreeSet<HostPortPB>(new Comparator<HostPortPB>() {
      @Override
      public int compare(HostPortPB a, HostPortPB b) {
        if (a.getHost().equals(b.getHost())) {
          int portA = a.getPort();
          int portB = b.getPort();
          if (portA < portB) {
            return -1;
          } else if (portA == portB) {
            return 0;
          } else {
            return 1;
          }
        } else {
          return a.getHost().compareTo(b.getHost());
        }
      }
    });
    // Add up the current list.
    if (isLeaderBlacklist) {
      finalHosts.addAll(config.getLeaderBlacklist().getHostsList());
    } else {
      finalHosts.addAll(config.getServerBlacklist().getHostsList());
    }
    // Add or remove the given list of servers.
    if (isAdd) {
      finalHosts.addAll(modifyHosts);
    } else {
      finalHosts.removeAll(modifyHosts);
    }
    // Change the blacklist in the local config copy.
    Master.BlacklistPB blacklist =
        Master.BlacklistPB.newBuilder().addAllHosts(finalHosts).build();

    if (isLeaderBlacklist) {
      configBuilder.setLeaderBlacklist(blacklist);
    } else {
      configBuilder.setServerBlacklist(blacklist);
    }
    return configBuilder.build();
  }
}

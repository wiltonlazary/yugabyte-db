/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.LoadBalancerStateChange;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.RollingRestartParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;

import java.util.Map;
import java.util.Map.Entry;
import java.util.List;
import java.util.UUID;

import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpgradeSoftware;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpdateGFlags;

public class UpgradeKubernetesUniverse extends KubernetesTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UpgradeKubernetesUniverse.class);

  public static class Params extends RollingRestartParams {}

  @Override
  protected RollingRestartParams taskParams() {
    return (RollingRestartParams)taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      taskParams().rootCA = universe.getUniverseDetails().rootCA;

      UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
      PlacementInfo pi = universe.getUniverseDetails().getPrimaryCluster().placementInfo;

      if (taskParams().taskType == UpgradeTaskType.Software) {
        if (taskParams().ybSoftwareVersion == null ||
            taskParams().ybSoftwareVersion.isEmpty()) {
          throw new IllegalArgumentException("Invalid yugabyte software version: " +
                                             taskParams().ybSoftwareVersion);
        }
        if (taskParams().ybSoftwareVersion.equals(userIntent.ybSoftwareVersion)) {
          throw new IllegalArgumentException("Cluster is already on yugabyte software version: " +
                                             taskParams().ybSoftwareVersion);
        }
      }

      switch (taskParams().taskType) {
        case Software:
          LOG.info("Upgrading software version to {} in universe {}",
                   taskParams().ybSoftwareVersion, universe.name);

          createUpgradeTask(userIntent, universe, pi);

          createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());
          break;
        case GFlags:
          LOG.info("Upgrading GFlags in universe {}", universe.name);
          updateGFlagsPersistTasks(taskParams().masterGFlags, taskParams().tserverGFlags)
              .setSubTaskGroupType(getTaskSubGroupType());

          createUpgradeTask(userIntent, universe, pi);
          break;
      }

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error={}.", getName(), t);

      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);
      // If the task failed, we don't want the loadbalancer to be disabled,
      // so we enable it again in case of errors.
      createLoadBalancerStateChangeTask(true /*enable*/)
          .setSubTaskGroupType(getTaskSubGroupType());

      subTaskGroupQueue.run();

      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }

  private SubTaskGroupType getTaskSubGroupType() {
    switch (taskParams().taskType) {
      case Software:
        return SubTaskGroupType.UpgradingSoftware;
      case GFlags:
        return SubTaskGroupType.UpdatingGFlags;
      default:
        return SubTaskGroupType.Invalid;
    }
  }

  private void createUpgradeTask(UserIntent userIntent, Universe universe, PlacementInfo pi) {
    String version = null;
    boolean flag = true;
    if (taskParams().taskType == UpgradeTaskType.Software) {
      version = taskParams().ybSoftwareVersion;
      flag = false;
    }
    
    createSingleKubernetesExecutorTask(CommandType.POD_INFO, pi);
    
    KubernetesPlacement placement = new KubernetesPlacement(pi);

    Provider provider = Provider.get(UUID.fromString(
          taskParams().getPrimaryCluster().userIntent.provider));

    String masterAddresses = PlacementInfoUtil.computeMasterAddresses(pi, placement.masters,
        taskParams().nodePrefix, provider);
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);

    createLoadBalancerStateChangeTask(false /*enable*/)
        .setSubTaskGroupType(getTaskSubGroupType());

    if (!taskParams().masterGFlags.isEmpty() || !flag) {
      userIntent.masterGFlags = taskParams().masterGFlags;
      upgradePodsTask(placement, masterAddresses, null, ServerType.MASTER,
                      version, taskParams().sleepAfterMasterRestartMillis);
    }
    if (!taskParams().tserverGFlags.isEmpty() || !flag) {
      userIntent.tserverGFlags = taskParams().tserverGFlags;
      upgradePodsTask(placement, masterAddresses, null, ServerType.TSERVER,
                      version, taskParams().sleepAfterTServerRestartMillis);
    }

    createLoadBalancerStateChangeTask(true /*enable*/)
        .setSubTaskGroupType(getTaskSubGroupType());
  }
}

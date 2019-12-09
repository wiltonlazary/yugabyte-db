// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

import com.google.common.collect.ImmutableSet;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod.CommandType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;
import org.apache.commons.lang3.StringUtils;
import com.yugabyte.yw.common.NodeManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import play.Configuration;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleUpdateNodeInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstanceActions;
import com.yugabyte.yw.commissioner.tasks.subtasks.EnableEncryptionAtRest;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForMasterLeader;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForTServerHeartBeats;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;

/**
 * Abstract base class for all tasks that create/edit the universe definition. These include the
 * create universe task and all forms of edit universe tasks. Note that the delete universe task
 * extends the UniverseTaskBase, as it does not depend on the universe definition.
 */
public abstract class UniverseDefinitionTaskBase extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseDefinitionTaskBase.class);

  // Enum for specifying the universe operation type.
  public enum UniverseOpType {
    CREATE,
    EDIT
  }

  // Enum for specifying the server type.
  public enum ServerType {
    MASTER,
    TSERVER,
    // TODO: Replace all YQLServer with YCQLserver
    YQLSERVER,
    YSQLSERVER,
    REDISSERVER,
    EITHER
  }

  private Configuration appConfig;

  // Constants needed for parsing a templated node name tag (for AWS).
  public static final String NODE_NAME_KEY = "Name";
  private static class TemplatedTags {
    private static final String DOLLAR = "$";
    private static final String LBRACE = "{";
    private static final String PREFIX = DOLLAR + LBRACE;
    private static final int PREFIX_LEN = PREFIX.length();
    private static final String SUFFIX = "}";
    private static final int SUFFIX_LEN = SUFFIX.length();
    private static final String UNIVERSE = PREFIX + "universe" + SUFFIX;
    private static final String INSTANCE_ID = PREFIX + "instance-id" + SUFFIX;
    private static final String ZONE = PREFIX + "zone" + SUFFIX;
    private static final String REGION = PREFIX + "region" + SUFFIX;
    private static final Set<String> RESERVED_TAGS =
        ImmutableSet.of(UNIVERSE.substring(PREFIX_LEN, UNIVERSE.length() - SUFFIX_LEN),
                        ZONE.substring(PREFIX_LEN, ZONE.length() - SUFFIX_LEN),
                        REGION.substring(PREFIX_LEN, REGION.length() - SUFFIX_LEN),
                        INSTANCE_ID.substring(PREFIX_LEN, INSTANCE_ID.length() - SUFFIX_LEN));
  }

  // The task params.
  @Override
  protected UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  /**
   * Writes the user intent to the universe.
   */
  public Universe writeUserIntentToUniverse() {
    return writeUserIntentToUniverse(false);
  }

  /**
   * Writes the user intent to the universe.
   * @param isReadOnlyCreate only readonly cluster being created info needs peristence.
   */
  public Universe writeUserIntentToUniverse(boolean isReadOnlyCreate) {
    // Create the update lambda.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        // Persist the updated information about the universe.
        // It should have been marked as being edited in lockUniverseForUpdate().
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        if (!universeDetails.updateInProgress) {
          String msg = "Universe " + taskParams().universeUUID +
                       " has not been marked as being updated.";
          LOG.error(msg);
          throw new RuntimeException(msg);
        }
        if (!isReadOnlyCreate) {
          universeDetails.nodeDetailsSet = taskParams().nodeDetailsSet;
          universeDetails.nodePrefix = taskParams().nodePrefix;
          universeDetails.universeUUID = taskParams().universeUUID;
          universeDetails.rootCA = taskParams().rootCA;
          universeDetails.allowInsecure = taskParams().allowInsecure;
          Cluster cluster = taskParams().getPrimaryCluster();
          if (cluster != null) {
            universeDetails.upsertPrimaryCluster(cluster.userIntent, cluster.placementInfo);
          } // else read only cluster edit mode.
        } else {
          // Combine the existing nodes with new read only cluster nodes.
          universeDetails.nodeDetailsSet.addAll(taskParams().nodeDetailsSet);
        }
        taskParams().getReadOnlyClusters().stream().forEach((async) -> {
          universeDetails.upsertCluster(async.userIntent, async.placementInfo, async.uuid);
        });
        universe.setUniverseDetails(universeDetails);
      }
    };
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    Universe universe = Universe.saveDetails(taskParams().universeUUID, updater);
    LOG.debug("Wrote user intent for universe {}.", taskParams().universeUUID);

    updateOnPremNodeUuids(universe);

    // Return the universe object that we have already updated.
    return universe;
  }

  /**
   * Delete a cluster from the universe.
   * @param clusterUUID uuid of the cluster user wants to delete.
   */
  public void deleteClusterFromUniverse(UUID clusterUUID) {
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        universeDetails.deleteCluster(clusterUUID);
        universe.setUniverseDetails(universeDetails);
      }
    };
    Universe.saveDetails(taskParams().universeUUID, updater);
    LOG.info("Delete cluster {} done.", clusterUUID);
  }

  // Helper data structure to save the new name and index of nodes for quick lookup using the
  // old name of nodes.
  private class NameAndIndex {
    String name;
    int index;

    public NameAndIndex(String name, int index) {
      this.name = name;
      this.index = index;
    }
    
    public String toString() {
      return "{name: " + name + ", index: " + index + "}";
    }
  }

  // Check allowed patterns for tagValue.
  public static void checkTagPattern(String tagValue) {
    if (tagValue == null || tagValue.isEmpty()) {
      throw new IllegalArgumentException("Invalid value '" + tagValue + "' for " + NODE_NAME_KEY);
    }

    int numPrefix = StringUtils.countMatches(tagValue, TemplatedTags.PREFIX);
    int numSuffix = StringUtils.countMatches(tagValue, TemplatedTags.SUFFIX);
    if (numPrefix != numSuffix) {
      throw new IllegalArgumentException("Number of '" + TemplatedTags.PREFIX + "' does not " +
                                         "match '" + TemplatedTags.SUFFIX + "' count in " + tagValue);
    }

    // Find all the content repeated within all the "{" and "}". These will be matched againt
    // supported keywords for tags.
    Pattern pattern =  Pattern.compile("\\" + TemplatedTags.DOLLAR + "\\" + TemplatedTags.LBRACE +
                                       "(.*?)\\" + TemplatedTags.SUFFIX);
    Matcher matcher = pattern.matcher(tagValue);
    Set<String> keys = new HashSet<String>();
    while (matcher.find()) {
      String match = matcher.group(1);
      if (keys.contains(match)) {
        throw new IllegalArgumentException("Duplicate " + match + " in " + NODE_NAME_KEY + " tag.");
      }
      if (!TemplatedTags.RESERVED_TAGS.contains(match)) {
        throw new IllegalArgumentException("Invalid variable " + match + " in " + NODE_NAME_KEY +
                                           " tag. Should be one of " + TemplatedTags.RESERVED_TAGS);
      }
      keys.add(match);
    }
    LOG.debug("Found tags keys : " + keys);

    if (!tagValue.contains(TemplatedTags.INSTANCE_ID)) {
      throw new IllegalArgumentException("'"+ TemplatedTags.INSTANCE_ID + "' should be part of " +
                                         NODE_NAME_KEY + " value " + tagValue);
    }
  }

  private static String getTagBasedName(String tagValue, Cluster cluster, int nodeIdx,
      String region, String az) {
    return tagValue.replace(TemplatedTags.UNIVERSE, cluster.userIntent.universeName)
                   .replace(TemplatedTags.INSTANCE_ID, Integer.toString(nodeIdx))
                   .replace(TemplatedTags.ZONE, az)
                   .replace(TemplatedTags.REGION, region);
  }

  /**
   * Method to derive the expected node name from the input parameters.
   *
   * @param cluster         The cluster containing the node.
   * @param tagValue        Templated name tag to use to derive the final node name.
   * @param prefix          Name prefix if not templated.
   * @param nodeIdx         index to be used in node name.
   * @param region          region in which this node is present.
   * @param az              zone in which this node is present.
   * @return a string which can be used as the node name.
   */
  public static String getNodeName(Cluster cluster, String tagValue, String prefix, int nodeIdx,
      String region, String az) {
    if (!tagValue.isEmpty()) {
      checkTagPattern(tagValue);
    }

    String newName = "";
    if (cluster.clusterType == ClusterType.ASYNC) {
      if (tagValue.isEmpty()) {
        newName = prefix + Universe.READONLY + cluster.index + Universe.NODEIDX_PREFIX + nodeIdx;
      } else {
        newName = getTagBasedName(tagValue, cluster, nodeIdx, region, az) +
                  Universe.READONLY + cluster.index;
      }
    } else {
      if (tagValue.isEmpty()) {
        newName = prefix + Universe.NODEIDX_PREFIX + nodeIdx;
      } else {
        newName = getTagBasedName(tagValue, cluster, nodeIdx, region, az);
      }
    }

    LOG.info("Node name " + newName + " at index " + nodeIdx);

    return newName;
  }

  // Set the universes' node prefix for universe creation op. And node names/indices of all the
  // being added nodes.
  public void setNodeNames(UniverseOpType opType, Universe universe) {
    if (universe == null) {
      throw new IllegalArgumentException("Invalid universe to update node names.");
    }

    PlacementInfoUtil.populateClusterIndices(taskParams());

    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      // Can be here for ReadReplica cluster dynamic create or edit.
      primaryCluster = universe.getUniverseDetails().getPrimaryCluster();

      if (primaryCluster == null) {
        throw new IllegalStateException("Primary cluster not found in task nor universe {} " +
                                        universe.universeUUID);
      }
    }

    String nameTagValue = "";
    Map<String, String> useTags = primaryCluster.userIntent.instanceTags;
    if (useTags.containsKey(NODE_NAME_KEY)) {
      nameTagValue = useTags.get(NODE_NAME_KEY);
    }

    for (Cluster cluster : taskParams().clusters) {
      Set<NodeDetails> nodesInCluster = taskParams().getNodesInCluster(cluster.uuid);
      int startIndex = PlacementInfoUtil.getStartIndex(
                           universe.getUniverseDetails().getNodesInCluster(cluster.uuid));
      int iter = 0;
      boolean isYSQL = universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL;
      for (NodeDetails node : nodesInCluster) {
        if (node.state == NodeDetails.NodeState.ToBeAdded) {
          if (node.nodeName != null) {
            throw new IllegalStateException("Node name " + node.nodeName + " cannot be preset.");
          }
          node.nodeIdx = startIndex + iter;
          node.nodeName = getNodeName(cluster, nameTagValue, taskParams().nodePrefix, node.nodeIdx,
                                      node.cloudInfo.region, node.cloudInfo.az);
          iter++;
        }
        node.isYsqlServer = isYSQL;
      }

    }

    PlacementInfoUtil.ensureUniqueNodeNames(taskParams().nodeDetailsSet);
  }

  public void updateOnPremNodeUuids(Universe universe) {
    LOG.debug("Update on prem nodes in universe {}.", taskParams().universeUUID);

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

    List<Cluster> onPremClusters = universeDetails.clusters.stream()
            .filter(c -> c.userIntent.providerType.equals(CloudType.onprem))
            .collect(Collectors.toList());
    for (Cluster onPremCluster : onPremClusters) {
      Map<UUID, List<String>> onpremAzToNodes = new HashMap<UUID, List<String>>();
      for (NodeDetails node : universeDetails.getNodesInCluster(onPremCluster.uuid)) {
        if (node.state == NodeDetails.NodeState.ToBeAdded) {
          List<String> nodeNames = onpremAzToNodes.getOrDefault(node.azUuid, new ArrayList<String>());
          nodeNames.add(node.nodeName);
          onpremAzToNodes.put(node.azUuid, nodeNames);
        }
      }
      // Update in-memory map.
      String instanceType = onPremCluster.userIntent.instanceType;
      Map<String, NodeInstance> nodeMap = NodeInstance.pickNodes(onpremAzToNodes, instanceType);
      for (NodeDetails node : taskParams().nodeDetailsSet) {
        // TODO: use the UUID to select the node, but this requires a refactor of the tasks/params
        // to more easily trickle down this uuid into all locations.
        NodeInstance n = nodeMap.get(node.nodeName);
        if (n != null) {
          node.nodeUuid = n.nodeUuid;
        }
      }
    }
  }

  public void selectMasters() {
    UniverseDefinitionTaskParams.Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      return;
    }
    Set<NodeDetails> primaryNodes = taskParams().getNodesInCluster(primaryCluster.uuid);
    LOG.info("Current active master count = " + PlacementInfoUtil.getNumActiveMasters(primaryNodes));
    int numMastersToChoose = primaryCluster.userIntent.replicationFactor -
                             PlacementInfoUtil.getNumActiveMasters(primaryNodes);
    if (numMastersToChoose > 0) {
      PlacementInfoUtil.selectMasters(primaryNodes, numMastersToChoose);
    }
  }

  /**
  * Get the number of masters to be placed in the availability zones.
  *
  * @param pi : the placement info in which the masters need to be placed.
  */
  public void selectNumMastersAZ(PlacementInfo pi) {
    UserIntent userIntent = taskParams().getPrimaryCluster().userIntent;
    int numTotalMasters = userIntent.replicationFactor;
    PlacementInfoUtil.selectNumMastersAZ(pi, numTotalMasters);
  }

  public void createGFlagsOverrideTasks(Collection<NodeDetails> nodes, ServerType taskType) {
    // Skip if no extra flags for MASTER in primary cluster.
    if (taskType.equals(ServerType.MASTER) &&
        (taskParams().getPrimaryCluster() == null ||
         taskParams().getPrimaryCluster().userIntent.masterGFlags.isEmpty())) {
      return;
    }

    // Skip if all clusters have no extra TSERVER flags. (No cluster has an extra TSERVER flag.)
    if (taskType.equals(ServerType.TSERVER) &&
        taskParams().clusters.stream().allMatch(c -> c.userIntent.tserverGFlags.isEmpty())) {
      return;
    }

    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleConfigureServersGFlags", executor);
    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      Map<String, String> gflags = taskType.equals(ServerType.MASTER) ? userIntent.masterGFlags
                                                                      : userIntent.tserverGFlags;
      if (gflags == null || gflags.isEmpty()) {
        continue;
      }

      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Add task type
      params.type = UpgradeUniverse.UpgradeTaskType.GFlags;
      params.setProperty("processType", taskType.toString());
      params.gflags = gflags;
      AnsibleConfigureServers task = new AnsibleConfigureServers();
      task.initialize(params);
      subTaskGroup.addTask(task);
    }

    if (subTaskGroup.getNumTasks() == 0) {
      return;
    }

    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    subTaskGroupQueue.add(subTaskGroup);
  }

  /**
   * Creates a task list to update tags on the nodes.
   *
   * @param nodes : a collection of nodes that need to be updated.
   * @param deleteTags : csv version of keys of tags to be deleted, if any.
   */
  public void createUpdateInstanceTagsTasks(Collection<NodeDetails> nodes, String deleteTags) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("InstanceActions", executor);
    for (NodeDetails node : nodes) {
      InstanceActions.Params params = new InstanceActions.Params();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // Add delete tags info.
      params.deleteTags = deleteTags;
      // Create and add a task for this node.
      InstanceActions task = new InstanceActions();
      task.initialize(params);
      subTaskGroup.addTask(task);
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.Provisioning);
    subTaskGroupQueue.add(subTaskGroup);
  }

  /**
   * Creates a task list to start the tservers on the set of passed in nodes and adds it to the task
   * queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createStartTServersTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = "tserver";
      params.command = "start";
      params.placementUuid = node.placementUuid;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = new AnsibleClusterServerCtl();
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createWaitForMasterLeaderTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("WaitForMasterLeader", executor);
    WaitForMasterLeader task = new WaitForMasterLeader();
    WaitForMasterLeader.Params params = new WaitForMasterLeader.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to wait for a minimum number of tservers to heartbeat
   * to the master leader.
   */
  public SubTaskGroup createWaitForTServerHeartBeatsTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("WaitForTServerHeartBeats", executor);
    WaitForTServerHeartBeats task = new WaitForTServerHeartBeats();
    WaitForTServerHeartBeats.Params params = new WaitForTServerHeartBeats.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list for provisioning the list of nodes passed in and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createSetupServerTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleSetupServer", executor);

    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      CloudSpecificInfo cloudInfo = node.cloudInfo;
      params.deviceInfo = userIntent.deviceInfo;
      // Set the region code.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Pick one of the subnets in a round robin fashion.
      params.subnetId = cloudInfo.subnet_id;
      // Set the instance type.
      params.instanceType = cloudInfo.instance_type;
      // Set the assign public ip param.
      params.assignPublicIP = cloudInfo.assignPublicIP;
      params.useTimeSync = cloudInfo.useTimeSync;
      params.cmkArn = taskParams().cmkArn;
      // Create the Ansible task to setup the server.
      AnsibleSetupServer ansibleSetupServer = new AnsibleSetupServer();
      ansibleSetupServer.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(ansibleSetupServer);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to configure the newly provisioned nodes and adds it to the task queue.
   * Includes tasks such as setting up the 'yugabyte' user and installing the passed in software
   * package.
   *
   * @param nodes : a collection of nodes that need to be created
   * @param isMasterInShellMode : true if we are configuring a master node in shell mode
   * @return subtask group
   */
  public SubTaskGroup createConfigureServerTasks(Collection<NodeDetails> nodes,
                                                 boolean isMasterInShellMode) {
    return createConfigureServerTasks(nodes, isMasterInShellMode, false /* updateMasterAddrs */);
  }

  public SubTaskGroup createConfigureServerTasks(Collection<NodeDetails> nodes,
                                                 boolean isMasterInShellMode,
                                                 boolean updateMasterAddrsOnly) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleConfigureServers", executor);
    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Sets the isMaster field
      params.isMaster = node.isMaster;
      params.enableYSQL = userIntent.enableYSQL;
      // Set if this node is a master in shell mode.
      params.isMasterInShellMode = isMasterInShellMode;
      // The software package to install for this cluster.
      params.ybSoftwareVersion = userIntent.ybSoftwareVersion;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      params.enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
      params.enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;

      params.allowInsecure = taskParams().allowInsecure;
      params.rootCA = taskParams().rootCA;
      
      UUID custUUID = Customer.get(Universe.get(taskParams().universeUUID).customerId).uuid;

      params.callhomeLevel = CustomerConfig.getCallhomeLevel(custUUID);
      // Set if updating master addresses only.
      params.updateMasterAddrsOnly = updateMasterAddrsOnly;
      if (updateMasterAddrsOnly) {
        params.type = UpgradeTaskType.GFlags;
        params.setProperty("processType", ServerType.TSERVER.toString());
      }
      // Create the Ansible task to get the server info.
      AnsibleConfigureServers task = new AnsibleConfigureServers();
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(task);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list for fetching information about the nodes provisioned (such as the ip
   * address) and adds it to the task queue. This is specific to the cloud.
   *
   * @param nodes : a collection of nodes that need to be provisioned
   * @return subtask group
   */
  public SubTaskGroup createServerInfoTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("AnsibleUpdateNodeInfo", executor);

    for (NodeDetails node : nodes) {
      NodeTaskParams params = new NodeTaskParams();
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Create the Ansible task to get the server info.
      AnsibleUpdateNodeInfo ansibleFindCloudHost = new AnsibleUpdateNodeInfo();
      ansibleFindCloudHost.initialize(params);
      // Add it to the task list.
      subTaskGroup.addTask(ansibleFindCloudHost);
    }
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Verify that the task params are valid.
   */
  public void verifyParams(UniverseOpType opType) {
    if (taskParams().universeUUID == null) {
      throw new IllegalArgumentException(getName() + ": universeUUID not set");
    }
    if (taskParams().nodePrefix == null) {
      throw new IllegalArgumentException(getName() + ": nodePrefix not set");
    }
    if (opType == UniverseOpType.CREATE &&
        PlacementInfoUtil.getNumMasters(taskParams().nodeDetailsSet) > 0) {
      throw new IllegalStateException("Should not have any masters before create task is run.");
    }
    for (Cluster cluster : taskParams().clusters) {
      if (opType == UniverseOpType.EDIT &&
          cluster.userIntent.instanceTags.containsKey(NODE_NAME_KEY)) {
        Universe universe = Universe.get(taskParams().universeUUID);
        Cluster univCluster = universe.getUniverseDetails().getClusterByUuid(cluster.uuid);
        if (univCluster == null) {
          throw new IllegalStateException("No cluster " + cluster.uuid + " found in " +
                                          taskParams().universeUUID);
        }
        if (!univCluster.userIntent.instanceTags.get(NODE_NAME_KEY).equals(
            cluster.userIntent.instanceTags.get(NODE_NAME_KEY))) {
          throw new IllegalArgumentException("'Name' tag value cannot be changed.");
        }
      }
      PlacementInfoUtil.verifyNodesAndRF(cluster.clusterType, cluster.userIntent.numNodes,
                                         cluster.userIntent.replicationFactor);
    }
  }
}

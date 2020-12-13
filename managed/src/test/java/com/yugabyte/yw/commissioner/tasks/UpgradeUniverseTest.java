// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.runners.MockitoJUnitRunner;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.YBClient;
import org.yb.client.SetFlagResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.master.Master;
import play.libs.Json;

import java.util.Arrays;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;

import static com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType.DownloadingSoftware;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.*;


@RunWith(MockitoJUnitRunner.class)
public class UpgradeUniverseTest extends CommissionerBaseTest {
  @InjectMocks
  Commissioner commissioner;

  @InjectMocks
  UpgradeUniverse upgradeUniverse;

  YBClient mockClient;
  Universe defaultUniverse;
  ShellResponse dummyShellResponse;

  @Before
  public void setUp() {
    super.setUp();
    upgradeUniverse.setUserTaskUUID(UUID.randomUUID());
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.create(region, "az-1", "AZ 1", "subnet-1");
    // create default universe
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "old-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.uuid);
    defaultUniverse = createUniverse(defaultCustomer.getCustomerId());
    Universe.saveDetails(defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));

    // Setup mocks
    mockClient = mock(YBClient.class);
    Master.SysClusterConfigEntryPB.Builder configBuilder =
      Master.SysClusterConfigEntryPB.newBuilder().setVersion(2);
    GetMasterClusterConfigResponse mockConfigResponse =
      new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
    } catch (Exception e) {}
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockClient.waitForServer(any(HostAndPort.class), anyLong())).thenReturn(true);
    when(mockClient.getLeaderMasterHostAndPort())
            .thenReturn(HostAndPort.fromString("host-n2").withDefaultPort(11));
    IsServerReadyResponse okReadyResp = new IsServerReadyResponse(0, "", null, 0, 0);
    try {
      when(mockClient.isServerReady(any(HostAndPort.class), anyBoolean())).thenReturn(okReadyResp);
    } catch (Exception ex) {}
    dummyShellResponse =  new ShellResponse();
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
  }

  private TaskInfo submitTask(UpgradeUniverse.Params taskParams,
                              UpgradeUniverse.UpgradeTaskType taskType) {
    return submitTask(taskParams, taskType, 2);
  }

  private TaskInfo submitTask(UpgradeUniverse.Params taskParams,
                              UpgradeUniverse.UpgradeTaskType taskType,
                              int expectedVersion) {
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.taskType = taskType;
    taskParams.expectedUniverseVersion = expectedVersion;
    // Need not sleep for default 4min in tests.
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;

    try {
      UUID taskUUID = commissioner.submit(TaskType.UpgradeUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  List<String> PROPERTY_KEYS = ImmutableList.of("processType", "taskSubType");

  List<TaskType> NON_NODE_TASKS = ImmutableList.of(
      TaskType.LoadBalancerStateChange,
      TaskType.UpdateAndPersistGFlags,
      TaskType.UpdateSoftwareVersion,
      TaskType.UniverseUpdateSucceeded);

  List<TaskType> GFLAGS_UPGRADE_TASK_SEQUENCE = ImmutableList.of(
      TaskType.AnsibleConfigureServers,
      TaskType.SetNodeState,
      TaskType.AnsibleClusterServerCtl,
      TaskType.AnsibleClusterServerCtl,
      TaskType.SetNodeState,
      TaskType.WaitForServer
  );

  List<TaskType> GFLAGS_ROLLING_UPGRADE_TASK_SEQUENCE = ImmutableList.of(
      TaskType.SetNodeState,
      TaskType.AnsibleConfigureServers,
      TaskType.AnsibleClusterServerCtl,
      TaskType.AnsibleClusterServerCtl,
      TaskType.WaitForServer,
      TaskType.WaitForServerReady,
      TaskType.WaitForEncryptionKeyInMemory,
      TaskType.SetNodeState
  );

  List<TaskType> GFLAGS_NON_ROLLING_UPGRADE_TASK_SEQUENCE = ImmutableList.of(
          TaskType.AnsibleConfigureServers,
          TaskType.SetNodeState,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeState
  );

  List<TaskType> SOFTWARE_FULL_UPGRADE_TASK_SEQUENCE = ImmutableList.of(
      TaskType.SetNodeState,
      TaskType.AnsibleClusterServerCtl,
      TaskType.AnsibleConfigureServers,
      TaskType.AnsibleClusterServerCtl,
      TaskType.SetNodeState,
      TaskType.WaitForServer
  );

  List<TaskType> SOFTWARE_ROLLING_UPGRADE_TASK_SEQUENCE = ImmutableList.of(
      TaskType.SetNodeState,
      TaskType.AnsibleClusterServerCtl,
      TaskType.AnsibleConfigureServers,
      TaskType.AnsibleClusterServerCtl,
      TaskType.WaitForServer,
      TaskType.WaitForServerReady,
      TaskType.WaitForEncryptionKeyInMemory,
      TaskType.SetNodeState
  );

  List<TaskType> ROLLING_RESTART_TASK_SEQUENCE = ImmutableList.of(
      TaskType.SetNodeState,
      TaskType.AnsibleClusterServerCtl,
      TaskType.AnsibleClusterServerCtl,
      TaskType.WaitForServer,
      TaskType.WaitForServerReady,
      TaskType.WaitForEncryptionKeyInMemory,
      TaskType.SetNodeState
  );

  private int assertRollingRestartSequence(Map<Integer, List<TaskInfo>> subTasksByPosition,
                                            ServerType serverType, int startPosition) {
    int position = startPosition;
    List<TaskType> taskSequence = ROLLING_RESTART_TASK_SEQUENCE;
    // We need to check that the master leader is upgraded last.
    List<Integer> nodeOrder = serverType == MASTER ?
        Arrays.asList(1, 3, 2) : Arrays.asList(1, 2, 3);
    for (int nodeIdx : nodeOrder) {
      String nodeName = String.format("host-n%d", nodeIdx);
      for (int j = 0; j < taskSequence.size(); j++) {
        Map<String, Object> assertValues = new HashMap<String, Object>();
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        TaskType taskType = tasks.get(0).getTaskType();
        UserTaskDetails.SubTaskGroupType subTaskGroupType = tasks.get(0).getSubTaskGroupType();
        assertEquals(1, tasks.size());
        assertEquals(taskSequence.get(j), taskType);
        if (!NON_NODE_TASKS.contains(taskType)) {
          assertValues.putAll(ImmutableMap.of(
              "nodeName", nodeName, "nodeCount", 1
          ));
          assertNodeSubTask(tasks, assertValues);
        }
        position++;
      }
    }
    return position;
  }

  private int assertSoftwareUpgradeSequence(Map<Integer, List<TaskInfo>> subTasksByPosition,
                                            ServerType serverType,
                                            int startPosition, boolean isRollingUpgrade) {
    int position = startPosition;
    if (isRollingUpgrade) {
      List<TaskType> taskSequence = SOFTWARE_ROLLING_UPGRADE_TASK_SEQUENCE;
      // We need to check that the master leader is upgraded last.
      List<Integer> nodeOrder = serverType == MASTER ?
          Arrays.asList(1, 3, 2) : Arrays.asList(1, 2, 3);
      for (int nodeIdx : nodeOrder) {
        String nodeName = String.format("host-n%d", nodeIdx);
        for (int j = 0; j < taskSequence.size(); j++) {
          Map<String, Object> assertValues = new HashMap<String, Object>();
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          TaskType taskType = tasks.get(0).getTaskType();
          UserTaskDetails.SubTaskGroupType subTaskGroupType = tasks.get(0).getSubTaskGroupType();
          assertEquals(1, tasks.size());
          assertEquals(taskSequence.get(j), taskType);
          if (!NON_NODE_TASKS.contains(taskType)) {
            assertValues.putAll(ImmutableMap.of(
                "nodeName", nodeName, "nodeCount", 1
            ));

            if (taskType.equals(TaskType.AnsibleConfigureServers)) {
              String version = "new-version";
              String taskSubType =
                  subTaskGroupType.equals(DownloadingSoftware) ? "Download" :  "Install";
              assertValues.putAll(ImmutableMap.of(
                  "ybSoftwareVersion", version,
                  "processType", serverType.toString(),
                  "taskSubType", taskSubType));
            }
            assertNodeSubTask(tasks, assertValues);
          }
          position++;
        }
      }
    } else {
      for (int j = 0; j < SOFTWARE_FULL_UPGRADE_TASK_SEQUENCE.size(); j++) {
        Map<String, Object> assertValues = new HashMap<String, Object>();
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        TaskType taskType = assertTaskType(tasks, SOFTWARE_FULL_UPGRADE_TASK_SEQUENCE.get(j));

        if (NON_NODE_TASKS.contains(taskType)) {
          assertEquals(1, tasks.size());
        } else {
          assertValues.putAll(ImmutableMap.of(
              "nodeNames", (Object) ImmutableList.of("host-n1", "host-n2", "host-n3"),
              "nodeCount", 3
          ));
          if (taskType.equals(TaskType.AnsibleConfigureServers)) {
            String version = "new-version";
            assertValues.putAll(ImmutableMap.of(
                "ybSoftwareVersion", version,
                "processType", serverType.toString()));
          }
          assertEquals(3, tasks.size());
          assertNodeSubTask(tasks, assertValues);
        }
        position++;
      }
    }
    return position;
  }

  private int assertGFlagsUpgradeSequence(Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType, int startPosition, UpgradeParams.UpgradeOption option) {
    return assertGFlagsUpgradeSequence(subTasksByPosition, serverType, startPosition,
                                       option, false);
  }

  private int assertGFlagsUpgradeSequence(Map<Integer, List<TaskInfo>> subTasksByPosition,
                                          ServerType serverType, int startPosition,
                                          UpgradeParams.UpgradeOption option, boolean isEdit) {
    return assertGFlagsUpgradeSequence(subTasksByPosition, serverType, startPosition,
                                       option, isEdit, false);
  }

  private int assertGFlagsUpgradeSequence(Map<Integer, List<TaskInfo>> subTasksByPosition,
                                          ServerType serverType,
                                          int startPosition, UpgradeParams.UpgradeOption option,
                                          boolean isEdit, boolean isDelete) {
    int position = startPosition;
    switch (option) {
      case ROLLING_UPGRADE:
        List<TaskType> taskSequence = GFLAGS_ROLLING_UPGRADE_TASK_SEQUENCE;
        // We need to check that the master leader is upgraded last.
        List<Integer> nodeOrder = serverType == MASTER ?
            Arrays.asList(1, 3, 2) : Arrays.asList(1, 2, 3);
        for (int nodeIdx : nodeOrder) {
          String nodeName = String.format("host-n%d", nodeIdx);
          for (int j = 0; j < taskSequence.size(); j++) {
            Map<String, Object> assertValues = new HashMap<String, Object>();
            List<TaskInfo> tasks = subTasksByPosition.get(position);
            TaskType taskType = tasks.get(0).getTaskType();
            assertEquals(1, tasks.size());
            assertEquals(taskSequence.get(j), taskType);
            if (!NON_NODE_TASKS.contains(taskType)) {
              assertValues.putAll(ImmutableMap.of(
                      "nodeName", nodeName, "nodeCount", 1
              ));

              if (taskType.equals(TaskType.AnsibleConfigureServers)) {
                if (!isDelete) {
                  JsonNode gflagValue = serverType.equals(MASTER) ?
                          Json.parse("{\"master-flag\":" + (isEdit ? "\"m2\"}" : "\"m1\"}")) :
                          Json.parse("{\"tserver-flag\":" + (isEdit ? "\"t2\"}" : "\"t1\"}"));
                  assertValues.putAll(ImmutableMap.of("gflags", gflagValue));
                }
              }
              assertNodeSubTask(tasks, assertValues);
            }
            position++;
          }
        }
        break;
      case NON_ROLLING_UPGRADE:
        for (int j = 0; j < GFLAGS_UPGRADE_TASK_SEQUENCE.size(); j++) {
          Map<String, Object> assertValues = new HashMap<String, Object>();
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          TaskType taskType = assertTaskType(tasks, GFLAGS_UPGRADE_TASK_SEQUENCE.get(j));

          if (NON_NODE_TASKS.contains(taskType)) {
            assertEquals(1, tasks.size());
          } else {
            assertValues.putAll(ImmutableMap.of(
                    "nodeNames", (Object) ImmutableList.of("host-n1", "host-n2", "host-n3"),
                    "nodeCount", 3
            ));
            if (taskType.equals(TaskType.AnsibleConfigureServers)) {
              if (!isDelete) {
                JsonNode gflagValue = serverType.equals(MASTER) ?
                        Json.parse("{\"master-flag\":" + (isEdit ? "\"m2\"}" : "\"m1\"}")) :
                        Json.parse("{\"tserver-flag\":" + (isEdit ? "\"t2\"}" : "\"t1\"}"));
                assertValues.putAll(ImmutableMap.of("gflags", gflagValue));
              }
              assertValues.put("processType", serverType.toString());

            }
            assertEquals(3, tasks.size());
            assertNodeSubTask(tasks, assertValues);
          }
          position++;
        }
        break;

      case NON_RESTART_UPGRADE:
        for (int j = 0; j < GFLAGS_NON_ROLLING_UPGRADE_TASK_SEQUENCE.size(); j++) {
          Map<String, Object> assertValues = new HashMap<String, Object>();
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          TaskType taskType = assertTaskType(tasks,
                                             GFLAGS_NON_ROLLING_UPGRADE_TASK_SEQUENCE.get(j));

          if (NON_NODE_TASKS.contains(taskType)) {
            assertEquals(1, tasks.size());
          } else {
            assertValues.putAll(ImmutableMap.of(
                    "nodeNames", (Object) ImmutableList.of("host-n1", "host-n2", "host-n3"),
                    "nodeCount", 3
            ));
            if (taskType.equals(TaskType.AnsibleConfigureServers)) {
              if (!isDelete) {
                JsonNode gflagValue = serverType.equals(MASTER) ?
                        Json.parse("{\"master-flag\":" + (isEdit ? "\"m2\"}" : "\"m1\"}")) :
                        Json.parse("{\"tserver-flag\":" + (isEdit ? "\"t2\"}" : "\"t1\"}"));
                assertValues.putAll(ImmutableMap.of("gflags", gflagValue));
              }
            }
            assertEquals(3, tasks.size());
            assertNodeSubTask(tasks, assertValues);
          }
          position++;
        }
        break;
    }

    return position;
  }

  public enum UpgradeType {
    ROLLING_UPGRADE,
    ROLLING_UPGRADE_MASTER_ONLY,
    ROLLING_UPGRADE_TSERVER_ONLY,
    FULL_UPGRADE,
    FULL_UPGRADE_MASTER_ONLY,
    FULL_UPGRADE_TSERVER_ONLY
  }

  private int assertRollingRestartCommonTasks(Map<Integer, List<TaskInfo>> subTasksByPosition,
                                              int startPosition) {
    int position = startPosition;
    List<TaskType> commonNodeTasks = new ArrayList<>();
    commonNodeTasks.addAll(ImmutableList.of(
          TaskType.LoadBalancerStateChange,
          TaskType.UniverseUpdateSucceeded));
    for (int i = 0; i < commonNodeTasks.size(); i++) {
      assertTaskType(subTasksByPosition.get(position), commonNodeTasks.get(i));
      position++;
    }
    return position;
  }

  private int assertGFlagsCommonTasks(Map<Integer, List<TaskInfo>> subTasksByPosition,
                                      int startPosition, UpgradeType type, boolean isFinalStep) {
    int position = startPosition;
    List<TaskType> commonNodeTasks = new ArrayList<>();
    if (type.name().equals("ROLLING_UPGRADE") || type.name().equals("ROLLING_UPGRADE_TSERVER_ONLY")) {
      commonNodeTasks.add(TaskType.LoadBalancerStateChange);
    }

    if (isFinalStep) {
      commonNodeTasks.addAll(ImmutableList.of(
          TaskType.UpdateAndPersistGFlags,
          TaskType.UniverseUpdateSucceeded));
    }
    for (int i = 0; i < commonNodeTasks.size(); i++) {
      assertTaskType(subTasksByPosition.get(position), commonNodeTasks.get(i));
      position++;
    }
    return position;
  }

  private int assertSoftwareCommonTasks(Map<Integer, List<TaskInfo>> subTasksByPosition,
                                        int startPosition, UpgradeType type, boolean isFinalStep) {
    int position = startPosition;
    List<TaskType> commonNodeTasks = new ArrayList<>();

    if (isFinalStep) {
      if (type.name().equals("ROLLING_UPGRADE") || type.name().equals("ROLLING_UPGRADE_TSERVER_ONLY")) {
        commonNodeTasks.add(TaskType.LoadBalancerStateChange);
      }

      commonNodeTasks.addAll(ImmutableList.of(
          TaskType.UpdateSoftwareVersion,
          TaskType.UniverseUpdateSucceeded));
    }
    for (int i = 0; i < commonNodeTasks.size(); i++) {
      assertTaskType(subTasksByPosition.get(position), commonNodeTasks.get(i));
      position++;
    }
    return position;
  }

  private void assertNodeSubTask(List<TaskInfo> subTasks,
                                 Map<String, Object> assertValues) {

    List<String> nodeNames = subTasks.stream()
        .map(t -> t.getTaskDetails().get("nodeName").textValue())
        .collect(Collectors.toList());
    int nodeCount = (int) assertValues.getOrDefault("nodeCount", 1);
    assertEquals(nodeCount, nodeNames.size());
    if (nodeCount == 1) {
      assertEquals(assertValues.get("nodeName"), nodeNames.get(0));
    } else {
      assertTrue(nodeNames.containsAll((List)assertValues.get("nodeNames")));
    }

    List<JsonNode> subTaskDetails = subTasks.stream()
        .map(t -> t.getTaskDetails())
        .collect(Collectors.toList());
    assertValues.forEach((expectedKey, expectedValue) -> {
      if (!ImmutableList.of("nodeName", "nodeNames", "nodeCount").contains(expectedKey)) {
        List<Object> values = subTaskDetails.stream()
            .map(t -> {
              JsonNode data = PROPERTY_KEYS.contains(expectedKey) ?
                  t.get("properties").get(expectedKey) : t.get(expectedKey);
              return data.isObject() ? data : data.textValue();
            })
            .collect(Collectors.toList());
        values.forEach((actualValue) -> assertEquals(actualValue, expectedValue));
      }
    });
  }

  private TaskType assertTaskType(List<TaskInfo> tasks, TaskType expectedTaskType) {
    TaskType taskType = tasks.get(0).getTaskType();
    assertEquals(expectedTaskType, taskType);
    return taskType;
  }

  @Test
  public void testSoftwareUpgradeWithSameVersion() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.ybSoftwareVersion = "old-version";

    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.Software);
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.version);
    // In case of an exception, no task should be queued.
    assertEquals(0, taskInfo.getSubTasks().size());
  }

  @Test
  public void testSoftwareUpgradeWithoutVersion() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.Software);
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.version);
    // In case of an exception, no task should be queued.
    assertEquals(0, taskInfo.getSubTasks().size());
  }

  @Test
  public void testSoftwareUpgrade() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.ybSoftwareVersion = "new-version";
    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.Software);
    verify(mockNodeManager, times(21)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));

    int position = 0;
    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(3, downloadTasks.size());
    position = assertSoftwareUpgradeSequence(subTasksByPosition, MASTER, position, true);
    assertTaskType(subTasksByPosition.get(position++), TaskType.LoadBalancerStateChange);
    position = assertSoftwareCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, false);
    position = assertSoftwareUpgradeSequence(subTasksByPosition, TSERVER, position, true);
    assertSoftwareCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true);
    assertEquals(50, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareNonRollingUpgrade() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.ybSoftwareVersion = "new-version";
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.Software);
    ArgumentCaptor<NodeTaskParams> commandParams = ArgumentCaptor.forClass(NodeTaskParams.class);
    verify(mockNodeManager, times(21)).nodeCommand(any(), commandParams.capture());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    int position = 0;
    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(3, downloadTasks.size());
    position = assertSoftwareUpgradeSequence(subTasksByPosition, MASTER, position, false);
    position = assertSoftwareUpgradeSequence(subTasksByPosition, TSERVER, position, false);
    assertSoftwareCommonTasks(subTasksByPosition, position, UpgradeType.FULL_UPGRADE, true);
    assertEquals(13, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
  }

  @Test
  public void testGFlagsNonRollingUpgrade() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.GFlags);
    verify(mockNodeManager, times(18)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));

    int position = 0;
    position = assertGFlagsUpgradeSequence(subTasksByPosition, MASTER, position,
                                           UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    position = assertGFlagsUpgradeSequence(subTasksByPosition, TSERVER, position,
                                           UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    position = assertGFlagsCommonTasks(subTasksByPosition, position, UpgradeType.FULL_UPGRADE, true);
    assertEquals(14, position);
  }

  @Test
  public void testGFlagsNonRollingMasterOnlyUpgrade() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.GFlags);
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));

    int position = 0;
    position = assertGFlagsUpgradeSequence(subTasksByPosition, MASTER, position,
                                           UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    position = assertGFlagsCommonTasks(subTasksByPosition, position,
                                       UpgradeType.FULL_UPGRADE_MASTER_ONLY, true);
    assertEquals(8, position);
  }

  @Test
  public void testGFlagsNonRollingTServerOnlyUpgrade() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.GFlags);
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));

    int position = 0;
    position = assertGFlagsUpgradeSequence(subTasksByPosition, TSERVER, position,
                                           UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    position = assertGFlagsCommonTasks(subTasksByPosition, position,
                                       UpgradeType.FULL_UPGRADE_TSERVER_ONLY, true);

    assertEquals(8, position);
  }

  @Test
  public void testGFlagsUpgradeWithMasterGFlags() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.GFlags);
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));

    int position = 0;
    position = assertGFlagsUpgradeSequence(subTasksByPosition, MASTER, position,
                                           UpgradeParams.UpgradeOption.ROLLING_UPGRADE);
    position = assertGFlagsCommonTasks(subTasksByPosition, position,
                                       UpgradeType.ROLLING_UPGRADE_MASTER_ONLY, true);
    assertEquals(26, position);
  }

  @Test
  public void testGFlagsUpgradeWithTServerGFlags() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.GFlags);
    ArgumentCaptor<NodeTaskParams> commandParams = ArgumentCaptor.forClass(NodeTaskParams.class);
    verify(mockNodeManager, times(9)).nodeCommand(any(), commandParams.capture());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.LoadBalancerStateChange);
    position = assertGFlagsUpgradeSequence(subTasksByPosition, TSERVER, position,
                                           UpgradeParams.UpgradeOption.ROLLING_UPGRADE);
    position = assertGFlagsCommonTasks(subTasksByPosition, position,
                                       UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, true);
    assertEquals(28, position);
  }

  @Test
  public void testGFlagsUpgrade() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.GFlags);
    verify(mockNodeManager, times(18)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));

    int position = 0;
    position = assertGFlagsUpgradeSequence(subTasksByPosition, MASTER, position,
                                           UpgradeParams.UpgradeOption.ROLLING_UPGRADE);
    position = assertGFlagsCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE,
                                       false);
    position = assertGFlagsUpgradeSequence(subTasksByPosition, TSERVER, position,
                                           UpgradeParams.UpgradeOption.ROLLING_UPGRADE);
    position = assertGFlagsCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE,
                                       true);
    assertEquals(52, position);
  }

  @Test
  public void testGFlagsUpgradeWithEmptyFlags() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.GFlags);
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.version);
    // In case of an exception, no task should be queued.
    assertEquals(0, taskInfo.getSubTasks().size());
  }

  @Test
  public void testGFlagsUpgradeWithSameMasterFlags() {
    Master.SysClusterConfigEntryPB.Builder configBuilder =
      Master.SysClusterConfigEntryPB.newBuilder().setVersion(3);
    GetMasterClusterConfigResponse mockConfigResponse =
      new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
    } catch (Exception e) {}
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    // Simulate universe created with master flags and tserver flags.
    final Map<String, String> masterFlags = ImmutableMap.of("master-flag", "m123");
    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
        userIntent.masterGFlags = masterFlags;
        userIntent.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
        universe.setUniverseDetails(universeDetails);
       }
    };
    Universe.saveDetails(defaultUniverse.universeUUID, updater);

    // Upgrade with same master flags but different tserver flags should not run master tasks.
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = masterFlags;
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t2");

    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.GFlags, 3);
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
            subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    List<TaskInfo> tasks = subTasksByPosition.get(0);
    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.LoadBalancerStateChange);
    position = assertGFlagsUpgradeSequence(subTasksByPosition, TSERVER, position,
                                           UpgradeParams.UpgradeOption.ROLLING_UPGRADE, true);
    position = assertGFlagsCommonTasks(subTasksByPosition, position,
                   UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, true);
    assertEquals(28, position);
  }

  @Test
  public void testGFlagsUpgradeWithSameTserverFlags() {
    Master.SysClusterConfigEntryPB.Builder configBuilder =
      Master.SysClusterConfigEntryPB.newBuilder().setVersion(3);
    GetMasterClusterConfigResponse mockConfigResponse =
      new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
    } catch (Exception e) {}
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    // Simulate universe created with master flags and tserver flags.
    final Map<String, String> tserverFlags = ImmutableMap.of("tserver-flag", "m123");
    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
        userIntent.masterGFlags = ImmutableMap.of("master-flag", "m1");
        userIntent.tserverGFlags = tserverFlags;
        universe.setUniverseDetails(universeDetails);
       }
    };
    Universe.saveDetails(defaultUniverse.universeUUID, updater);

    // Upgrade with same master flags but different tserver flags should not run master tasks.
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m2");;
    taskParams.tserverGFlags = tserverFlags;

    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.GFlags, 3);
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
            subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    int position = 0;
    position = assertGFlagsUpgradeSequence(subTasksByPosition, MASTER, position,
                                           UpgradeParams.UpgradeOption.ROLLING_UPGRADE, true);
    position = assertGFlagsCommonTasks(subTasksByPosition, position,
                   UpgradeType.ROLLING_UPGRADE_MASTER_ONLY, true);
    assertEquals(26, position);
  }

  @Test
  public void testRemoveFlags() {
    for (ServerType serverType : ImmutableList.of(MASTER, TSERVER)) {
      if (serverType.equals(MASTER)) {
        Master.SysClusterConfigEntryPB.Builder configBuilder =
          Master.SysClusterConfigEntryPB.newBuilder().setVersion(3);
        GetMasterClusterConfigResponse mockConfigResponse =
          new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
        try {
          when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
        } catch (Exception e) {}
        when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
      } else if (serverType.equals(TSERVER)) {
        Master.SysClusterConfigEntryPB.Builder configBuilder =
          Master.SysClusterConfigEntryPB.newBuilder().setVersion(4);
        GetMasterClusterConfigResponse mockConfigResponse =
          new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
        try {
          when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
        } catch (Exception e) {}
        when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
      }
      System.out.println("SERVER TYPE: " + serverType.toString());
      // Simulate universe created with master flags and tserver flags.
      final Map<String, String> tserverFlags = ImmutableMap.of("tserver-flag", "t1");
      final Map<String, String> masterGFlags = ImmutableMap.of("master-flag", "m1");
      Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
        public void run(Universe universe) {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.masterGFlags = masterGFlags;
          userIntent.tserverGFlags = tserverFlags;
          universe.setUniverseDetails(universeDetails);
        }
      };
      Universe.saveDetails(defaultUniverse.universeUUID, updater);

      UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
      // This is a delete operation on the master flags.
      if (serverType == MASTER) {
        taskParams.masterGFlags = new HashMap<>();
        taskParams.tserverGFlags = tserverFlags;
      } else {
        taskParams.masterGFlags = masterGFlags;
        taskParams.tserverGFlags = new HashMap<>();
      }

      int expectedVersion = serverType == MASTER ? 3 : 4;
      TaskInfo taskInfo = submitTask(taskParams,
              UpgradeUniverse.UpgradeTaskType.GFlags,
              expectedVersion);

      int numInvocations = serverType == MASTER ? 9 : 18;
      verify(mockNodeManager, times(numInvocations)).nodeCommand(any(), any());

      List<TaskInfo> subTasks = new ArrayList<>(taskInfo.getSubTasks());
      Map<Integer, List<TaskInfo>> subTasksByPosition =
              subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
      int position = 0;
      if (serverType != MASTER) {
        assertTaskType(subTasksByPosition.get(position++), TaskType.LoadBalancerStateChange);
      }
      position = assertGFlagsUpgradeSequence(subTasksByPosition, serverType, position,
              UpgradeParams.UpgradeOption.ROLLING_UPGRADE, true, true);
      position = assertGFlagsCommonTasks(subTasksByPosition, position, serverType == MASTER ?
                      UpgradeType.ROLLING_UPGRADE_MASTER_ONLY :
                      UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY,
              true);
      assertEquals(serverType == MASTER ? 26 : 28, position);
    }
  }

  public void testGFlagsUpgradeNonRestart() throws Exception {
    // Simulate universe created with master flags and tserver flags.
    final Map<String, String> tserverFlags = ImmutableMap.of("tserver-flag", "t1");
    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
        userIntent.masterGFlags = ImmutableMap.of("master-flag", "m1");
        userIntent.tserverGFlags = tserverFlags;
        universe.setUniverseDetails(universeDetails);
      }
    };
    Universe.saveDetails(defaultUniverse.universeUUID, updater);

    //SetFlagResponse response = new SetFlagResponse(0, "", null);
    when(mockClient.setFlag(any(), any(), any(), anyBoolean())).thenReturn(true);

    // Upgrade with same master flags but different tserver flags should not run master tasks.
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m2");;
    taskParams.tserverGFlags = tserverFlags;
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.NON_RESTART_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.GFlags, 3);
    verify(mockNodeManager, times(3)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
            subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    int position = 0;
    position = assertGFlagsUpgradeSequence(subTasksByPosition, MASTER, position,
                                           taskParams.upgradeOption, true);
    position = assertGFlagsCommonTasks(subTasksByPosition, position,
            UpgradeType.ROLLING_UPGRADE_MASTER_ONLY, true);
    assertEquals(6, position);
  }

  @Test
  public void testRollingRestart() throws Exception {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    TaskInfo taskInfo = submitTask(taskParams, UpgradeUniverse.UpgradeTaskType.Restart);
    verify(mockNodeManager, times(12)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));

    int position = 0;
    position = assertRollingRestartSequence(subTasksByPosition, MASTER, position);
    assertTaskType(subTasksByPosition.get(position++), TaskType.LoadBalancerStateChange);
    position = assertRollingRestartSequence(subTasksByPosition, TSERVER, position);
    assertRollingRestartCommonTasks(subTasksByPosition, position);
    assertEquals(43, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
  }
}

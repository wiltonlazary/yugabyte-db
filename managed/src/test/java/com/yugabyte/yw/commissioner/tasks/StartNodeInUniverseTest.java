// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;

import java.util.*;
import java.util.stream.Collectors;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.*;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.class)
public class StartNodeInUniverseTest extends CommissionerBaseTest {

    @InjectMocks
    Commissioner commissioner;
    Universe defaultUniverse;
    ShellProcessHandler.ShellResponse dummyShellResponse;

    @Before
    public void setUp() {
        super.setUp();
        Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
        AvailabilityZone.create(region, "az-1", "AZ 1", "subnet-1");
        // create default universe
        UniverseDefinitionTaskParams.UserIntent userIntent =
                new UniverseDefinitionTaskParams.UserIntent();
        userIntent.numNodes = 3;
        userIntent.ybSoftwareVersion = "yb-version";
        userIntent.accessKeyCode = "demo-access";
        userIntent.regionList = ImmutableList.of(region.uuid);
        defaultUniverse = createUniverse(defaultCustomer.getCustomerId());
        Universe.saveDetails(defaultUniverse.universeUUID,
                ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));

        dummyShellResponse =  new ShellProcessHandler.ShellResponse();
        dummyShellResponse.message = "true";
        when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
    }


    private TaskInfo submitTask(NodeTaskParams taskParams, String nodeName) {
        taskParams.expectedUniverseVersion = 2;
        taskParams.nodeName = nodeName;
        try {
            UUID taskUUID = commissioner.submit(TaskType.StartNodeInUniverse, taskParams);
            return waitForTask(taskUUID);
        } catch (InterruptedException e) {
            assertNull(e.getMessage());
        }
        return null;
    }

    List<TaskType> START_NODE_TASK_SEQUENCE = ImmutableList.of(
            TaskType.SetNodeState,
            TaskType.AnsibleClusterServerCtl,
            TaskType.UpdateNodeProcess,
            TaskType.SetNodeState,
            TaskType.UniverseUpdateSucceeded
    );

    List<JsonNode> START_NODE_TASK_EXPECTED_RESULTS = ImmutableList.of(
            Json.toJson(ImmutableMap.of("state", "Starting")),
            Json.toJson(ImmutableMap.of("process", "tserver",
                            "command", "start")),
            Json.toJson(ImmutableMap.of("processType", "TSERVER",
                            "isAdd", true)),
            Json.toJson(ImmutableMap.of("state", "Live")),
            Json.toJson(ImmutableMap.of())
    );


    List<TaskType> WITH_MASTER_UNDER_REPLICATED = ImmutableList.of(
            TaskType.SetNodeState,
            TaskType.AnsibleClusterServerCtl,
            TaskType.UpdateNodeProcess,
            TaskType.AnsibleClusterServerCtl,
            TaskType.UpdateNodeProcess,
            TaskType.WaitForServer,
            TaskType.ChangeMasterConfig,
            TaskType.SetNodeState,
            TaskType.UniverseUpdateSucceeded
    );

    List<JsonNode> WITH_MASTER_UNDER_REPLICATED_RESULTS = ImmutableList.of(
            Json.toJson(ImmutableMap.of("state", "Starting")),
            Json.toJson(ImmutableMap.of("process", "tserver",
                    "command", "start")),
            Json.toJson(ImmutableMap.of("processType", "TSERVER",
                    "isAdd", true)),
            Json.toJson(ImmutableMap.of("process", "master",
                    "command", "start")),
            Json.toJson(ImmutableMap.of("processType", "MASTER",
                    "isAdd", true)),
            Json.toJson(ImmutableMap.of()),
            Json.toJson(ImmutableMap.of()),
            Json.toJson(ImmutableMap.of("state", "Live")),
            Json.toJson(ImmutableMap.of())

    );


    private void assertStartNodeSequence(Map<Integer, List<TaskInfo>> subTasksByPosition,
                                         boolean masterUnderReplicated) {
        int position = 0;
        if (masterUnderReplicated) {
            for (TaskType taskType: WITH_MASTER_UNDER_REPLICATED) {
                List<TaskInfo> tasks = subTasksByPosition.get(position);
                assertEquals(1, tasks.size());
                assertEquals(taskType, tasks.get(0).getTaskType());
                JsonNode expectedResults =
                        WITH_MASTER_UNDER_REPLICATED_RESULTS.get(position);
                List<JsonNode> taskDetails = tasks.stream()
                        .map(t -> t.getTaskDetails())
                        .collect(Collectors.toList());
                assertJsonEqual(expectedResults, taskDetails.get(0));
                position++;
            }
        } else {
            for (TaskType taskType: START_NODE_TASK_SEQUENCE) {
                List<TaskInfo> tasks = subTasksByPosition.get(position);
                assertEquals(1, tasks.size());
                assertEquals(taskType, tasks.get(0).getTaskType());
                JsonNode expectedResults =
                        START_NODE_TASK_EXPECTED_RESULTS.get(position);
                List<JsonNode> taskDetails = tasks.stream()
                        .map(t -> t.getTaskDetails())
                        .collect(Collectors.toList());
                assertJsonEqual(expectedResults, taskDetails.get(0));
                position++;
            }
        }

    }

    @Test
    public void testAddNodeSuccess() {
        NodeTaskParams taskParams = new NodeTaskParams();
        taskParams.universeUUID = defaultUniverse.universeUUID;

        TaskInfo taskInfo = submitTask(taskParams, "host-n1");
        verify(mockNodeManager, times(2)).nodeCommand(any(), any());
        List<TaskInfo> subTasks = taskInfo.getSubTasks();
        Map<Integer, List<TaskInfo>> subTasksByPosition =
                subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
        assertEquals(subTasksByPosition.size(), START_NODE_TASK_SEQUENCE.size());
        assertStartNodeSequence(subTasksByPosition, false);
    }

    @Test
    public void testAddNodeWithUnderReplicatedMaster() {
        Universe universe = createUniverse("Demo");
        universe = Universe.saveDetails(universe.universeUUID,
                ApiUtils.mockUniverseUpdaterWithInactiveNodes());
        NodeTaskParams taskParams = new NodeTaskParams();
        taskParams.universeUUID = universe.universeUUID;
        TaskInfo taskInfo = submitTask(taskParams, "host-n1");
        verify(mockNodeManager, times(3)).nodeCommand(any(), any());
        List<TaskInfo> subTasks = taskInfo.getSubTasks();
        Map<Integer, List<TaskInfo>> subTasksByPosition =
                subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
        assertEquals(subTasksByPosition.size(), WITH_MASTER_UNDER_REPLICATED.size());
        assertStartNodeSequence(subTasksByPosition, true);
    }

    @Test
    public void testAddUnknownNode() {
        NodeTaskParams taskParams = new NodeTaskParams();
        taskParams.universeUUID = defaultUniverse.universeUUID;
        TaskInfo taskInfo = submitTask(taskParams, "host-n9");
        verify(mockNodeManager, times(0)).nodeCommand(any(), any());
        assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    }
}

// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.Map;
import java.util.HashMap;
import java.util.Collection;
import java.util.Optional;

import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeoutException;

import com.yugabyte.yw.forms.SubTaskFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.Provider;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.CustomerTaskFormData;

import play.libs.Json;
import play.mvc.Result;

public class CustomerTaskController extends AuthenticatedController {

  @Inject
  Commissioner commissioner;

  protected static final int TASK_HISTORY_LIMIT = 6;
  public static final Logger LOG = LoggerFactory.getLogger(CustomerTaskController.class);

  private List<SubTaskFormData> fetchFailedSubTasks(UUID parentUUID) {
    List<TaskInfo> result = TaskInfo.getFailedSubTasks(parentUUID);
    List<SubTaskFormData> subTasks = new ArrayList<>();
    for (TaskInfo taskInfo : result) {
      SubTaskFormData subTaskData = new SubTaskFormData();
      subTaskData.subTaskUUID = taskInfo.getTaskUUID();
      subTaskData.subTaskType = taskInfo.getTaskType().name();
      subTaskData.subTaskState = taskInfo.getTaskState().name();
      subTaskData.creationTime = taskInfo.getCreationTime();
      subTaskData.subTaskGroupType = taskInfo.getSubTaskGroupType().name();
      subTaskData.errorString = taskInfo.getTaskDetails().get("errorString").asText();
      subTasks.add(subTaskData);
    }
    return subTasks;
  }

  private Map<UUID, List<CustomerTaskFormData>> iterateTasksList(
    Collection<CustomerTask> collection,
    boolean pendingTasksOnly
  ) {
    Map<UUID, List<CustomerTaskFormData>> taskListMap = new HashMap<>();
    for (CustomerTask task : collection) {
      try {
        CustomerTaskFormData taskData = new CustomerTaskFormData();

        JsonNode taskProgress = commissioner.getStatus(task.getTaskUUID());
        // If the task progress API returns error, we will log it and not add that task
        // to the task list for UI rendering.
        if (taskProgress.has("error")) {
          LOG.error("Error fetching Task Progress for " + task.getTaskUUID() +
            ", Error: " + taskProgress.get("error"));
        } else {
          taskData.percentComplete = taskProgress.get("percent").asInt();
          taskData.status = taskProgress.get("status").asText();
          taskData.id = task.getTaskUUID();
          taskData.title = task.getFriendlyDescription();
          taskData.createTime = task.getCreateTime();
          taskData.completionTime = task.getCompletionTime();
          taskData.target = task.getTarget().name();
          taskData.type = task.getType().getFriendlyName();
          taskData.targetUUID = task.getTargetUUID();

          if (pendingTasksOnly) {
            if (TaskInfo.INCOMPLETE_STATES.contains(taskData.status)) {
              List<CustomerTaskFormData> taskList = taskListMap.getOrDefault(task.getTargetUUID(),
                new ArrayList<>());
              taskList.add(taskData);
              taskListMap.put(task.getTargetUUID(), taskList);
            }
          } else {
            List<CustomerTaskFormData> taskList = taskListMap.getOrDefault(task.getTargetUUID(),
              new ArrayList<>());
            taskList.add(taskData);
            taskListMap.put(task.getTargetUUID(), taskList);
          }
        }
      } catch(RuntimeException e) {
        LOG.error("Error fetching Task Progress for " +  task.getTaskUUID() +
          ", TaskInfo with that taskUUID not found");
      }
    }
    return taskListMap;
  }

  private ObjectNode fetchTasks(UUID customerUUID, UUID targetUUID, int offset, int limit) {
    List<CustomerTask> pendingTasks = CustomerTask.findCustomerTasks(customerUUID,
      targetUUID, offset,limit);
    ObjectNode result = Json.newObject();
    result.put("items", CustomerTask.countCustomerTasks(customerUUID));
    Map<UUID, List<CustomerTaskFormData>> taskListMap = iterateTasksList(pendingTasks, false);
    ObjectMapper mapper = new ObjectMapper();
    JsonNode tasksJson = mapper.valueToTree(taskListMap);
    result.set("data", tasksJson);
    return result;
  }

  private Map<UUID, List<CustomerTaskFormData>> fetchIncompleteTasks(UUID customerUUID) {
    List<CustomerTask> pendingTasks = CustomerTask.findIncompleteCustomerTargetTasks(customerUUID,
      null);
    return iterateTasksList(pendingTasks, true);
  }
  private Map<UUID, List<CustomerTaskFormData>> fetchIncompleteUniverseTasks(UUID customerUUID,
                                                                             UUID universeUUID) {
    List<CustomerTask> pendingTasks = CustomerTask.findIncompleteCustomerTargetTasks(customerUUID,
      universeUUID);
    return iterateTasksList(pendingTasks, true);
  }

  private Map<UUID, List<CustomerTaskFormData>> fetchIncompleteUniverseTasks(UUID customerUUID) {
    List<CustomerTask> pendingTasks = CustomerTask.findCustomerAllUniversesIncompleteTasks(
      customerUUID
    );
    return iterateTasksList(pendingTasks, true);
  }

  public Result list(UUID customerUUID, Optional<String> target,
                     Optional<String> status, int page, int limit) {
    Customer customer = Customer.get(customerUUID);

    if (customer == null) {
      ObjectNode responseJson = Json.newObject();
      responseJson.put("error", "Invalid Customer UUID: " + customerUUID);
      return badRequest(responseJson);
    }

    Map<UUID, List<CustomerTaskFormData>> taskList;
    ObjectNode result;
    if (target.isPresent() && target.get().equals("universes")
      && status.isPresent() && status.get().equals("pending")) {
      taskList = fetchIncompleteUniverseTasks(customerUUID);
      return ApiResponse.success(taskList);
    } else if (!target.isPresent() && status.isPresent() && status.get().equals("pending")) {
      taskList = fetchIncompleteTasks(customerUUID);
      return ApiResponse.success(taskList);
    } else {
      // Page size is equal to limit
      int rowOffset = (page - 1) * limit;
      result = fetchTasks(customerUUID, null, rowOffset, limit);
      return ApiResponse.success(result);
    }
  }

  public Result universeTasks(UUID customerUUID, UUID universeUUID, Optional<String> target,
                              Optional<String> status, int page, int limit) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    try {
      Universe universe = Universe.get(universeUUID);
      Map<UUID, List<CustomerTaskFormData>> taskList;
      if (status.isPresent() && status.get().equals("pending")) {
        taskList = fetchIncompleteUniverseTasks(customerUUID, universe.universeUUID);
        return ApiResponse.success(taskList);
      } else {
        // Page size is equal to limit
        int rowOffset = (page - 1) * limit;
        ObjectNode result = fetchTasks(customerUUID, universe.universeUUID, rowOffset, limit);
        return ApiResponse.success(result);
      }
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Universe UUID: " + universeUUID);
    }
  }

  public Result providerTasks(UUID customerUUID, UUID providerUUID, int limit) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    try {
      Provider provider = Provider.get(customerUUID, providerUUID);
      // We don't care about pagination right now
      JsonNode result = fetchTasks(customerUUID, provider.uuid, 0, limit).get("data");
      return ApiResponse.success(result);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, "Invalid provider UUID: " + providerUUID);
    }
  }

  public Result status(UUID customerUUID, UUID taskUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    CustomerTask customerTask = CustomerTask.findByTaskUUID(taskUUID);
    if (customerTask == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer Task UUID: " + taskUUID);
    }

    try {
      ObjectNode responseJson = commissioner.getStatus(taskUUID);
      return ok(responseJson);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, e.getMessage());
    }
  }

  public Result failedSubtasks(UUID customerUUID, UUID taskUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    CustomerTask customerTask = CustomerTask.findByTaskUUID(taskUUID);
    if (customerTask == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer Task UUID: " + taskUUID);
    }

    List<SubTaskFormData> failedSubTasks = fetchFailedSubTasks(taskUUID);
    ObjectNode responseJson = Json.newObject();
    responseJson.put("failedSubTasks", Json.toJson(failedSubTasks));
    return ok(responseJson);
  }

  public Result retryTask(UUID customerUUID, UUID taskUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    CustomerTask customerTask = CustomerTask.get(customer.uuid, taskUUID);

    if (customerTask == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer Task UUID: " + taskUUID);
    }

    TaskInfo taskInfo = TaskInfo.get(taskUUID);

    if (taskInfo == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer Task UUID: " + taskUUID);
    } else if (taskInfo.getTaskType() != TaskType.CreateUniverse) {
      String errMsg = String.format(
        "Invalid task type: %s. Only 'Create Universe' task retries are supported.",
        taskInfo.getTaskType().toString());
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    JsonNode oldTaskParams = commissioner.getTaskDetails(taskUUID);
    if (oldTaskParams == null) {
      return ApiResponse.error(
        BAD_REQUEST, "Failed to retrieve task params for Task UUID: " + taskUUID);
    }

    UniverseDefinitionTaskParams params = Json.fromJson(
      oldTaskParams, UniverseDefinitionTaskParams.class);
    params.firstTry = false;
    Universe universe = Universe.get(params.universeUUID);
    if (universe == null) {
      return ApiResponse.error(
        BAD_REQUEST, "Did not find failed universe with uuid: " + params.universeUUID);
    }

    UUID newTaskUUID = commissioner.submit(taskInfo.getTaskType(), params);
    LOG.info("Submitted retry task to create universe for {}:{}, task uuid = {}.",
      universe.universeUUID, universe.name, newTaskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(customer,
                        universe.universeUUID,
                        newTaskUUID,
                        CustomerTask.TargetType.Universe,
                        CustomerTask.TaskType.Create,
                        universe.name);
    LOG.info("Saved task uuid " + newTaskUUID + " in customer tasks table for universe " +
      universe.universeUUID + ":" + universe.name);

    ObjectNode resultNode = (ObjectNode) universe.toJson();
    resultNode.put("taskUUID", newTaskUUID.toString());
    Audit.createAuditEntry(ctx(), request(), Json.toJson(params), newTaskUUID);
    return ok(resultNode);
  }
}

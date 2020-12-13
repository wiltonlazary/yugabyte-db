// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.HealthManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.CustomerRegisterFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.Configuration;
import play.api.Play;
import play.libs.Json;

public abstract class AbstractTaskBase implements ITask {

  public static final Logger LOG = LoggerFactory.getLogger(AbstractTaskBase.class);

  // Number of concurrent tasks to execute at a time.
  private static final int TASK_THREADS = 10;

  // The maximum time that excess idle threads will wait for new tasks before terminating.
  // The unit is specified in the API (and is seconds).
  private static final long THREAD_ALIVE_TIME = 60L;

  // The params for this task.
  protected ITaskParams taskParams;

  // The threadpool on which the tasks are executed.
  protected ExecutorService executor;

  // The sequence of task lists that should be executed.
  protected SubTaskGroupQueue subTaskGroupQueue;

  // The UUID of the top-level user-facing task at the top of Task tree. Eg. CreateUniverse, etc.
  protected UUID userTaskUUID;

  // A field used to send additional information with prometheus metric associated with this task
  public String taskInfo = "";

  protected ITaskParams taskParams() {
    return taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    this.taskParams = params;
  }

  @Override
  public String getName() {
    return this.getClass().getSimpleName();
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public String toString() {
    return getName() + " : details=" + getTaskDetails();
  }

  @Override
  public abstract void run();

  // Create an task pool which can handle an unbounded number of tasks, while using an initial set
  // of threads which get spawned upto TASK_THREADS limit.
  public void createThreadpool() {
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-" + getName() + "-%d").build();
    executor =
        new ThreadPoolExecutor(TASK_THREADS, TASK_THREADS, THREAD_ALIVE_TIME,
                               TimeUnit.SECONDS, new LinkedBlockingQueue<Runnable>(),
                               namedThreadFactory);
  }

  @Override
  public void setUserTaskUUID(UUID userTaskUUID) {
    this.userTaskUUID = userTaskUUID;
  }

  /**
   * @param response : ShellResponse object
   */
  public void processShellResponse(ShellResponse response) {
    if (response.code != 0) {
      throw new RuntimeException((response.message != null ) ? response.message : "error");
    }
  }

  /**
   * We would try to parse the shell response message as JSON and return JsonNode
   *
   * @param response: ShellResponse object
   * @return JsonNode: Json formatted shell response message
   */
  public JsonNode parseShellResponseAsJson(ShellResponse response) {
    return Util.convertStringToJson(response.message);
  }

  public UniverseUpdater nodeStateUpdater(final UUID universeUUID, final String nodeName,
                                          final NodeDetails.NodeState state) {
    UniverseUpdater updater = new UniverseUpdater() {
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        NodeDetails node = universe.getNode(nodeName);
        if (node == null) {
          return;
        }
        LOG.info("Changing node {} state from {} to {} in universe {}.",
                  nodeName, node.state, state, universeUUID);
        node.state = state;
        if (state == NodeDetails.NodeState.Decommissioned) {
          node.cloudInfo.private_ip = null;
          node.cloudInfo.public_ip = null;
        }

        // Update the node details.
        universeDetails.nodeDetailsSet.add(node);
        universe.setUniverseDetails(universeDetails);
      }
    };
    return updater;
  }

  @Override
  public boolean shouldSendNotification() {
    try {
      CustomerTask task = CustomerTask.findByTaskUUID(userTaskUUID);
      Customer customer = Customer.get(task.getCustomerUUID());
      CustomerConfig config = CustomerConfig.getAlertConfig(customer.uuid);
      CustomerRegisterFormData.AlertingData alertingData =
        Json.fromJson(config.data, CustomerRegisterFormData.AlertingData.class);
      return task.getType().equals(CustomerTask.TaskType.Create) &&
        task.getTarget().equals(CustomerTask.TargetType.Backup) &&
        alertingData.reportBackupFailures;
    } catch (Exception e) {
      return false;
    }
  }

  @Override
  public void sendNotification() {
    try {
      Configuration appConfig = Play.current().injector().instanceOf(Configuration.class);
      HealthManager healthManager = Play.current().injector().instanceOf(HealthManager.class);
      CustomerTask task = CustomerTask.findByTaskUUID(userTaskUUID);
      Customer customer = Customer.get(task.getCustomerUUID());
      ObjectNode notificationData = Json.newObject()
        .put("alert_name", "Backup failure")
        .put("task_type", task.getType().name())
        .put("target_type", task.getTarget().name())
        .put("target_name", task.getNotificationTargetName())
        .put("task_info", taskInfo);
      String customerTag = String.format("[%s][%s]", customer.name, customer.code);
      List<String> destinations = new ArrayList<>();
      String ybEmail = appConfig.getString("yb.health.default_email", null);
      CustomerConfig config = CustomerConfig.getAlertConfig(customer.uuid);
      CustomerRegisterFormData.AlertingData alertingData =
        Json.fromJson(config.data, CustomerRegisterFormData.AlertingData.class);
      if (alertingData.sendAlertsToYb && ybEmail != null && !ybEmail.isEmpty()) {
        destinations.add(ybEmail);
      }

      if (alertingData.alertingEmail != null && !alertingData.alertingEmail.isEmpty()) {
        destinations.add(alertingData.alertingEmail);
      }

      CustomerConfig smtpConfig = CustomerConfig.getSmtpConfig(customer.uuid);
      CustomerRegisterFormData.SmtpData smtpData = null;
      if (smtpConfig != null) {
        smtpData =  Json.fromJson(smtpConfig.data, CustomerRegisterFormData.SmtpData.class);
      }

      healthManager.runCommand(
        customerTag,
        destinations.size() == 0 ? null : String.join(",", destinations),
        smtpData,
        notificationData
      );
    } catch (Exception e) {
      LOG.error("Error alerting task failure", e);
    }
  }
}

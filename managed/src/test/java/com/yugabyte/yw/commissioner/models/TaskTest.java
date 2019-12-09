package com.yugabyte.yw.commissioner.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.HashMap;
import java.util.Map;

import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.TaskInfo;

import play.libs.Json;

public class TaskTest extends FakeDBApplication {

  public static final Logger LOG = LoggerFactory.getLogger(TaskTest.class);

  @Test
  public void testCreateAndUpdate() {
    // Create the task and save it.
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateUniverse);
    // Set the task details.
    Map<String, String> details = new HashMap<String, String>();
    details.put("description", "Test task");
    details.put("key1", "val1");
    details.put("key2", "val2");
    taskInfo.setTaskDetails(Json.toJson(details));
    // Set the owner info.
    String hostname = "";
    try {
      hostname = InetAddress.getLocalHost().getHostName();
    } catch (UnknownHostException e) {
      LOG.error("Could not determine the hostname", e);
    }
    taskInfo.setOwner(hostname);

    taskInfo.save();

    // Check the various fields.
    assertNotNull(taskInfo.getTaskUUID());
    assertEquals(taskInfo.getTaskType(), TaskType.CreateUniverse);
    assertNotNull(taskInfo.getCreationTime());
    assertNotNull(taskInfo.getLastUpdateTime());
    assertEquals(taskInfo.getLastUpdateTime(), taskInfo.getCreationTime());

    // Sleep so that the last updated time will be different.
    try {
      Thread.sleep(1000);
    } catch (InterruptedException e) { }

    // Update the task, make sure its last update time is changed.
    taskInfo.markAsDirty();
    taskInfo.save();
    assertNotEquals(taskInfo.getLastUpdateTime(), taskInfo.getCreationTime());
  }
}

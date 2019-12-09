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

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteTableFromUniverse;

public class DeleteTable extends UniverseTaskBase {

  @Override
  protected DeleteTableFromUniverse.Params taskParams() {
    return (DeleteTableFromUniverse.Params)taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening. Does not alter 'updateSucceeded' flag so as not
      // to lock out the universe completely in case this task fails.
      lockUniverse(-1 /* expectedUniverseVersion */);

      // Delete table task
      createDeleteTableFromUniverseTask(taskParams())
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeletingTable);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow any other pending tasks against
      // the universe to execute.
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }
}

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
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class BackupUniverse extends UniverseTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(BackupUniverse.class);

  @Override
  protected BackupTableParams taskParams() {
    return (BackupTableParams) taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      Universe universe = Universe.get(taskParams().universeUUID);
      if (universe.getUniverseDetails().backupInProgress) {
        throw new RuntimeException("A backup for this universe is already in progress.");
      }

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      lockUniverse(-1 /* expectedUniverseVersion */);

      UserTaskDetails.SubTaskGroupType groupType;
      if (taskParams().actionType == BackupTableParams.ActionType.CREATE) {
        groupType = UserTaskDetails.SubTaskGroupType.CreatingTableBackup;
        updateBackupState(true);
        unlockUniverseForUpdate();
      } else if (taskParams().actionType == BackupTableParams.ActionType.RESTORE) {
        groupType = UserTaskDetails.SubTaskGroupType.RestoringTableBackup;
      } else {
        throw new RuntimeException("Invalid backup action type: " + taskParams().actionType);
      }
      createTableBackupTask(taskParams(), null).setSubTaskGroupType(groupType);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

      if (taskParams().actionType == BackupTableParams.ActionType.CREATE) {
        // Run all the tasks.
        subTaskGroupQueue.run();
      } else {
        // Run all the tasks.
        subTaskGroupQueue.run();
        unlockUniverseForUpdate();
      }
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      // Run an unlock in case the task failed before getting to the unlock. It is okay if it
      // errors out.
      unlockUniverseForUpdate();
      throw t;
    } finally {
      if (taskParams().actionType == BackupTableParams.ActionType.CREATE) {
        updateBackupState(false);
      }
    }
    LOG.info("Finished {} task.", getName());
  }
}

/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;


import java.util.UUID;

import com.yugabyte.yw.forms.AbstractTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;

public class UniverseUpdateSucceeded extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseUpdateSucceeded.class);

  // Parameters for marking universe update as a success.
  public static class Params extends AbstractTaskParams {
    // The universe against which this node's details should be saved.
    public UUID universeUUID;
  }

  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ")";
  }

  @Override
  public void run() {
    try {
      LOG.info("Running {}", getName());

      // Create the update lambda.
      UniverseUpdater updater = new UniverseUpdater() {
        @Override
        public void run(Universe universe) {
          // If this universe is not being edited, fail the request.
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          if (!universeDetails.updateInProgress && !universeDetails.backupInProgress) {
            LOG.error("UserUniverse " + taskParams().universeUUID + " is not being edited.");
            throw new RuntimeException("UserUniverse " + taskParams().universeUUID +
                " is not being edited");
          }
          // Set the operation success flag.
          universeDetails.updateSucceeded = true;
          universe.setUniverseDetails(universeDetails);
        }
      };
      // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
      // catch as we want to fail.
      Universe.saveDetails(taskParams().universeUUID, updater);

    } catch (Exception e) {
      String msg = getName() + " failed with exception "  + e.getMessage();
      LOG.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}

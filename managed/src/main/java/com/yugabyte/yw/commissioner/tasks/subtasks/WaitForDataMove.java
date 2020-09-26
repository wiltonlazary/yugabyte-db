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

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.YBClient;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;

import play.api.Play;

public class WaitForDataMove extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(WaitForDataMove.class);

  // The YB client to use.
  private YBClientService ybService;

  // Time to wait (in millisec) during each iteration of load move completion check.
  private static final int WAIT_EACH_ATTEMPT_MS = 100;

  // Number of response errors to tolerate.
  private static final int MAX_ERRORS_TO_IGNORE = 128;

  // Log after these many iterations
  private static final int LOG_EVERY_NUM_ITERS = 100;

  // Parameters for data move wait task.
  public static class Params extends UniverseTaskParams { }

  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public void run() {
    String errorMsg = null;
    YBClient client = null;
    int numErrors = 0;
    double percent = 0;
    int numIters = 0;
    // Get the master addresses and certificate info.
    Universe universe = Universe.get(taskParams().universeUUID);
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificate();
    LOG.info("Running {} on masterAddress = {}.", getName(), masterAddresses);

    try {

      client = ybService.getClient(masterAddresses, certificate);
      LOG.info("Leader Master UUID={}.", client.getLeaderMasterUUID());

      // TODO: Have a mechanism to send this percent to the parent task completion.
      while (percent < (double)100) {
        GetLoadMovePercentResponse response = client.getLoadMoveCompletion();

        if (response.hasError()) {
          LOG.warn("{} response has error {}.", getName(), response.errorMessage());
          numErrors++;
          // If there are more than the threshold of response errors, bail out.
          if (numErrors >= MAX_ERRORS_TO_IGNORE) {
            errorMsg = getName() + ": hit too many errors during data move completion wait.";
            break;
          }
          Thread.sleep(WAIT_EACH_ATTEMPT_MS);
          continue;
        }

        percent = response.getPercentCompleted();
        // No need to wait if completed (as in, percent == 100).
        if (percent < (double)100) {
          Thread.sleep(WAIT_EACH_ATTEMPT_MS);
        }

        numIters++;
        if (numIters % LOG_EVERY_NUM_ITERS == 0) {
          LOG.info("Info: iters={}, percent={}, numErrors={}.", numIters, percent, numErrors);
        }
        // For now, we wait until load moves out fully. TODO: Add an overall timeout as needed.
      }
    } catch (Exception e) {
      LOG.error("{} hit error {}.", getName(), e.getMessage(), e);
      throw new RuntimeException(getName() + " hit error: " , e);
    } finally {
      ybService.closeClient(client, masterAddresses);
    }

    if (errorMsg != null) {
      LOG.error(errorMsg);
      throw new RuntimeException(errorMsg);
    }
  }
}

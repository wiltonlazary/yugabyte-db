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

import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.Customer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.models.Universe;

import java.util.UUID;

public class RemoveUniverseEntry extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(RemoveUniverseEntry.class);
  @Override
  protected DestroyUniverse.Params taskParams() {
    return (DestroyUniverse.Params) taskParams;
  }
  @Override
  public void run() {
    Customer customer = Customer.get(taskParams().customerUUID);
    customer.removeUniverseUUID(taskParams().universeUUID);
    customer.save();
    Universe.delete(taskParams().universeUUID);
  }
}

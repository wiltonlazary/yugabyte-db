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

import java.io.File;
import java.util.Arrays;
import java.util.Base64;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBClient;
import play.api.Play;
import org.yb.util.Pair;

public class EnableEncryptionAtRest extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(EnableEncryptionAtRest.class);

  public static final int KEY_IN_MEMORY_TIMEOUT = 500;

  // The YB client.
  public YBClientService ybService = null;

  // Timeout for failing to respond to pings.
  private static final long TIMEOUT_SERVER_WAIT_MS = 120000;

  public static class Params extends UniverseDefinitionTaskParams {}

  @Override
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
    Universe universe = Universe.get(taskParams().universeUUID);
    String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificate();
    YBClient client = null;
    try {
      LOG.info("Running {}: hostPorts={}.", getName(), hostPorts);
      client = ybService.getClient(hostPorts, certificate);
      EncryptionAtRestManager manager = Play.current()
              .injector()
              .instanceOf(EncryptionAtRestManager.class);
      byte[] currentKeyRef = manager.getCurrentUniverseKeyRef(universe.universeUUID);
      final byte[] currentKeyVal = manager.getCurrentUniverseKey(
              universe.universeUUID,
              currentKeyRef
      );
      final String encodedKeyRef = Base64.getEncoder().encodeToString(currentKeyRef);
      List<HostAndPort> masterAddrs = Arrays
              .stream(hostPorts.split(","))
              .map(addr -> HostAndPort.fromString(addr))
              .collect(Collectors.toList());
      for (HostAndPort hp : masterAddrs) {
        client.addUniverseKeys(ImmutableMap.of(encodedKeyRef, currentKeyVal), hp);
      }
      for (HostAndPort hp : masterAddrs) {
        if (!client
                .waitForMasterHasUniverseKeyInMemory(KEY_IN_MEMORY_TIMEOUT, encodedKeyRef, hp)) {
          throw new RuntimeException(
                  "Timeout occured waiting for universe encryption key to be set in memory"
          );
        }
      }
      client.enableEncryptionAtRestInMemory(encodedKeyRef);
      Pair<Boolean, String> isEncryptionEnabled = client.isEncryptionEnabled();
      if (!isEncryptionEnabled.getFirst() ||
              !isEncryptionEnabled.getSecond().equals(encodedKeyRef)) {
        throw new RuntimeException("Error occured enabling encryption at rest");
      }
    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
  }
}

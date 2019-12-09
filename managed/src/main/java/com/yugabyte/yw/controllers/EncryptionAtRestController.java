/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.KMSConfigTaskParams;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;
import play.mvc.Results;

import com.yugabyte.yw.models.helpers.CommonUtils;

public class EncryptionAtRestController extends AuthenticatedController {
    public static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestController.class);

    @Inject
    EncryptionAtRestManager keyManager;

    @Inject
    Commissioner commissioner;

    public Result createKMSConfig(UUID customerUUID, String keyProvider) {
        LOG.info(String.format(
                "Creating KMS configuration for customer %s with %s",
                customerUUID.toString(),
                keyProvider
        ));
        try {
            TaskType taskType = TaskType.CreateKMSConfig;
            ObjectNode formData = (ObjectNode) request().body().asJson();
            KMSConfigTaskParams taskParams = new KMSConfigTaskParams();
            taskParams.kmsProvider = Enum.valueOf(KeyProvider.class, keyProvider);
            taskParams.providerConfig = formData;
            taskParams.customerUUID = customerUUID;
            taskParams.kmsConfigName = formData.get("name").asText();
            formData.remove("name");
            UUID taskUUID = commissioner.submit(taskType, taskParams);
            LOG.info("Submitted create KMS config for {}, task uuid = {}.", customerUUID, taskUUID);

            // Add this task uuid to the user universe.
            CustomerTask.create(Customer.get(customerUUID),
                    customerUUID,
                    taskUUID,
                    CustomerTask.TargetType.KMSConfiguration,
                    CustomerTask.TaskType.Create,
                    taskParams.getName());
            LOG.info("Saved task uuid " + taskUUID + " in customer tasks table for customer: " +
                    customerUUID);

            ObjectNode resultNode = (ObjectNode) Json.newObject();
            resultNode.put("taskUUID", taskUUID.toString());
            return Results.status(OK, resultNode);
        } catch (Exception e) {
            final String errMsg = "Error caught attempting to create KMS configuration";
            LOG.error(errMsg, e);
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }

    public Result getKMSConfig(UUID customerUUID, UUID configUUID) {
        LOG.info(String.format(
                "Retrieving KMS configuration %s",
                configUUID.toString()
        ));
        KmsConfig config = KmsConfig.get(configUUID);
        EncryptionAtRestService keyService =
                keyManager.getServiceInstance(config.keyProvider.name());
        ObjectNode kmsConfig = keyService.getAuthConfig(configUUID);
        if (kmsConfig == null) {
            return ApiResponse.error(BAD_REQUEST, String.format(
                    "No KMS configuration found for config %s",
                    configUUID.toString()
            ));
        }
        return ApiResponse.success(kmsConfig);
    }

    public Result listKMSConfigs(UUID customerUUID) {
        LOG.info(String.format(
                "Listing KMS configurations for customer %s",
                customerUUID.toString()
        ));
        List<JsonNode> kmsConfigs = KmsConfig.listKMSConfigs(customerUUID)
                .stream()
                .map(configModel -> {
                    ObjectNode result = null;
                    EncryptionAtRestService keyService = keyManager
                            .getServiceInstance(configModel.keyProvider.name());
                    ObjectNode credentials = keyService.getAuthConfig(configModel.configUUID);
                    if (credentials != null) {
                        result = Json.newObject();
                        ObjectNode metadata = Json.newObject();
                        metadata.put("configUUID", configModel.configUUID.toString());
                        metadata.put("provider", configModel.keyProvider.name());
                        metadata.put(
                                "in_use",
                                EncryptionAtRestService.configInUse(configModel.configUUID)
                        );
                        metadata.put("name", configModel.name);
                        result.put("credentials", CommonUtils.maskConfig(credentials));
                        result.put("metadata", metadata);
                    }
                    return result;
                })
                .filter(Objects::nonNull)
                .collect(Collectors.toList());
        return ApiResponse.success(kmsConfigs);
    }

    public Result deleteKMSConfig(UUID customerUUID, UUID configUUID) {
        LOG.info(String.format(
                "Deleting KMS configuration %s for customer %s",
                configUUID.toString(),
                customerUUID.toString()
        ));
        try {
            KmsConfig config = KmsConfig.get(configUUID);
            TaskType taskType = TaskType.DeleteKMSConfig;
            KMSConfigTaskParams taskParams = new KMSConfigTaskParams();
            taskParams.kmsProvider = config.keyProvider;
            taskParams.customerUUID = customerUUID;
            taskParams.configUUID = configUUID;
            UUID taskUUID = commissioner.submit(taskType, taskParams);
            LOG.info("Submitted delete KMS config for {}, task uuid = {}.", customerUUID, taskUUID);

            // Add this task uuid to the user universe.
            CustomerTask.create(Customer.get(customerUUID),
                    customerUUID,
                    taskUUID,
                    CustomerTask.TargetType.KMSConfiguration,
                    CustomerTask.TaskType.Delete,
                    taskParams.getName());
            LOG.info("Saved task uuid " + taskUUID + " in customer tasks table for customer: " +
                    customerUUID);

            ObjectNode resultNode = (ObjectNode) Json.newObject();
            resultNode.put("taskUUID", taskUUID.toString());
            return Results.status(OK, resultNode);
        } catch (Exception e) {
            final String errMsg = "Error caught attempting to delete KMS configuration";
            LOG.error(errMsg, e);
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }

    public Result retrieveKey(UUID customerUUID, UUID universeUUID) {
        LOG.info(String.format(
                "Retrieving universe key for universe %s",
                customerUUID.toString(),
                universeUUID.toString()
        ));
        byte[] keyRef = null;
        byte[] recoveredKey = null;
        try {
            ObjectNode formData = (ObjectNode) request().body().asJson();
            keyRef = Base64.getDecoder().decode(formData.get("reference").asText());
            UUID configUUID = keyManager.getKeyRefKMSConfigUUID(universeUUID, keyRef);
            recoveredKey = keyManager.getCurrentUniverseKey(universeUUID, configUUID, keyRef);
            if (recoveredKey == null || recoveredKey.length == 0) {
                final String errMsg = String.format(
                        "No universe key found for universe %s",
                        universeUUID.toString()
                );
                throw new RuntimeException(errMsg);
            }
            ObjectNode result = Json.newObject()
                    .put("reference", keyRef)
                    .put("value", Base64.getEncoder().encodeToString(recoveredKey));
            return ApiResponse.success(result);
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Could not recover universe key from universe %s",
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }

    public Result getKeyRefHistory(UUID customerUUID, UUID universeUUID) {
        LOG.info(String.format(
                "Retrieving key ref history for customer %s and universe %s",
                customerUUID.toString(),
                universeUUID.toString()
        ));
        try {
            return ApiResponse.success(KmsHistory.getAllTargetKeyRefs(
                    universeUUID,
                    KmsHistoryId.TargetType.UNIVERSE_KEY
            )
                    .stream()
                    .map(history -> {
                        return Json.newObject()
                                .put("reference", history.keyRef)
                                .put("configUUID", history.uuid.configUUID.toString())
                                .put("timestamp", history.timestamp.toString());
                    })
                    .collect(Collectors.toList()));
        } catch (Exception e) {
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }

    public Result removeKeyRefHistory(UUID customerUUID, UUID universeUUID) {
        LOG.info(String.format(
                "Removing key ref for customer %s with universe %s",
                customerUUID.toString(),
                universeUUID.toString()
        ));
        try {
            keyManager.cleanupEncryptionAtRest(customerUUID, universeUUID);
            return ApiResponse.success("Key ref was successfully removed");
        } catch (Exception e) {
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }

    public Result getCurrentKeyRef(UUID customerUUID, UUID universeUUID) {
        LOG.info(String.format(
                "Retrieving key ref for customer %s and universe %s",
                customerUUID.toString(),
                universeUUID.toString()
        ));
        try {
            UUID configUUID = keyManager.getCurrentKMSConfigUUID(universeUUID);
            byte[] keyRef = keyManager.getCurrentUniverseKeyRef(universeUUID, configUUID);
            if (keyRef == null || keyRef.length == 0) {
                return ApiResponse.error(BAD_REQUEST, String.format(
                        "Could not retrieve key service for customer %s and universe %s",
                        customerUUID.toString(),
                        universeUUID.toString()
                ));
            }
            return ApiResponse.success(Json.newObject().put(
                    "reference", Base64.getEncoder().encodeToString(keyRef)
            ));
        } catch (Exception e) {
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }
}

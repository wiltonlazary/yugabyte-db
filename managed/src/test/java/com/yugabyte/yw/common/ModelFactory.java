// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import play.libs.Json;

import java.util.HashSet;
import java.util.Map;
import java.util.UUID;

public class ModelFactory {

  /*
   * Customer creation helpers.
   */

  public static Customer testCustomer() {
    return testCustomer("test@customer.com");
  }
  public static Customer testCustomer(String email) {
    return testCustomer("tc", email);
  }
  public static Customer testCustomer(String code, String email) {
    return Customer.create(code, "Test customer", email, "password");
  }

  /*
   * Provider creation helpers.
   */

  public static Provider awsProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.aws, "Amazon");
  }
  public static Provider gcpProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.gcp, "Google");
  }
  public static Provider onpremProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.onprem, "OnPrem");
  }
  public static Provider kubernetesProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.kubernetes, "Kubernetes");
  }
  public static Provider newProvider(Customer customer, Common.CloudType cloud) {
    return Provider.create(customer.uuid, cloud, cloud.toString());
  }
  public static Provider newProvider(Customer customer, Common.CloudType cloud, Map<String, String> config) {
    return Provider.create(customer.uuid, cloud, cloud.toString(), config);
  }

  /*
   * Universe creation helpers.
   */

  public static Universe createUniverse() {
    return createUniverse("Test Universe", 1L);
  }
  public static Universe createUniverse(String universeName) {
    return createUniverse(universeName, 1L);
  }
  public static Universe createUniverse(long customerId) {
    return createUniverse("Test Universe", customerId);
  }
  public static Universe createUniverse(String universeName, long customerId) {
    return createUniverse(universeName, UUID.randomUUID(), customerId, Common.CloudType.aws);
  }
  public static Universe createUniverse(String universeName, UUID universeUUID) {
    return createUniverse(universeName, universeUUID, 1L, Common.CloudType.aws);
  }
  public static Universe createUniverse(String universeName, long customerId, Common.CloudType cloudType) {
    return createUniverse(universeName, UUID.randomUUID(), 1L, cloudType);
  }
  public static Universe createUniverse(String universeName, UUID universeUUID, long customerId, Common.CloudType cloudType) {
    return createUniverse(universeName, universeUUID, customerId, cloudType, null);
  }
  public static Universe createUniverse(String universeName, UUID universeUUID, long customerId, Common.CloudType cloudType,
                                        PlacementInfo pi) {
    Customer c = Customer.get(customerId);
    // Custom setup a default AWS provider, can be overridden later.
    Provider p = Provider.get(c.uuid, cloudType);
    if (p == null) {
      p = newProvider(c, cloudType);
    }
    UniverseDefinitionTaskParams.UserIntent userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.universeName = universeName;
    userIntent.provider = p.uuid.toString();
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.universeUUID = universeUUID;
    params.nodeDetailsSet = new HashSet<>();
    params.nodePrefix = universeName;
    params.upsertPrimaryCluster(userIntent, pi);
    Universe u = Universe.create(params, customerId);
    c.addUniverseUUID(u.universeUUID);
    c.save();
    return u;
  }

  public static CustomerConfig createS3StorageConfig(Customer customer) {
    JsonNode formData = Json.parse("{\"name\": \"S3\", \"type\": \"STORAGE\", \"data\": " +
        "{\"S3_BUCKET\": \"s3://foo\", \"ACCESS_KEY\": \"A-KEY\", \"ACCESS_SECRET\": \"A-SECRET\"}}");
    return CustomerConfig.createWithFormData(customer.uuid, formData);
  }

  public static CustomerConfig createNfsStorageConfig(Customer customer) {
    JsonNode formData = Json.parse("{\"name\": \"NFS\", \"type\": \"STORAGE\", \"data\": " +
      "{\"NFS_PATH\": \"/foo/bar\"}}");
    return CustomerConfig.createWithFormData(customer.uuid, formData);
  }

  public static CustomerConfig setCallhomeLevel(Customer customer, String level) {
    return CustomerConfig.createCallHomeConfig(customer.uuid, level);
  }

  /*
   * KMS Configuration creation helpers.
   */
  public static KmsConfig createKMSConfig(
          UUID customerUUID,
          String keyProvider,
          ObjectNode authConfig
  ) {
    EncryptionAtRestManager keyManager = new EncryptionAtRestManager();
    EncryptionAtRestService keyService = keyManager.getServiceInstance(keyProvider);
    return keyService.createAuthConfig(
            customerUUID,
            "Test KMS Configuration",
            authConfig
    );
  }
}

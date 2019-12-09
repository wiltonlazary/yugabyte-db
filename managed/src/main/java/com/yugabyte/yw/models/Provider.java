// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.models;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import javax.persistence.CascadeType;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.OneToMany;

import com.avaje.ebean.annotation.DbJson;
import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.Common;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.avaje.ebean.Model;
import play.data.validation.Constraints;
import play.libs.Json;

import static com.yugabyte.yw.models.helpers.CommonUtils.maskConfig;

@Entity
public class Provider extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Provider.class);

  @Id
  public UUID uuid;

  @Column(nullable = false)
  public String code;

  @Column(nullable = false)
  public String name;

  @Column(nullable = false, columnDefinition = "boolean default true")
  public Boolean active = true;
  public Boolean isActive() { return active; }
  public void setActiveFlag(Boolean active) { this.active = active; }

  @Column(nullable = false)
  public UUID customerUUID;

  public void setCustomerUuid(UUID id) {
    this.customerUUID = id;
  }

  @Constraints.Required
  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  private JsonNode config;

  @OneToMany(cascade=CascadeType.ALL)
  @JsonBackReference(value="regions")
  public Set<Region> regions;

  public void setConfig(Map<String, String> configMap) {
    Map<String, String> currConfig = this.getConfig();
    for (String key : configMap.keySet()) {
      currConfig.put(key, configMap.get(key));
    }
    this.config = Json.toJson(currConfig);
    this.save();
  }

  @JsonIgnore
  public JsonNode getMaskedConfig() {
    if (this.config == null) {
      return Json.newObject();
    } else {
      return maskConfig(this.config);
    }
  }

  @JsonIgnore
  public Map<String, String> getConfig() {
    if (this.config == null) {
      return new HashMap();
    } else {
      return Json.fromJson(this.config, Map.class);
    }
  }

  /**
   * Query Helper for Provider with uuid
   */
  public static final Find<UUID, Provider> find = new Find<UUID, Provider>(){};

  /**
   * Create a new Cloud Provider
   * @param customerUUID, customer uuid
   * @param code, code of cloud provider
   * @param name, name of cloud provider
   * @return instance of cloud provider
   */
  public static Provider create(UUID customerUUID, Common.CloudType code, String name) {
    return create(customerUUID, code, name, new HashMap<String, String>());
  }

  /**
   * Create a new Cloud Provider
   * @param customerUUID, customer uuid
   * @param code, code of cloud provider
   * @param name, name of cloud provider
   * @param config, Map of cloud provider configuration
   * @return instance of cloud provider
   */
  public static Provider create(UUID customerUUID, Common.CloudType code, String name, Map<String, String> config) {
    return create(customerUUID, null, code, name, config);
  }

  public static Provider create(UUID customerUUID, UUID providerUUID,
                                Common.CloudType code, String name, Map<String, String> config) {
    Provider provider = new Provider();
    provider.customerUUID = customerUUID;
    provider.uuid = providerUUID;
    provider.code = code.toString();
    provider.name = name;
    provider.setConfig(config);
    provider.save();
    return provider;
  }

  /**
   * Query provider based on customer uuid and provider uuid
   * @param customerUUID, customer uuid
   * @param providerUUID, cloud provider uuid
   * @return instance of cloud provider.
   */
  public static Provider get(UUID customerUUID, UUID providerUUID) {
    return find.where().eq("customer_uuid", customerUUID).idEq(providerUUID).findUnique();
  }

  /**
   * Get all the providers for a given customer uuid
   * @param customerUUID, customer uuid
   * @return list of cloud providers.
   */
  public static List<Provider> getAll(UUID customerUUID) {
    return find.where().eq("customer_uuid", customerUUID).findList();
  }

  /**
   * Get Provider by code for a given customer uuid. If there is multiple
   * providers with the same name, it will raise a exception.
   * @param customerUUID
   * @param code
   * @return
   */
  public static Provider get(UUID customerUUID, Common.CloudType code) {
    List<Provider> providerList = find.where().eq("customer_uuid", customerUUID)
            .eq("code", code.toString()).findList();
    int size = providerList.size();

    if (size == 0) {
      return null;
    } else if (size > 1) {
        throw new RuntimeException("Found " + size + " providers with code: " + code);
    }
    return providerList.get(0);
  }

  public static Provider get(UUID providerUuid) {
    return find.byId(providerUuid);
  }

  public String getAwsHostedZoneId() {
    return getConfig().get("AWS_HOSTED_ZONE_ID");
  }

  public String getAwsHostedZoneName() {
    return getConfig().get("AWS_HOSTED_ZONE_NAME");
  }

  // Update host zone if for aws provider
  public void updateHostedZone(String hostedZoneId, String hostedZoneName) {
    Map<String, String> currentProviderConfig = getConfig();
    currentProviderConfig.put("AWS_HOSTED_ZONE_ID", hostedZoneId);
    currentProviderConfig.put("AWS_HOSTED_ZONE_NAME", hostedZoneName);
    this.setConfig(currentProviderConfig);
    this.save();
  }
}

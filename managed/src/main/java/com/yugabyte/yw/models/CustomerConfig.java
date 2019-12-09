// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.DbJson;
import com.avaje.ebean.annotation.EnumValue;
import com.avaje.ebean.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.libs.Json;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;

@Entity
public class CustomerConfig extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(CustomerConfig.class);
  public static final String ALERTS_PREFERENCES = "preferences";
  public static final String CALLHOME_PREFERENCES = "callhome level";

  public enum ConfigType {
    @EnumValue("STORAGE")
    STORAGE,

    @EnumValue("ALERTS")
    ALERTS,

    @EnumValue("CALLHOME")
    CALLHOME,

    // TODO: move metric and other configs to this table as well.
    @EnumValue("OTHER")
    OTHER;

    public static boolean isValid(String type) {
      for (ConfigType t : ConfigType.values()) {
        if (t.name().equals(type)) {
          return true;
        }
      }

      return false;
    }
  }

  @Id
  public UUID configUUID;

  @Column(nullable = false)
  public UUID customerUUID;

  @Column(length=25, nullable = false)
  public ConfigType type;

  @Column(length=100, nullable = false)
  public String name;

  @Constraints.Required
  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  @JsonIgnore
  public JsonNode data;

  public static final Find<UUID, CustomerConfig> find = new Find<UUID, CustomerConfig>(){};

  public Map<String, String> dataAsMap() {
    return new ObjectMapper().convertValue(data, Map.class);
  }

  public JsonNode getData() {
    // MASK any sensitive data.
    JsonNode maskedData = data.deepCopy();
    for (Iterator<String> it = maskedData.fieldNames(); it.hasNext(); ) {
      String key = it.next();
      if (key.contains("KEY") || key.contains("SECRET")) {
        ((ObjectNode) maskedData).put(key, maskedData.get(key).asText().replaceAll("(?<!^.?).(?!.?$)", "*"));
      }
    }
    return maskedData;
  }

  public static CustomerConfig createWithFormData(UUID customerUUID, JsonNode formData) {
    CustomerConfig customerConfig = Json.fromJson(formData, CustomerConfig.class);
    customerConfig.customerUUID = customerUUID;
    customerConfig.save();
    return customerConfig;
  }

  public static List<CustomerConfig> getAll(UUID customerUUID) {
    return CustomerConfig.find.where().eq("customer_uuid", customerUUID).findList();
  }

  public static CustomerConfig get(UUID customerUUID, UUID configUUID) {
    return CustomerConfig.find.where().eq("customer_uuid", customerUUID).idEq(configUUID).findUnique();
  }

  public static CustomerConfig createAlertConfig(UUID customerUUID, JsonNode payload) {
    CustomerConfig customerConfig = new CustomerConfig();
    customerConfig.type = ConfigType.ALERTS;
    customerConfig.name = ALERTS_PREFERENCES;
    customerConfig.customerUUID = customerUUID;
    customerConfig.data = payload;
    customerConfig.save();
    return customerConfig;
  }

  public static CustomerConfig getAlertConfig(UUID customerUUID) {
    return CustomerConfig.find.where()
      .eq("customer_uuid", customerUUID)
      .eq("type", ConfigType.ALERTS.toString())
      .eq("name", ALERTS_PREFERENCES)
      .findUnique();
  }

  public static CustomerConfig createCallHomeConfig(UUID customerUUID) {
    return createCallHomeConfig(customerUUID, "MEDIUM");
  }

  public static CustomerConfig createCallHomeConfig(UUID customerUUID, String level) {
    CustomerConfig customerConfig = new CustomerConfig();
    customerConfig.type = ConfigType.CALLHOME;
    customerConfig.name = CALLHOME_PREFERENCES;
    customerConfig.customerUUID = customerUUID;
    ObjectNode callhome_json = Json.newObject().put("callhomeLevel", level);
    customerConfig.data = callhome_json;
    customerConfig.save();
    return customerConfig;
  }

  public static CustomerConfig getCallhomeConfig(UUID customerUUID) {
    return CustomerConfig.find.where()
      .eq("customer_uuid", customerUUID)
      .eq("type", ConfigType.CALLHOME.toString())
      .eq("name", CALLHOME_PREFERENCES)
      .findUnique();
  }

  public static CollectionLevel getOrCreateCallhomeLevel(UUID customerUUID){
    CustomerConfig callhomeConfig = CustomerConfig.getCallhomeConfig(customerUUID);
    if (callhomeConfig == null) CustomerConfig.createCallHomeConfig(customerUUID);
    return CustomerConfig.getCallhomeLevel(customerUUID);
  }

  public static CollectionLevel getCallhomeLevel(UUID customerUUID) {
    CustomerConfig config = getCallhomeConfig(customerUUID);
    if (config != null){
      return CollectionLevel.valueOf(config.getData().get("callhomeLevel").textValue());
    }
    return null;
  }

  public static CustomerConfig upsertCallhomeConfig(UUID customerUUID, String callhomeLevel) {
    CustomerConfig callhomeConfig = CustomerConfig.getCallhomeConfig(customerUUID);
    if (callhomeConfig == null) {
      callhomeConfig = CustomerConfig.createCallHomeConfig(customerUUID, callhomeLevel);
    } else {
      callhomeConfig.data = Json.newObject().put("callhomeLevel", callhomeLevel);
      callhomeConfig.update();
    }
    return callhomeConfig;
  }
}

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;

import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.forms.AlertFormData;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;
import play.data.Form;
import play.data.FormFactory;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import play.libs.Json;

import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

import static play.mvc.Http.Status.CONFLICT;

public class AlertController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(AlertController.class);

  @Inject
  FormFactory formFactory;

  /**
   * Lists alerts for given customer.
   */
  public Result list(UUID customerUUID) {
    if (Customer.get(customerUUID) == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    ArrayNode alerts = Json.newArray();
    for (Alert alert: Alert.get(customerUUID)) {
        alerts.add(alert.toJson());
    }
    return ok(alerts);
  }

  /**
   * Upserts alert of specified errCode with new message and createTime. Creates alert if needed.
   * This may only be used to create or update alerts that have 1 or fewer entries in the DB.
   * e.g. Creating two different alerts with errCode='LOW_ULIMITS' and then calling this would
   * error. Creating one alert with errCode=`LOW_ULIMITS` and then calling update would change
   * the previously created alert.
   */
  public Result upsert(UUID customerUUID) {
    if (Customer.get(customerUUID) == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    Form<AlertFormData> formData = formFactory.form(AlertFormData.class).bindFromRequest();
    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }

    AlertFormData data = formData.get();
    List<Alert> alerts = Alert.get(customerUUID, data.errCode);
    if (alerts.size() > 1) {
      return ApiResponse.error(CONFLICT,
        "May only update alerts that have been created once."
        + "Use POST instead to create new alert.");
    } else if (alerts.size() == 1) {
      alerts.get(0).update(data.message);
    } else {
      Alert.create(customerUUID, data.errCode, data.type, data.message);
    }
    return ok();
  }

  /**
   * Creates new alert.
   */
  public Result create(UUID customerUUID) {
    if (Customer.get(customerUUID) == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    Form<AlertFormData> formData = formFactory.form(AlertFormData.class).bindFromRequest();
    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }
    AlertFormData data = formData.get();
    Alert.create(customerUUID, data.errCode, data.type, data.message);
    return ok();
  }
}

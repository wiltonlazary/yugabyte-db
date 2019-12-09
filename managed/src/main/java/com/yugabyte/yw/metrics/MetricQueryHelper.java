// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.metrics;


import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ApiHelper;
import org.joda.time.DateTime;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;
import play.libs.Json;

import java.io.IOException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

@Singleton
public class MetricQueryHelper {
  public static final Logger LOG = LoggerFactory.getLogger(MetricQueryHelper.class);
  public static final Integer STEP_SIZE =  100;
  public static final Integer QUERY_EXECUTOR_THREAD_POOL = 5;
  @Inject
  Configuration appConfig;

  @Inject
  ApiHelper apiHelper;

  @Inject
  YBMetricQueryComponent ybMetricQueryComponent;
  /**
   * Query prometheus for a given metricType and query params
   * @param params, Query params like start, end timestamps, even filters
   *                Ex: {"metricKey": "cpu_usage_user",
   *                     "start": <start timestamp>,
   *                     "end": <end timestamp>}
   * @return MetricQueryResponse Object
   */
  public JsonNode query(List<String> metricKeys, Map<String, String> params) {
    if (metricKeys.isEmpty()) {
      throw new RuntimeException("Empty metricKeys data provided.");
    }

    Long timeDifference;
    if (params.get("end") != null) {
      timeDifference = Long.parseLong(params.get("end")) - Long.parseLong(params.get("start"));
    } else {
      String startTime = params.remove("start").toString();
      Integer endTime = Math.round(DateTime.now().getMillis()/1000);
      params.put("time", startTime);
      params.put("_", endTime.toString());
      timeDifference = endTime - Long.parseLong(startTime);
    }

    if (params.get("step") == null) {
      Integer resolution = Math.round(timeDifference / STEP_SIZE);
      params.put("step", resolution.toString());
    }

    HashMap<String, String> additionalFilters = new HashMap<String, String>();
    if (params.containsKey("filters")) {
      try {
        additionalFilters = new ObjectMapper().readValue(params.get("filters"), HashMap.class);
      } catch (IOException e) {
        throw new RuntimeException("Invalid filter params provided, it should be a hash.");
      }
    }

    ExecutorService threadPool = Executors.newFixedThreadPool(QUERY_EXECUTOR_THREAD_POOL);
    Set<Future<JsonNode>> futures = new HashSet<Future<JsonNode>>();
    for (String metricKey : metricKeys) {
      Map<String, String> queryParams = params;
      queryParams.put("queryKey", metricKey);
      Callable<JsonNode> callable = new MetricQueryExecutor(appConfig, apiHelper,
                                                            queryParams, additionalFilters,
                                                            ybMetricQueryComponent);
      Future<JsonNode> future = threadPool.submit(callable);
      futures.add(future);
    }

    ObjectNode responseJson = Json.newObject();
    for (Future<JsonNode> future : futures) {
      JsonNode response = Json.newObject();
      try {
        response = future.get();
      } catch (InterruptedException e) {
        LOG.error("Error fetching metrics data: {}", e.getMessage());
        e.printStackTrace();
      } catch (ExecutionException e) {
        LOG.error("Error fetching metrics data: {}", e.getMessage());
        e.printStackTrace();
      }

      responseJson.set(response.get("queryKey").asText(), response);
    }
    threadPool.shutdown();
    return responseJson;
  }
}

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

import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.common.kms.services.AwsEARService;
import com.yugabyte.yw.common.kms.services.SmartKeyEARService;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import static com.yugabyte.yw.common.AssertHelper.*;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.test.Helpers.contentAsString;

import java.util.*;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import org.mockito.Mock;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import org.mockito.runners.MockitoJUnitRunner;
import play.Application;
import play.Configuration;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

@RunWith(MockitoJUnitRunner.class)
public class EncryptionAtRestControllerTest extends WithApplication {

    @Mock
    play.Configuration mockAppConfig;
    private Customer customer;
    private Universe universe;
    private String authToken;
    private ApiHelper mockApiHelper;
    private EncryptionAtRestManager mockUtil;

    String mockEncryptionKey = "RjZiNzVGekljNFh5Zmh0NC9FQ1dpM0FaZTlMVGFTbW1Wa1dnaHRzdDhRVT0=";
    String algorithm = "AES";
    int keySize = 256;
    String mockKid = "some_kId";


    @Override
    protected Application provideApplication() {
        mockApiHelper = mock(ApiHelper.class);
        mockUtil = mock(EncryptionAtRestManager.class);
        return new GuiceApplicationBuilder()
                .configure((Map) Helpers.inMemoryDatabase())
                .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
                .overrides(bind(EncryptionAtRestManager.class).toInstance(mockUtil))
                .build();
    }

    @Before
    public void setUp() {
        customer = ModelFactory.testCustomer();
        universe = ModelFactory.createUniverse();
        authToken = customer.createAuthToken();
        String mockApiKey = "some_api_key";
        Map<String, String> authorizationHeaders = ImmutableMap.of(
                "Authorization", String.format("Basic %s", mockApiKey)
        );
        ObjectNode createReqPayload = Json.newObject()
                .put("name", universe.universeUUID.toString())
                .put("obj_type", algorithm)
                .put("key_size", keySize);
        ArrayNode keyOps = Json.newArray()
                .add("EXPORT")
                .add("APPMANAGEABLE");
        createReqPayload.set("key_ops", keyOps);
        Map<String, String> postReqHeaders = ImmutableMap.of(
                "Authorization", String.format("Bearer %s", mockApiKey),
                "Content-Type", "application/json"
        );
        when(mockApiHelper.postRequest(any(String.class), any(ObjectNode.class), any(Map.class)))
                .thenReturn(
                        Json.newObject()
                                .put("kid", mockKid)
                                .put("access_token", "some_access_token")
                );
        Map<String, String> getReqHeaders = ImmutableMap.of(
                "Authorization", String.format("Bearer %s", mockApiKey)
        );
        String getKeyUrl = String.format("https://some_base_url/crypto/v1/keys/%s/export", mockKid);
        when(mockApiHelper.getRequest(any(String.class), any(Map.class)))
                .thenReturn(Json.newObject().put("value", mockEncryptionKey));
        Map<String, String> mockQueryParams = ImmutableMap.of(
                "name", universe.universeUUID.toString(),
                "limit", "1"
        );
        when(mockApiHelper.getRequest(any(String.class), any(Map.class), any(Map.class)))
                .thenReturn(Json.newArray());
        when(mockUtil.getServiceInstance(eq("SMARTKEY"))).thenReturn(new SmartKeyEARService());
        when(mockUtil.getServiceInstance(eq("AWS"))).thenReturn(new AwsEARService());
    }

    @Test
    public void testListKMSConfigs() {
        ModelFactory.createKMSConfig(customer.uuid, "SMARTKEY", Json.newObject());
        String url = "/api/v1/customers/" + customer.uuid + "/kms_configs";
        Result listResult = doRequestWithAuthToken("GET", url, authToken);
        assertOk(listResult);
        JsonNode json = Json.parse(contentAsString(listResult));
        assertTrue(json.isArray());
        assertEquals(1, json.size());
    }

    @Test
    public void testListEmptyConfigList() {
        String url = "/api/v1/customers/" + customer.uuid + "/kms_configs";
        Result listResult = doRequestWithAuthToken("GET", url, authToken);
        assertOk(listResult);
        JsonNode json = Json.parse(contentAsString(listResult));
        assertTrue(json.isArray());
        assertEquals(json.size(), 0);
    }

    @Test
    public void testDeleteConfig() {
        ModelFactory.createKMSConfig(customer.uuid, "SMARTKEY", Json.newObject());
        String url = "/api/v1/customers/" + customer.uuid + "/kms_configs";
        Result listResult = doRequestWithAuthToken("GET", url, authToken);
        assertOk(listResult);
        JsonNode json = Json.parse(contentAsString(listResult));
        assertTrue(json.isArray());
        assertEquals(json.size(), 1);
        UUID kmsConfigUUID = UUID.fromString(
                ((ArrayNode) json)
                        .get(0)
                        .get("metadata")
                        .get("configUUID")
                        .asText()
        );
        url = "/api/v1/customers/" + customer.uuid + "/kms_configs/" + kmsConfigUUID.toString();
        Result deleteResult = doRequestWithAuthToken("DELETE", url, authToken);
        assertOk(deleteResult);
        url = "/api/v1/customers/" + customer.uuid + "/kms_configs";
        listResult = doRequestWithAuthToken("GET", url, authToken);
        assertOk(listResult);
        json = Json.parse(contentAsString(listResult));
        assertTrue(json.isArray());
        assertEquals(json.size(), 0);
    }

    @Ignore("This test passes locally but fails on Jenkins due to Guice not injecting mocked ApiHelper for an unknown reason")
    @Test
    public void testCreateAndRecoverKey() {
        String kmsConfigUrl = "/api/customers/" + customer.uuid + "/kms_configs/SMARTKEY";
        ObjectNode kmsConfigReq = Json.newObject()
                .put("base_url", "some_base_url")
                .put("api_key", "some_api_token");
        Result createKMSResult = doRequestWithAuthTokenAndBody(
                "POST",
                kmsConfigUrl,
                authToken,
                kmsConfigReq
        );
        assertOk(createKMSResult);
        String url = "/api/customers/" + customer.uuid + "/universes/" + universe.universeUUID +
                "/kms/SMARTKEY/create_key";
        ObjectNode createPayload = Json.newObject()
                .put("kms_provider", "SMARTKEY")
                .put("algorithm", algorithm)
                .put("key_size", Integer.toString(keySize));
        Result createKeyResult = doRequestWithAuthTokenAndBody(
                "POST",
                url,
                authToken,
                createPayload
        );
        assertOk(createKeyResult);
        JsonNode json = Json.parse(contentAsString(createKeyResult));
        String keyValue = json.get("value").asText();
        assertEquals(keyValue, mockEncryptionKey);
    }

    @Test
    public void testRecoverKeyNotFound() {
        ModelFactory.createKMSConfig(customer.uuid, "SMARTKEY", Json.newObject());
        String url = "/api/customers/" + customer.uuid + "/universes/" +
                universe.universeUUID + "/kms";
        ObjectNode body = Json.newObject()
                .put("reference", "NzNiYmY5M2UtNWYyNy00NzE3LTgyYTktMTVjYzUzMDIzZWRm");
        Result recoverKeyResult = doRequestWithAuthTokenAndBody("POST", url, authToken, body);
        JsonNode json = Json.parse(contentAsString(recoverKeyResult));
        String expectedErrorMsg = String.format(
                "No universe key found for universe %s",
                universe.universeUUID.toString()
        );
        assertErrorNodeValue(json, expectedErrorMsg);
    }
}

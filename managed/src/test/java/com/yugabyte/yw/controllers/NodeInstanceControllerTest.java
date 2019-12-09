// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.mockito.Matchers.any;
import static org.junit.Assert.*;
import static org.mockito.Mockito.*;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.test.Helpers.contentAsString;

import java.util.Set;
import java.util.LinkedList;
import java.util.UUID;
import java.util.stream.Collectors;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;

import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;

import org.mockito.Mockito;
import play.libs.Json;
import play.mvc.Result;

public class NodeInstanceControllerTest extends FakeDBApplication {
  private final String FAKE_IP = "fake_ip";
  private Customer customer;
  private Provider provider;
  private Region region;
  private AvailabilityZone zone;
  private NodeInstance node;

  ArgumentCaptor<TaskType> taskType;
  ArgumentCaptor<NodeTaskParams> taskParams;


  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("tc", "demo@customer.com");
    provider = ModelFactory.awsProvider(customer);
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    zone = AvailabilityZone.create(region, "az-1", "AZ 1", "subnet-1");

    taskType = ArgumentCaptor.forClass(TaskType.class);
    taskParams = ArgumentCaptor.forClass(NodeTaskParams.class);

    NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceFormData.NodeInstanceData();
    nodeData.ip = FAKE_IP;
    nodeData.region = region.code;
    nodeData.zone = zone.code;
    nodeData.instanceType = "fake_instance_type";
    nodeData.sshUser = "ssh-user";
    node = NodeInstance.create(zone.uuid, nodeData);
    // Give it a name.
    node.setNodeName("fake_name");
    node.save();
  }

  private Result getNode(UUID nodeUuid) {
    String uri = "/api/customers/" + customer.uuid + "/nodes/" + nodeUuid + "/list";
    return FakeApiHelper.doRequest("GET", uri);
  }

  private Result listByZone(UUID zoneUuid) {
    String uri = "/api/customers/" + customer.uuid + "/zones/" + zoneUuid + "/nodes/list";
    return FakeApiHelper.doRequest("GET", uri);
  }

  private Result listByProvider(UUID providerUUID) {
    String uri = "/api/customers/" + customer.uuid + "/providers/" + providerUUID + "/nodes/list";
    return FakeApiHelper.doRequest("GET", uri);
  }

  private Result createNode(UUID zoneUuid) {
    String uri = "/api/customers/" + customer.uuid + "/zones/" + zoneUuid + "/nodes";
    NodeInstanceFormData formData = new NodeInstanceFormData();
    formData.nodes = new LinkedList<>();
    formData.nodes.add(node.getDetails());
    JsonNode body = Json.toJson(formData);
    return FakeApiHelper.doRequestWithBody("POST", uri, body);
  }

  private Result deleteInstance(UUID customerUUID, UUID providerUUID, String instanceIP) {
    String uri= "/api/customers/" + customerUUID + "/providers/" + providerUUID + "/instances/" +
                instanceIP;
    return FakeApiHelper.doRequest("DELETE", uri);
  }

  private Result performNodeAction(UUID customerUUID, UUID universeUUID,
                                   String nodeName, NodeActionType nodeAction, boolean mimicError) {
    String uri = "/api/customers/" + customerUUID + "/universes/" + universeUUID + "/nodes/" +
            nodeName;
    ObjectNode params = Json.newObject();
    if (mimicError) {
      params.put("foo", "bar");
    } else {
      params.put("nodeAction", nodeAction.name());
    }

    return FakeApiHelper.doRequestWithBody("PUT", uri, params);
  }

  private void setInTransitNode(UUID universeUUID) {
    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        NodeDetails node = universeDetails.nodeDetailsSet.iterator().next();
        node.state = NodeState.Removed;
        universe.setUniverseDetails(universeDetails);
      }
    };
    Universe.saveDetails(universeUUID, updater);
  }

  private void checkOk(Result r) { assertEquals(OK, r.status()); }
  private void checkNotOk(Result r, String error) {
    assertNotEquals(OK, r.status());
    if (error != null) {
      JsonNode json = parseResult(r);
      assertEquals(error, json.get("error").asText());
    }
  }

  private void checkNodesMatch(JsonNode queryNode, NodeInstance dbNode) {
    assertEquals(dbNode.nodeUuid.toString(), queryNode.get("nodeUuid").asText());
    assertEquals(dbNode.zoneUuid.toString(), queryNode.get("zoneUuid").asText());
    assertEquals(dbNode.getDetailsJson(), queryNode.get("details").toString());
    assertEquals(dbNode.getDetails().sshUser, queryNode.get("details").get("sshUser").asText());
  }

  private void checkNodeValid(JsonNode nodeAsJson) { checkNodesMatch(nodeAsJson, node); }

  private JsonNode parseResult(Result r) {
    return Json.parse(contentAsString(r));
  }

  @Test
  public void testGetNodeWithValidUuid() {
    Result r = getNode(node.nodeUuid);
    checkOk(r);
    JsonNode json = parseResult(r);
    assertTrue(json.isObject());
    checkNodeValid(json);
  }

  @Test
  public void testGetNodeWithInvalidUuid() {
    Result r = getNode(UUID.randomUUID());
    checkNotOk(r, "Null content");
  }

  @Test
  public void testListByZoneSuccess() {
    Result r = listByZone(zone.uuid);
    checkOk(r);
    JsonNode json = parseResult(r);
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    checkNodeValid(json.get(0));
  }

  @Test
  public void testListByProviderSuccess() {
    Result r = listByProvider(provider.uuid);
    checkOk(r);
    JsonNode json = parseResult(r);
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    checkNodeValid(json.get(0));
  }
  @Test
  public void testListByZoneWrongZone() {
    UUID wrongUuid = UUID.randomUUID();
    Result r = listByZone(wrongUuid);
    String error =
      "Invalid com.yugabyte.yw.models.AvailabilityZoneUUID: " + wrongUuid.toString();
    checkNotOk(r, error);
  }

  @Test
  public void testListByZoneNoFreeNodes() {
    node.inUse = true;
    node.save();
    Result r = listByZone(zone.uuid);
    checkOk(r);

    JsonNode json = parseResult(r);
    assertEquals(0, json.size());

    node.inUse = false;
    node.save();
  }

  @Test
  public void testCreateSuccess() {
    Result r = createNode(zone.uuid);
    checkOk(r);
    JsonNode json = parseResult(r);
    assertThat(json, is(notNullValue()));
    assertTrue(json.isObject());
    JsonNode nodeJson = json.get(FAKE_IP);
    assertThat(nodeJson, is(notNullValue()));
    assertTrue(nodeJson.isObject());

    UUID uuid = UUID.fromString(nodeJson.get("nodeUuid").asText());
    NodeInstance dbNode = NodeInstance.get(uuid);
    assertTrue(dbNode != null);
    checkNodesMatch(nodeJson, dbNode);
  }

  @Test
  public void testCreateFailureInvalidZone() {
    UUID wrongUuid = UUID.randomUUID();
    Result r = createNode(wrongUuid);
    String error =
      "Invalid com.yugabyte.yw.models.AvailabilityZoneUUID: " + wrongUuid.toString();
    checkNotOk(r, error);
  }
  // Test for Delete Instance, use case is only for OnPrem, but test can be validated with AWS provider as well
  @Test
  public void testDeleteInstanceWithValidInstanceIP() {
    Result r = deleteInstance(customer.uuid, provider.uuid, FAKE_IP);
    assertOk(r);
  }

  @Test
  public void testDeleteInstanceWithInvalidProviderValidInstanceIP() {
    UUID invalidProviderUUID = UUID.randomUUID();
    Result r = deleteInstance(customer.uuid, invalidProviderUUID, FAKE_IP);
    assertBadRequest(r, "Invalid Provider UUID: " + invalidProviderUUID);
  }

  @Test
  public void testDeleteInstanceWithValidProviderInvalidInstanceIP() {
    Result r = deleteInstance(customer.uuid, provider.uuid, "abc");
    assertBadRequest(r, "Node Not Found");
  }

  @Test
  public void testDeleteInstanceWithInvalidCustomerUUID() {
    UUID invalidCustomerUUID = UUID.randomUUID();
    Result r = deleteInstance(invalidCustomerUUID, provider.uuid, "random_ip");
    assertEquals(FORBIDDEN, r.status());

    String resultString = contentAsString(r);
    assertEquals(resultString, "Unable To Authenticate Customer");
  }

  @Test
  public void testDeleteInstance() {
    Result r = deleteInstance(customer.uuid, provider.uuid, FAKE_IP);
    assertOk(r);
  }

  @Test
  public void testMissingNodeActionParam() {
    verify(mockCommissioner, times(0)).submit(any(), any());
    Universe u = ModelFactory.createUniverse();
    u = Universe.saveDetails(u.universeUUID,
            ApiUtils.mockUniverseUpdater()
    );
    customer.addUniverseUUID(u.universeUUID);
    customer.save();
    Result r = performNodeAction(customer.uuid, u.universeUUID, "host-n1",
            NodeActionType.DELETE, true);
    assertBadRequest(r, "{\"nodeAction\":[\"This field is required\"]}");
  }

  @Test
  public void testInvalidNodeAction() {
    for (NodeActionType nodeActionType: NodeActionType.values()) {
      Universe u = ModelFactory.createUniverse(nodeActionType.name(), customer.getCustomerId());
      customer.addUniverseUUID(u.universeUUID);
      customer.save();
      verify(mockCommissioner, times(0)).submit(any(), any());
      Result r = performNodeAction(customer.uuid, u.universeUUID, "fake-n1",
              nodeActionType, true);
      assertBadRequest(r, "Invalid Node fake-n1 for Universe");
    }
  }

  @Test
  public void testValidNodeAction() {
    for (NodeActionType nodeActionType: NodeActionType.values()) {
      UUID fakeTaskUUID = UUID.randomUUID();
      when(mockCommissioner.submit(any(TaskType.class),
              any(UniverseDefinitionTaskParams.class)))
              .thenReturn(fakeTaskUUID);
      Universe u = ModelFactory.createUniverse(nodeActionType.name(), customer.getCustomerId());
      u = Universe.saveDetails(u.universeUUID,
              ApiUtils.mockUniverseUpdater()
      );
      customer.addUniverseUUID(u.universeUUID);
      customer.save();
      Result r = performNodeAction(customer.uuid, u.universeUUID, "host-n1",
              nodeActionType, false);
      verify(mockCommissioner, times(1)).submit(taskType.capture(), taskParams.capture());
      assertEquals(nodeActionType.getCommissionerTask(), taskType.getValue());
      assertOk(r);
      JsonNode json = Json.parse(contentAsString(r));
      assertValue(json, "taskUUID", fakeTaskUUID.toString());
      CustomerTask ct = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
      assertNotNull(ct);
      assertEquals(CustomerTask.TargetType.Node, ct.getTarget());
      assertEquals(nodeActionType.getCustomerTask(), ct.getType());
      assertEquals("host-n1", ct.getTargetName());
      Mockito.reset(mockCommissioner);
    }
  }

  @Test
  public void testDisableStopRemove() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class),
         any(UniverseDefinitionTaskParams.class)))
         .thenReturn(fakeTaskUUID);

    Universe u = ModelFactory.createUniverse("disable-stop-remove-rf-3", customer.getCustomerId());
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    setInTransitNode(u.universeUUID);

    Set<NodeDetails> nodes = u.getMasters().stream().filter((n) -> n.state == NodeState.Live).collect(Collectors.toSet());

    NodeDetails curNode = nodes.iterator().next();
    Result invalidRemove = performNodeAction(customer.uuid, u.universeUUID, curNode.nodeName,
                                             NodeActionType.REMOVE, false);
    assertBadRequest(invalidRemove, "Cannot REMOVE " + curNode.nodeName + " as it will under replicate the masters.");

    Result invalidStop = performNodeAction(customer.uuid, u.universeUUID, curNode.nodeName,
                                           NodeActionType.STOP, false);
    assertBadRequest(invalidStop, "Cannot STOP " + curNode.nodeName + " as it will under replicate the masters.");
  }
}

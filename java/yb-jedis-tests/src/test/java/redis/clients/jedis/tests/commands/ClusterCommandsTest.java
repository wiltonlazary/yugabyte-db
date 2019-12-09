package redis.clients.jedis.tests.commands;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;

import java.util.List;

import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.Test;
import org.junit.Ignore;

import redis.clients.jedis.HostAndPort;
import redis.clients.jedis.Jedis;
import redis.clients.jedis.JedisCluster.Reset;
import redis.clients.jedis.tests.HostAndPortUtil;
import redis.clients.jedis.tests.utils.JedisClusterTestUtil;

import org.junit.runner.RunWith;


import redis.clients.jedis.tests.BaseYBClassForJedis;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class ClusterCommandsTest extends BaseYBClassForJedis {
  private static Jedis node1;
  private static Jedis node2;

  private HostAndPort nodeInfo1 = HostAndPortUtil.getClusterServers().get(0);
  private HostAndPort nodeInfo2 = HostAndPortUtil.getClusterServers().get(1);

  @Before
  public void setUp() throws Exception {

    node1 = new Jedis(nodeInfo1.getHost(), nodeInfo1.getPort());
    node1.auth("cluster");
    node1.flushAll();

    node2 = new Jedis(nodeInfo2.getHost(), nodeInfo2.getPort());
    node2.auth("cluster");
    node2.flushAll();
  }

  @After
  public void tearDown() {
    node1.disconnect();
    node2.disconnect();
  }

  @AfterClass
  public static void removeSlots() throws InterruptedException {
    node1.clusterReset(Reset.SOFT);
    node2.clusterReset(Reset.SOFT);
  }

  @Test
  @Ignore public void testClusterSoftReset() {
    node1.clusterMeet("127.0.0.1", nodeInfo2.getPort());
    assertTrue(node1.clusterNodes().split("\n").length > 1);
    node1.clusterReset(Reset.SOFT);
    assertEquals(1, node1.clusterNodes().split("\n").length);
  }

  @Test
  @Ignore public void testClusterHardReset() {
    String nodeId = JedisClusterTestUtil.getNodeId(node1.clusterNodes());
    node1.clusterReset(Reset.HARD);
    String newNodeId = JedisClusterTestUtil.getNodeId(node1.clusterNodes());
    assertNotEquals(nodeId, newNodeId);
  }

  @Test
  @Ignore public void clusterSetSlotImporting() {
    node2.clusterAddSlots(6000);
    String[] nodes = node1.clusterNodes().split("\n");
    String nodeId = nodes[0].split(" ")[0];
    String status = node1.clusterSetSlotImporting(6000, nodeId);
    assertEquals("OK", status);
  }

  @Test
  @Ignore public void clusterNodes() {
    String nodes = node1.clusterNodes();
    assertTrue(nodes.split("\n").length > 0);
  }

  @Test
  @Ignore public void clusterMeet() {
    String status = node1.clusterMeet("127.0.0.1", nodeInfo2.getPort());
    assertEquals("OK", status);
  }

  @Test
  @Ignore public void clusterAddSlots() {
    String status = node1.clusterAddSlots(1, 2, 3, 4, 5);
    assertEquals("OK", status);
  }

  @Test
  @Ignore public void clusterDelSlots() {
    node1.clusterAddSlots(900);
    String status = node1.clusterDelSlots(900);
    assertEquals("OK", status);
  }

  @Test
  @Ignore public void clusterInfo() {
    String info = node1.clusterInfo();
    assertNotNull(info);
  }

  @Test
  @Ignore public void clusterGetKeysInSlot() {
    node1.clusterAddSlots(500);
    List<String> keys = node1.clusterGetKeysInSlot(500, 1);
    assertEquals(0, keys.size());
  }

  @Test
  @Ignore public void clusterSetSlotNode() {
    String[] nodes = node1.clusterNodes().split("\n");
    String nodeId = nodes[0].split(" ")[0];
    String status = node1.clusterSetSlotNode(10000, nodeId);
    assertEquals("OK", status);
  }

  @Test
  @Ignore public void clusterSetSlotMigrating() {
    node1.clusterAddSlots(5000);
    String[] nodes = node1.clusterNodes().split("\n");
    String nodeId = nodes[0].split(" ")[0];
    String status = node1.clusterSetSlotMigrating(5000, nodeId);
    assertEquals("OK", status);
  }

  @Test
  @Ignore public void clusterSlots() {
    // please see cluster slot output format from below commit
    // @see:
    // https://github.com/antirez/redis/commit/e14829de3025ffb0d3294e5e5a1553afd9f10b60
    String status = node1.clusterAddSlots(3000, 3001, 3002);
    assertEquals("OK", status);
    status = node2.clusterAddSlots(4000, 4001, 4002);
    assertEquals("OK", status);

    List<Object> slots = node1.clusterSlots();
    assertNotNull(slots);
    assertTrue(!slots.isEmpty());

    for (Object slotInfoObj : slots) {
      List<Object> slotInfo = (List<Object>) slotInfoObj;
      assertNotNull(slots);
      assertTrue(slots.size() >= 2);

      assertTrue(slotInfo.get(0) instanceof Long);
      assertTrue(slotInfo.get(1) instanceof Long);

      if (slots.size() > 2) {
        // assigned slots
        assertTrue(slotInfo.get(2) instanceof List);
      }
    }
  }

}

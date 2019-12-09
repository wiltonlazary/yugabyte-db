// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.cql;

import org.junit.Test;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;

import com.datastax.driver.core.Session;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.yb.util.RandomNumberUtil;

@RunWith(value=YBTestRunner.class)
public class TestAlterKeyspace extends BaseAuthenticationCQLTest {
  // We use a random keyspace name in each test to be sure that the keyspace does not exist in the
  // beginning of the test.
  private static String getRandomKeyspaceName() {
    return "test_keyspace_" + RandomNumberUtil.randomNonNegNumber();
  }

  private String alterKeyspaceStmt(String keyspaceName, String options) throws Exception {
    return "ALTER KEYSPACE \"" + keyspaceName + "\"" + options;
  }

  private String durableWritesStmt(String value) {
    return String.format("REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3 } " +
            "AND DURABLE_WRITES = %s;", value);
  }

  public void alterKeyspace(String keyspaceName, String options) throws Exception {
    execute(alterKeyspaceStmt(keyspaceName, options));
  }

  private void runInvalidAlterKeyspace(String keyspaceName, String options) throws Exception {
    executeInvalid(alterKeyspaceStmt(keyspaceName, options));
  }

  private void runInvalidKeyspaceProperty(String property) throws Exception {
    runInvalidStmt(alterKeyspaceStmt(DEFAULT_TEST_KEYSPACE, " WITH " + property));
  }

  private void runValidKeyspaceProperty(String property) throws Exception {
    session.execute(alterKeyspaceStmt(DEFAULT_TEST_KEYSPACE, " WITH " + property));
  }

  // Type of resources.
  private static final String ALL_KEYSPACES = "ALL KEYSPACES";
  private static final String KEYSPACE = "KEYSPACE";

  // Permissions.
  private static final String ALL = "ALL";
  private static final String ALTER = "ALTER";

  private void grantPermission(String permission, String resourceType, String resource,
                               String role) throws Exception {
    execute(String.format("GRANT %s ON %s %s TO %s", permission, resourceType, resource, role));
  }

  private Session createRoleAndLogin(String permission, String resourceType, String resource)
      throws Exception {
    final String username = "test_role_" + RandomNumberUtil.randomNonNegNumber();
    final String password = "test_role_password_" + RandomNumberUtil.randomNonNegNumber();

    // Create new user and grant permission.
    testCreateRoleHelperWithSession(username, password, true, false, true, session);
    grantPermission(permission, resourceType, resource, username);
    LOG.info("Starting session with ROLE " + username);
    return getSession(username, password);
  }

  @Test
  public void testSimpleAlterKeyspace() throws Exception {
    LOG.info("--- TEST CQL: SIMPLE ALTER KEYSPACE - Start");
    final String keyspaceName = getRandomKeyspaceName();

    // Non-existing keyspace.
    runInvalidAlterKeyspace(keyspaceName, "");

    createKeyspace(keyspaceName);

    // Alter created keyspace.
    alterKeyspace(keyspaceName, "");
    alterKeyspace(keyspaceName,
        " WITH REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3 }");

    // Unknown strategy.
    runInvalidAlterKeyspace(keyspaceName,
        " WITH REPLICATION = { 'class' : 'MyStrategy', 'replication_factor' : 3 }");

    dropKeyspace(keyspaceName);
    LOG.info("--- TEST CQL: SIMPLE ALTER KEYSPACE - End");
  }

  @Test
  public void testDurableWrites() throws Exception {
    LOG.info("--- TEST CQL: DURABLE WRITES - Start");
    // Invalid cases.
    runInvalidKeyspaceProperty("DURABLE_WRITES = true");
    runInvalidKeyspaceProperty("DURABLE_WRITES = 'true'");
    runInvalidKeyspaceProperty("DURABLE_WRITES = false");
    runInvalidKeyspaceProperty("DURABLE_WRITES = 'false'");
    runInvalidKeyspaceProperty(durableWritesStmt("1"));
    runInvalidKeyspaceProperty(durableWritesStmt("1.0"));
    runInvalidKeyspaceProperty(durableWritesStmt("'a'"));
    runInvalidKeyspaceProperty(durableWritesStmt("{'a' : 1}"));
    runInvalidKeyspaceProperty(durableWritesStmt("{}"));

    // Valid cases.
    runValidKeyspaceProperty(durableWritesStmt("true"));
    runValidKeyspaceProperty(durableWritesStmt("'true'"));
    runValidKeyspaceProperty(durableWritesStmt("false"));
    runValidKeyspaceProperty(durableWritesStmt("'false'"));

    runValidKeyspaceProperty(durableWritesStmt("TRUE"));
    runValidKeyspaceProperty(durableWritesStmt("'TRUE'"));
    runValidKeyspaceProperty(durableWritesStmt("FALSE"));
    runValidKeyspaceProperty(durableWritesStmt("'FALSE'"));
    LOG.info("--- TEST CQL: DURABLE WRITES - End");
  }

  @Test
  public void testReplication() throws Exception {
    LOG.info("--- TEST CQL: REPLICATION - Start");
    // Invalid cases.
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : true }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 1}");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 1.0 }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : {} }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 'a' }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 'a', 'replication_factor' : 3 }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 'SimpleStrategy' }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 'a' }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 'a' }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3.0 }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', " +
            "'replication_factor' : 9223372036854775808e9223372036854775808}");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3, 'dc1' : 1 }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'replication_factor' : 3 }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'd' : 1, 'replication_factor' : 3 }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'dc1' : true }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 9223372036854775808 }");

    // Valid cases.
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3 }");
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : '3' }");
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'dc1' : 3 }");
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'dc1' : 3, 'dc2' : 3 }");
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'dc1' : '3' }");
    LOG.info("--- TEST CQL: REPLICATION - End");
  }

  @Test
  public void testAlterKeyspaceWithPermissions() throws Exception {
    LOG.info("--- TEST CQL: ALTER KEYSPACE WITH PERMISSIONS - Start");
    final String keyspaceName = getRandomKeyspaceName();

    createKeyspace(keyspaceName);

    final String alterStmt = alterKeyspaceStmt(keyspaceName,
            " WITH REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3 }");

    // Alter created keyspace (default 'cassandra' superuser role).
    execute(alterStmt, session);

    // Alter keyspace WITHOUT correct permissions.
    testCreateRoleHelperWithSession("test_role", "test_password", true, false, true, session);
    executeInvalid(alterStmt, getSession("test_role", "test_password"));

    // Permission ALL on ALL KEYSPACES.
    Session s1 = createRoleAndLogin(ALL, ALL_KEYSPACES, "");
    execute(alterStmt, s1);

    // Permission ALTER on ALL KEYSPACES.
    Session s2 = createRoleAndLogin(ALTER, ALL_KEYSPACES, "");
    execute(alterStmt, s2);
    executeInvalid("DROP KEYSPACE \"" + keyspaceName + "\"", s2);

    // Permission ALL on this keyspace.
    Session s3 = createRoleAndLogin(ALL, KEYSPACE, keyspaceName);
    execute(alterStmt, s3);
    executeInvalid("DROP KEYSPACE \"" + DEFAULT_TEST_KEYSPACE + "\"", s3);

    // Permission ALTER on this keyspace.
    Session s4 = createRoleAndLogin(ALTER, KEYSPACE, keyspaceName);
    execute(alterStmt, s4);
    executeInvalid("DROP KEYSPACE \"" + DEFAULT_TEST_KEYSPACE + "\"", s4);
    executeInvalid("DROP KEYSPACE \"" + keyspaceName + "\"", s4);

    dropKeyspace(keyspaceName);
    LOG.info("--- TEST CQL: ALTER KEYSPACE WITH PERMISSIONS - End");
  }
}

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
package org.yb.pgsql;

import org.apache.commons.lang3.RandomUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.postgresql.core.TransactionState;
import org.postgresql.util.PSQLException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.SanitizerUtil;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.*;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgDropDatabase extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgDropDatabase.class);

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  public void CreateDatabaseObjects(Connection cxn) throws Exception {
    try (Statement stmt = cxn.createStatement()) {
      // Execute a simplest SQL statement.
      stmt.execute("SELECT 1");

      // Create simple table.
      String tableName = "test_dropdb";
      String sql;
      createSimpleTable(stmt, tableName);

      // Create index.
      sql = String.format("CREATE INDEX %s_rindex on %s(r)", tableName, tableName);
      stmt.execute(sql);

      // Insert some data.
      for (int i = 0; i < 100; i++) {
        sql = String.format("INSERT INTO %s VALUES (%d, %f, %d, 'value_%d')",
                            tableName, i, 1.5*i, 2*i, i);
        stmt.execute(sql);
      }

      // Create sequence.
      stmt.execute("CREATE SEQUENCE s1");
      sql = String.format("CREATE SEQUENCE s2 OWNED BY %s.vi", tableName);
      stmt.execute(sql);
    }
  }

  @Test
  public void testBasicDropDatabase() throws Exception {
    String dbname = "basic_db";

    // Create database.
    Connection connection0 = createConnection(0);
    try (Statement statement0 = connection0.createStatement()) {
      statement0.execute(String.format("CREATE DATABASE %s", dbname));
    }

    // Creating a few objects in the database.
    Connection connection1 = createConnection(1, dbname);
    CreateDatabaseObjects(connection1);
    connection1.close();

    // Drop the database.
    try (Statement statement0 = connection0.createStatement()) {
      statement0.execute(String.format("DROP DATABASE %s", dbname));
    }

    // Connect should fail as database should not exist.
    try {
      createConnection(2, dbname);
      fail(String.format("Connecting to non-existing database '%s' did not fail", dbname));
    } catch (Exception ex) {
      LOG.info("Expected connection failure", ex);
    }
  }

  @Test
  public void testAsyncDropDatabase() throws Exception {
    String dbname = "async_db";

    // Create database.
    Connection connection0 = createConnection(0);
    try (Statement statement0 = connection0.createStatement()) {
      statement0.execute(String.format("CREATE DATABASE %s", dbname));
      statement0.execute(String.format("DROP DATABASE %s", dbname));
      statement0.execute(String.format("CREATE DATABASE %s", dbname));
    }

    // Creating a few objects in the database.
    Connection connection1 = createConnection(1, dbname);
    CreateDatabaseObjects(connection1);

    // Drop database.
    try (Statement statement0 = connection0.createStatement()) {
      statement0.execute(String.format("DROP DATABASE %s", dbname));
    }

    // Execute statements with connection to the dropped database.
    try {
      CreateDatabaseObjects(connection1);
      fail(String.format("Execute statements in dropped database '%s' did not fail", dbname));
    } catch (Exception ex) {
      LOG.info("Expected connection failure", ex);
    }
  }

  @Test
  public void testRecreateDatabase() throws Exception {
    String dbname = "recreate_db";

    // Create database.
    Connection connection0 = createConnection(0);
    try (Statement statement0 = connection0.createStatement()) {
      statement0.execute(String.format("CREATE DATABASE %s", dbname));
    }

    // Create two connections to different databases on the same node.
    Connection connection1a = createConnection(1, dbname);
    Connection connection1b = createConnection(1);
    CreateDatabaseObjects(connection1a);

    // Create database of the same name.
    try (Statement statement0 = connection0.createStatement()) {
      statement0.execute(String.format("DROP DATABASE %s", dbname));
      statement0.execute(String.format("CREATE DATABASE %s", dbname));
    }

    // Execute statements in old and invalid connection1a.
    try {
      CreateDatabaseObjects(connection1a);
      fail(String.format("Execute statements in dropped database '%s' did not fail", dbname));
    } catch (Exception ex) {
      LOG.info("Expected connection failure", ex);
    }

    // New connect to new database of the same name should pass.
    Connection connection2 = createConnection(2, dbname);
    CreateDatabaseObjects(connection2);
    connection2.close();

    // Dropping the new database of the same name and also testing IF EXISTS clause.
    try (Statement statement1 = connection1b.createStatement()) {
      // Existing database should get dropped.
      statement1.execute(String.format("DROP DATABASE IF EXISTS %s", dbname));

      try {
        statement1.execute(String.format("DROP DATABASE %s", dbname));
      } catch  (Exception ex) {
        LOG.info("Expected error for dropping non-existing database", ex);
      }

      // No error for dropping non-existing database.
      statement1.execute(String.format("DROP DATABASE IF EXISTS %s", dbname));
    }
  }
}

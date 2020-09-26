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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.ResultSet;
import java.sql.Statement;
import java.util.List;
import java.util.Map;

import org.postgresql.util.PSQLException;
import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgTimeout extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgSelect.class);
  private static final int kSlowdownPgsqlAggregateReadMs = 2000;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("TEST_slowdown_pgsql_aggregate_read_ms",
        Integer.toString(kSlowdownPgsqlAggregateReadMs));
    return flagMap;
  }

  @Test
  public void testTimeout() throws Exception {
    Statement statement = connection.createStatement();
    List<Row> allRows = setupSimpleTable("timeouttest");
    String query = "SELECT count(*) FROM timeouttest";

    boolean timeoutEncountered = false;
    try (ResultSet rs = statement.executeQuery(query)) {
    } catch (PSQLException ex) {
      if (ex.getMessage().contains("canceling statement due to statement timeout")) {
        LOG.info("Timeout ERROR");
        timeoutEncountered = true;
      }
    }
    assertEquals(timeoutEncountered, false);

    query = "SET STATEMENT_TIMEOUT=1000";
    statement.execute(query);

    query = "SELECT count(*) FROM timeouttest";
    try (ResultSet rs = statement.executeQuery(query)) {
    } catch (PSQLException ex) {
      if (ex.getMessage().contains("canceling statement due to statement timeout")) {
        LOG.info("Timeout ERROR");
        timeoutEncountered = true;
      }
    }
    assertEquals(timeoutEncountered, true);
    LOG.info("Done with the test");
  }
}

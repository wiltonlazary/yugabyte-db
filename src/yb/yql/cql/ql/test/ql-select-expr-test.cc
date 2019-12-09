//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <thread>
#include <cmath>
#include <limits>

#include "yb/yql/cql/ql/test/ql-test-base.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/decimal.h"
#include "yb/common/jsonb.h"
#include "yb/common/ql_value.h"

DECLARE_bool(test_tserver_timeout);

using std::string;
using std::unique_ptr;
using std::shared_ptr;
using std::numeric_limits;

using strings::Substitute;
using yb::util::Decimal;
using yb::util::DecimalFromComparable;
using yb::util::VarInt;

namespace yb {
namespace ql {

class QLTestSelectedExpr : public QLTestBase {
 public:
  QLTestSelectedExpr() : QLTestBase() {
  }
};

TEST_F(QLTestSelectedExpr, TestAggregateExpr) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Test selecting numeric expressions.";

  // Create the table and insert some value.
  const char *create_stmt =
    "CREATE TABLE test_aggr_expr(h int, r int,"
    "                            v1 bigint, v2 int, v3 smallint, v4 tinyint,"
    "                            v5 float, v6 double, primary key(h, r));";
  CHECK_VALID_STMT(create_stmt);

  // Insert rows whose hash value is '1'.
  CHECK_VALID_STMT("INSERT INTO test_aggr_expr(h, r, v1, v2, v3, v4, v5, v6)"
                   "  VALUES(1, 777, 11, 12, 13, 14, 15, 16);");

  // Insert the rest of the rows, one of which has hash value of '1'.
  int64_t v1_total = 11;
  int32_t v2_total = 12;
  int16_t v3_total = 13;
  int8_t v4_total = 14;
  float v5_total = 15;
  double v6_total = 16;
  for (int i = 1; i < 20; i++) {
    string stmt = strings::Substitute(
        "INSERT INTO test_aggr_expr(h, r, v1, v2, v3, v4, v5, v6)"
        "  VALUES($0, $1, $2, $3, $4, $5, $6, $7);",
        i, i + 1, i + 1000, i + 100, i + 10, i, i + 77.77, i + 999.99);
    CHECK_VALID_STMT(stmt);

    v1_total += (i + 1000);
    v2_total += (i + 100);
    v3_total += (i + 10);
    v4_total += i;
    v5_total += (i + 77.77);
    v6_total += (i + 999.99);
  }

  std::shared_ptr<QLRowBlock> row_block;

  //------------------------------------------------------------------------------------------------
  // Test COUNT() aggregate function.
  {
    // Test COUNT() - Not existing data.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1) "
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_0_row = row_block->row(0);
    CHECK_EQ(sum_0_row.column(0).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(1).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(2).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(3).int64_value(), 0);

    // Test COUNT() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1) "
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(1).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(2).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(3).int64_value(), 1);

    // Test COUNT() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1) "
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 2);
    CHECK_EQ(sum_2_row.column(1).int64_value(), 2);
    CHECK_EQ(sum_2_row.column(2).int64_value(), 2);
    CHECK_EQ(sum_2_row.column(3).int64_value(), 2);

    // Test COUNT() - All rows.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1) "
                     "  FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), 20);
    CHECK_EQ(sum_all_row.column(1).int64_value(), 20);
    CHECK_EQ(sum_all_row.column(2).int64_value(), 20);
    CHECK_EQ(sum_all_row.column(3).int64_value(), 20);
  }

  //------------------------------------------------------------------------------------------------
  // Test SUM() aggregate function.
  {
    // Test SUM() - Not existing data.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_0_row = row_block->row(0);
    CHECK_EQ(sum_0_row.column(0).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(1).int32_value(), 0);
    CHECK_EQ(sum_0_row.column(2).int16_value(), 0);
    CHECK_EQ(sum_0_row.column(3).int8_value(), 0);
    CHECK_EQ(sum_0_row.column(4).float_value(), 0);
    CHECK_EQ(sum_0_row.column(5).double_value(), 0);

    // Test SUM() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 11);
    CHECK_EQ(sum_1_row.column(1).int32_value(), 12);
    CHECK_EQ(sum_1_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_1_row.column(3).int8_value(), 14);
    CHECK_EQ(sum_1_row.column(4).float_value(), 15);
    CHECK_EQ(sum_1_row.column(5).double_value(), 16);

    // Test SUM() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 1012);
    CHECK_EQ(sum_2_row.column(1).int32_value(), 113);
    CHECK_EQ(sum_2_row.column(2).int16_value(), 24);
    CHECK_EQ(sum_2_row.column(3).int8_value(), 15);
    // Comparing floating point for 93.77
    CHECK_GT(sum_2_row.column(4).float_value(), 93.765);
    CHECK_LT(sum_2_row.column(4).float_value(), 93.775);
    // Comparing floating point for 1016.99
    CHECK_GT(sum_2_row.column(5).double_value(), 1016.985);
    CHECK_LT(sum_2_row.column(5).double_value(), 1016.995);

    // Test SUM() - All rows.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), v1_total);
    CHECK_EQ(sum_all_row.column(1).int32_value(), v2_total);
    CHECK_EQ(sum_all_row.column(2).int16_value(), v3_total);
    CHECK_EQ(sum_all_row.column(3).int8_value(), v4_total);
    CHECK_GT(sum_all_row.column(4).float_value(), v5_total - 0.1);
    CHECK_LT(sum_all_row.column(4).float_value(), v5_total + 0.1);
    CHECK_GT(sum_all_row.column(5).double_value(), v6_total - 0.1);
    CHECK_LT(sum_all_row.column(5).double_value(), v6_total + 0.1);
  }

  //------------------------------------------------------------------------------------------------
  // Test MAX() aggregate functions.
  {
    // Test MAX() - Not exist.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_0_row = row_block->row(0);
    CHECK(sum_0_row.column(0).IsNull());
    CHECK(sum_0_row.column(1).IsNull());
    CHECK(sum_0_row.column(2).IsNull());
    CHECK(sum_0_row.column(3).IsNull());
    CHECK(sum_0_row.column(4).IsNull());
    CHECK(sum_0_row.column(5).IsNull());

    // Test MAX() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 11);
    CHECK_EQ(sum_1_row.column(1).int32_value(), 12);
    CHECK_EQ(sum_1_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_1_row.column(3).int8_value(), 14);
    CHECK_EQ(sum_1_row.column(4).float_value(), 15);
    CHECK_EQ(sum_1_row.column(5).double_value(), 16);

    // Test MAX() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6)"
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 1001);
    CHECK_EQ(sum_2_row.column(1).int32_value(), 101);
    CHECK_EQ(sum_2_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_2_row.column(3).int8_value(), 14);
    // Comparing floating point for 78.77
    CHECK_GT(sum_2_row.column(4).float_value(), 78.765);
    CHECK_LT(sum_2_row.column(4).float_value(), 78.775);
    // Comparing floating point for 1000.99
    CHECK_GT(sum_2_row.column(5).double_value(), 1000.985);
    CHECK_LT(sum_2_row.column(5).double_value(), 1000.995);

    // Test MAX() - All rows.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6)"
                     "  FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), 1019);
    CHECK_EQ(sum_all_row.column(1).int32_value(), 119);
    CHECK_EQ(sum_all_row.column(2).int16_value(), 29);
    CHECK_EQ(sum_all_row.column(3).int8_value(), 19);
    float v5_max = 96.77;
    CHECK_GT(sum_all_row.column(4).float_value(), v5_max - 0.1);
    CHECK_LT(sum_all_row.column(4).float_value(), v5_max + 0.1);
    double v6_max = 1018.99;
    CHECK_GT(sum_all_row.column(5).double_value(), v6_max - 0.1);
    CHECK_LT(sum_all_row.column(5).double_value(), v6_max + 0.1);
  }

  //------------------------------------------------------------------------------------------------
  // Test MIN() aggregate functions.
  {
    // Test MIN() - Not exist.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_0_row = row_block->row(0);
    CHECK(sum_0_row.column(0).IsNull());
    CHECK(sum_0_row.column(1).IsNull());
    CHECK(sum_0_row.column(2).IsNull());
    CHECK(sum_0_row.column(3).IsNull());
    CHECK(sum_0_row.column(4).IsNull());
    CHECK(sum_0_row.column(5).IsNull());

    // Test MIN() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 11);
    CHECK_EQ(sum_1_row.column(1).int32_value(), 12);
    CHECK_EQ(sum_1_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_1_row.column(3).int8_value(), 14);
    CHECK_EQ(sum_1_row.column(4).float_value(), 15);
    CHECK_EQ(sum_1_row.column(5).double_value(), 16);

    // Test MIN() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6)"
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 11);
    CHECK_EQ(sum_2_row.column(1).int32_value(), 12);
    CHECK_EQ(sum_2_row.column(2).int16_value(), 11);
    CHECK_EQ(sum_2_row.column(3).int8_value(), 1);
    // Comparing floating point for 15
    CHECK_GT(sum_2_row.column(4).float_value(), 14.9);
    CHECK_LT(sum_2_row.column(4).float_value(), 15.1);
    // Comparing floating point for 16
    CHECK_GT(sum_2_row.column(5).double_value(), 15.9);
    CHECK_LT(sum_2_row.column(5).double_value(), 16.1);

    // Test MIN() - All rows.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6)"
                     "  FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), 11);
    CHECK_EQ(sum_all_row.column(1).int32_value(), 12);
    CHECK_EQ(sum_all_row.column(2).int16_value(), 11);
    CHECK_EQ(sum_all_row.column(3).int8_value(), 1);
    float v5_min = 15;
    CHECK_GT(sum_all_row.column(4).float_value(), v5_min - 0.1);
    CHECK_LT(sum_all_row.column(4).float_value(), v5_min + 0.1);
    double v6_min = 16;
    CHECK_GT(sum_all_row.column(5).double_value(), v6_min - 0.1);
    CHECK_LT(sum_all_row.column(5).double_value(), v6_min + 0.1);
  }
}

TEST_F(QLTestSelectedExpr, TestAggregateExprWithNull) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Test selecting numeric expressions with NULL column 'v2'.";

  // Create the table and insert some value.
  const char *create_stmt =
    "CREATE TABLE test_aggr_expr(h int, r int,"
    "                            v1 bigint, v2 int, v3 smallint, v4 tinyint,"
    "                            v5 float, v6 double, v7 text, primary key(h, r));";
  CHECK_VALID_STMT(create_stmt);

  // Insert rows whose hash value is '1'.
  // v2 = NULL - for all, v1 = NULL - first only, v7 = NULL - except first & second,
  // v3,v4,v5,v6 = NULL second only.
  CHECK_VALID_STMT("INSERT INTO test_aggr_expr(h, r, v3, v4, v5, v6, v7)" // v1, v2 = NULL
                   " VALUES(1, 777, 13, 14, 15, 16, 'aaa');");
  CHECK_VALID_STMT("INSERT INTO test_aggr_expr(h, r, v1, v7)" // v2, v3, v4, v5, v6 = NULL
                   " VALUES(1, 888, 11, 'bbb');");

  // Insert the rest of the rows, one of which has hash value of '1'.
  int64_t v1_total = 11;
  int16_t v3_total = 13;
  int8_t v4_total = 14;
  float v5_total = 15;
  double v6_total = 16;
  for (int i = 1; i < 20; i++) {
    string stmt = strings::Substitute(
        "INSERT INTO test_aggr_expr(h, r, v1, v3, v4, v5, v6)" // v2, v7 = NULL
        " VALUES($0, $1, $2, $3, $4, $5, $6);",
        i, i + 1, i + 1000, i + 10, i, i + 77.77, i + 999.99);
    CHECK_VALID_STMT(stmt);

    v1_total += (i + 1000);
    v3_total += (i + 10);
    v4_total += i;
    v5_total += (i + 77.77);
    v6_total += (i + 999.99);
  }

  std::shared_ptr<QLRowBlock> row_block;

  //------------------------------------------------------------------------------------------------
  // Test COUNT() aggregate function.
  {
    // Test COUNT() - Not existing data.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1), count(v2), count(v7)"
                     " FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_0_row = row_block->row(0);
    CHECK_EQ(sum_0_row.column(0).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(1).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(2).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(3).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(4).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(5).int64_value(), 0);

    // Test COUNT() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1), count(v2), count(v7)"
                     " FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(1).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(2).int64_value(), 1);
    CHECK_EQ(sum_1_row.column(3).int64_value(), 0); // NULL values are not counted.
    CHECK_EQ(sum_1_row.column(4).int64_value(), 0); // NULL values are not counted.
    CHECK_EQ(sum_1_row.column(5).int64_value(), 1);

    // Test COUNT() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1), count(v2), count(v7)"
                     " FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 3);
    CHECK_EQ(sum_2_row.column(1).int64_value(), 3);
    CHECK_EQ(sum_2_row.column(2).int64_value(), 3);
    CHECK_EQ(sum_2_row.column(3).int64_value(), 2);
    CHECK_EQ(sum_2_row.column(4).int64_value(), 0); // NULL values are not counted.
    CHECK_EQ(sum_2_row.column(5).int64_value(), 2);

    // Test COUNT() - All rows.
    CHECK_VALID_STMT("SELECT count(*), count(h), count(r), count(v1), count(v2), count(v7)"
                     " FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), 21);
    CHECK_EQ(sum_all_row.column(1).int64_value(), 21);
    CHECK_EQ(sum_all_row.column(2).int64_value(), 21);
    CHECK_EQ(sum_all_row.column(3).int64_value(), 20);
    CHECK_EQ(sum_all_row.column(4).int64_value(), 0); // NULL values are not counted.
    CHECK_EQ(sum_all_row.column(5).int64_value(), 2);
  }

  //------------------------------------------------------------------------------------------------
  // Test SUM() aggregate function. NOTE: SUM(v7) - is not applicable for TEXT type.
  {
    // Test SUM() - Not existing data.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_0_row = row_block->row(0);
    CHECK_EQ(sum_0_row.column(0).int64_value(), 0);
    CHECK_EQ(sum_0_row.column(1).int32_value(), 0);
    CHECK_EQ(sum_0_row.column(2).int16_value(), 0);
    CHECK_EQ(sum_0_row.column(3).int8_value(), 0);
    CHECK_EQ(sum_0_row.column(4).float_value(), 0);
    CHECK_EQ(sum_0_row.column(5).double_value(), 0);

    // Test SUM() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_1_row = row_block->row(0);
    CHECK_EQ(sum_1_row.column(0).int64_value(), 0); // Only one NULL value.
    CHECK_EQ(sum_1_row.column(1).int32_value(), 0); // NULL values are not counted.
    CHECK_EQ(sum_1_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_1_row.column(3).int8_value(), 14);
    CHECK_EQ(sum_1_row.column(4).float_value(), 15);
    CHECK_EQ(sum_1_row.column(5).double_value(), 16);

    // Test SUM() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 1012);
    CHECK_EQ(sum_2_row.column(1).int32_value(), 0); // NULL values are not counted.
    CHECK_EQ(sum_2_row.column(2).int16_value(), 24);
    CHECK_EQ(sum_2_row.column(3).int8_value(), 15);
    // Comparing floating point for 93.77
    CHECK_GT(sum_2_row.column(4).float_value(), 93.765);
    CHECK_LT(sum_2_row.column(4).float_value(), 93.775);
    // Comparing floating point for 1016.99
    CHECK_GT(sum_2_row.column(5).double_value(), 1016.985);
    CHECK_LT(sum_2_row.column(5).double_value(), 1016.995);

    // Test SUM() - All rows.
    CHECK_VALID_STMT("SELECT sum(v1), sum(v2), sum(v3), sum(v4), sum(v5), sum(v6)"
                     "  FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), v1_total);
    CHECK_EQ(sum_all_row.column(1).int32_value(), 0); // NULL values are not counted.
    CHECK_EQ(sum_all_row.column(2).int16_value(), v3_total);
    CHECK_EQ(sum_all_row.column(3).int8_value(), v4_total);
    CHECK_GT(sum_all_row.column(4).float_value(), v5_total - 0.1);
    CHECK_LT(sum_all_row.column(4).float_value(), v5_total + 0.1);
    CHECK_GT(sum_all_row.column(5).double_value(), v6_total - 0.1);
    CHECK_LT(sum_all_row.column(5).double_value(), v6_total + 0.1);
  }

  //------------------------------------------------------------------------------------------------
  // Test MAX() aggregate functions.
  {
    // Test MAX() - Not exist.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6), max(v7)"
                     " FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_0_row = row_block->row(0);
    CHECK(sum_0_row.column(0).IsNull());
    CHECK(sum_0_row.column(1).IsNull());
    CHECK(sum_0_row.column(2).IsNull());
    CHECK(sum_0_row.column(3).IsNull());
    CHECK(sum_0_row.column(4).IsNull());
    CHECK(sum_0_row.column(5).IsNull());
    CHECK(sum_0_row.column(6).IsNull());

    // Test MAX() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6), max(v7)"
                     " FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_1_row = row_block->row(0);
    CHECK(sum_1_row.column(0).IsNull()); // NULL value.
    CHECK(sum_1_row.column(1).IsNull()); // NULL values.
    CHECK_EQ(sum_1_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_1_row.column(3).int8_value(), 14);
    CHECK_EQ(sum_1_row.column(4).float_value(), 15);
    CHECK_EQ(sum_1_row.column(5).double_value(), 16);
    CHECK_EQ(sum_1_row.column(6).string_value(), "aaa");

    // Test MAX() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6), max(v7)"
                     " FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 1001);
    CHECK(sum_2_row.column(1).IsNull()); // NULL values.
    CHECK_EQ(sum_2_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_2_row.column(3).int8_value(), 14);
    // Comparing floating point for 78.77
    CHECK_GT(sum_2_row.column(4).float_value(), 78.765);
    CHECK_LT(sum_2_row.column(4).float_value(), 78.775);
    // Comparing floating point for 1000.99
    CHECK_GT(sum_2_row.column(5).double_value(), 1000.985);
    CHECK_LT(sum_2_row.column(5).double_value(), 1000.995);
    CHECK_EQ(sum_2_row.column(6).string_value(), "bbb");

    // Test MAX() - All rows.
    CHECK_VALID_STMT("SELECT max(v1), max(v2), max(v3), max(v4), max(v5), max(v6), max(v7)"
                     " FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), 1019);
    CHECK(sum_all_row.column(1).IsNull()); // NULL values.
    CHECK_EQ(sum_all_row.column(2).int16_value(), 29);
    CHECK_EQ(sum_all_row.column(3).int8_value(), 19);
    float v5_max = 96.77;
    CHECK_GT(sum_all_row.column(4).float_value(), v5_max - 0.1);
    CHECK_LT(sum_all_row.column(4).float_value(), v5_max + 0.1);
    double v6_max = 1018.99;
    CHECK_GT(sum_all_row.column(5).double_value(), v6_max - 0.1);
    CHECK_LT(sum_all_row.column(5).double_value(), v6_max + 0.1);
    CHECK_EQ(sum_all_row.column(6).string_value(), "bbb");
  }

  //------------------------------------------------------------------------------------------------
  // Test MIN() aggregate functions.
  {
    // Test MIN() - Not exist.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6), min(v7)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_0_row = row_block->row(0);
    CHECK(sum_0_row.column(0).IsNull());
    CHECK(sum_0_row.column(1).IsNull());
    CHECK(sum_0_row.column(2).IsNull());
    CHECK(sum_0_row.column(3).IsNull());
    CHECK(sum_0_row.column(4).IsNull());
    CHECK(sum_0_row.column(5).IsNull());
    CHECK(sum_0_row.column(6).IsNull());

    // Test MIN() - Where condition provides full primary key.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6), min(v7)"
                     "  FROM test_aggr_expr WHERE h = 1 AND r = 777;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_1_row = row_block->row(0);
    CHECK(sum_1_row.column(0).IsNull()); // NULL value.
    CHECK(sum_1_row.column(1).IsNull()); // NULL values.
    CHECK_EQ(sum_1_row.column(2).int16_value(), 13);
    CHECK_EQ(sum_1_row.column(3).int8_value(), 14);
    CHECK_EQ(sum_1_row.column(4).float_value(), 15);
    CHECK_EQ(sum_1_row.column(5).double_value(), 16);
    CHECK_EQ(sum_1_row.column(6).string_value(), "aaa");

    // Test MIN() - Where condition provides full hash key.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6), min(v7)"
                     "  FROM test_aggr_expr WHERE h = 1;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_2_row = row_block->row(0);
    CHECK_EQ(sum_2_row.column(0).int64_value(), 11);
    CHECK(sum_2_row.column(1).IsNull()); // NULL values.
    CHECK_EQ(sum_2_row.column(2).int16_value(), 11);
    CHECK_EQ(sum_2_row.column(3).int8_value(), 1);
    // Comparing floating point for 15
    CHECK_GT(sum_2_row.column(4).float_value(), 14.9);
    CHECK_LT(sum_2_row.column(4).float_value(), 15.1);
    // Comparing floating point for 16
    CHECK_GT(sum_2_row.column(5).double_value(), 15.9);
    CHECK_LT(sum_2_row.column(5).double_value(), 16.1);
    CHECK_EQ(sum_2_row.column(6).string_value(), "aaa");

    // Test MIN() - All rows.
    CHECK_VALID_STMT("SELECT min(v1), min(v2), min(v3), min(v4), min(v5), min(v6), min(v7)"
                     "  FROM test_aggr_expr;");
    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const QLRow& sum_all_row = row_block->row(0);
    CHECK_EQ(sum_all_row.column(0).int64_value(), 11);
    CHECK(sum_all_row.column(1).IsNull()); // NULL values.
    CHECK_EQ(sum_all_row.column(2).int16_value(), 11);
    CHECK_EQ(sum_all_row.column(3).int8_value(), 1);
    float v5_min = 15;
    CHECK_GT(sum_all_row.column(4).float_value(), v5_min - 0.1);
    CHECK_LT(sum_all_row.column(4).float_value(), v5_min + 0.1);
    double v6_min = 16;
    CHECK_GT(sum_all_row.column(5).double_value(), v6_min - 0.1);
    CHECK_LT(sum_all_row.column(5).double_value(), v6_min + 0.1);
    CHECK_EQ(sum_all_row.column(6).string_value(), "aaa");
  }
}

TEST_F(QLTestSelectedExpr, TestQLSelectNumericExpr) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Test selecting numeric expressions.";

  // Create the table and insert some value.
  const char *create_stmt =
    "CREATE TABLE test_numeric_expr(h1 int primary key,"
    "                               v1 bigint, v2 int, v3 smallint, v4 tinyint,"
    "                               v5 float, v6 double);";
  CHECK_VALID_STMT(create_stmt);
  CHECK_VALID_STMT("INSERT INTO test_numeric_expr(h1, v1, v2, v3, v4, v5, v6)"
                   "  VALUES(1, 11, 12, 13, 14, 15, 16);");

  // Select TTL and WRITETIME.
  // - TTL and WRITETIME are not suppported for primary column.
  CHECK_INVALID_STMT("SELECT TTL(h1) FROM test_numeric_expr WHERE h1 = 1;");
  CHECK_INVALID_STMT("SELECT WRITETIME(h1) FROM test_numeric_expr WHERE h1 = 1;");

  // Test various select.
  std::shared_ptr<QLRowBlock> row_block;

  // Select '*'.
  CHECK_VALID_STMT("SELECT * FROM test_numeric_expr WHERE h1 = 1;");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const QLRow& star_row = row_block->row(0);
  CHECK_EQ(star_row.column(0).int32_value(), 1);
  CHECK_EQ(star_row.column(1).int64_value(), 11);
  CHECK_EQ(star_row.column(2).int32_value(), 12);
  CHECK_EQ(star_row.column(3).int16_value(), 13);
  CHECK_EQ(star_row.column(4).int8_value(), 14);
  CHECK_EQ(star_row.column(5).float_value(), 15);
  CHECK_EQ(star_row.column(6).double_value(), 16);

  // Select expressions.
  CHECK_VALID_STMT("SELECT h1, v1+1, v2+2, v3+3, v4+4, v5+5, v6+6 "
                   "FROM test_numeric_expr WHERE h1 = 1;");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const QLRow& expr_row = row_block->row(0);
  CHECK_EQ(expr_row.column(0).int32_value(), 1);
  CHECK_EQ(expr_row.column(1).int64_value(), 12);
  CHECK_EQ(expr_row.column(2).int64_value(), 14);
  CHECK_EQ(expr_row.column(3).int64_value(), 16);
  CHECK_EQ(expr_row.column(4).int64_value(), 18);
  CHECK_EQ(expr_row.column(5).double_value(), 20);
  CHECK_EQ(expr_row.column(6).double_value(), 22);

  // Select with alias.
  CHECK_VALID_STMT("SELECT v1+1 as one, TTL(v2) as two FROM test_numeric_expr WHERE h1 = 1;");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const QLRow& expr_alias_row = row_block->row(0);
  CHECK_EQ(expr_alias_row.column(0).int64_value(), 12);
  CHECK(expr_alias_row.column(1).IsNull());
}

TEST_F(QLTestSelectedExpr, TestQLSelectToken) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Test selecting numeric expressions.";

  // Create the table and insert some value.
  const char *create_stmt =
      "CREATE TABLE test_select_token(h1 int, h2 double, h3 text, "
      "                               r int, v int, primary key ((h1, h2, h3), r));";

  CHECK_VALID_STMT(create_stmt);

  CHECK_VALID_STMT("INSERT INTO test_select_token(h1, h2, h3, r, v) VALUES (1, 2.0, 'a', 1, 1)");
  CHECK_VALID_STMT("INSERT INTO test_select_token(h1, h2, h3, r, v) VALUES (11, 22.5, 'bc', 1, 1)");

  // Test various selects.
  std::shared_ptr<QLRowBlock> row_block;

  // Get the token for the first row.
  CHECK_VALID_STMT("SELECT token(h1, h2, h3) FROM test_select_token "
      "WHERE h1 = 1 AND h2 = 2.0 AND h3 = 'a';");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  int64_t token1 = row_block->row(0).column(0).int64_value();

  // Check the token value matches the row.
  CHECK_VALID_STMT(Substitute("SELECT h1, h2, h3 FROM test_select_token "
      "WHERE token(h1, h2, h3) = $0", token1));
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const QLRow& row1 = row_block->row(0);
  CHECK_EQ(row1.column(0).int32_value(), 1);
  CHECK_EQ(row1.column(1).double_value(), 2.0);
  CHECK_EQ(row1.column(2).string_value(), "a");

  // Get the token for the second row (also test additional selected columns).
  CHECK_VALID_STMT("SELECT v, token(h1, h2, h3), h3 FROM test_select_token "
      "WHERE h1 = 11 AND h2 = 22.5 AND h3 = 'bc';");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  // Check the other selected columns return expected result.
  CHECK_EQ(row_block->row(0).column(0).int32_value(), 1);
  CHECK_EQ(row_block->row(0).column(2).string_value(), "bc");
  int64_t token2 = row_block->row(0).column(1).int64_value();

  // Check the token value matches the row.
  CHECK_VALID_STMT(Substitute("SELECT h1, h2, h3 FROM test_select_token "
      "WHERE token(h1, h2, h3) = $0", token2));
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const QLRow& row2 = row_block->row(0);
  CHECK_EQ(row2.column(0).int32_value(), 11);
  CHECK_EQ(row2.column(1).double_value(), 22.5);
  CHECK_EQ(row2.column(2).string_value(), "bc");
}

TEST_F(QLTestSelectedExpr, TestQLSelectToJson) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  std::shared_ptr<QLRowBlock> row_block;
  LOG(INFO) << "Test selecting with ToJson() built-in.";

  auto to_json_str = [](const QLValue& value) -> string {
    common::Jsonb jsonb(value.jsonb_value());
    string str;
    CHECK_OK(jsonb.ToJsonString(&str));
    return str;
  };

  // Test various selects.

  // Create the user-defined-type, table with UDT & FROZEN and insert some value.
  CHECK_VALID_STMT("CREATE TYPE udt(v1 int, v2 int)");
  CHECK_VALID_STMT("CREATE TABLE test_udt (h int PRIMARY KEY, s SET<int>, u udt, "
                   "f FROZEN<set<int>>, sf SET<FROZEN<set<int>>>, su SET<FROZEN<udt>>)");
  CHECK_VALID_STMT("INSERT INTO test_udt (h, s, u, f, sf, su) values (1, "
                   "{1,2}, {v1:3,v2:4}, {5,6}, {{7,8}}, {{v1:9,v2:0}})");

  // Apply ToJson() to the key column.
  CHECK_VALID_STMT("SELECT tojson(h) FROM test_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("1", to_json_str(row_block->row(0).column(0)));
  // Apply ToJson() to the SET.
  CHECK_VALID_STMT("SELECT tojson(s) FROM test_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("[1,2]", to_json_str(row_block->row(0).column(0)));
  // Apply ToJson() to the UDT column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("{\"v1\":3,\"v2\":4}", to_json_str(row_block->row(0).column(0)));
  // Apply ToJson() to the FROZEN<SET> column.
  CHECK_VALID_STMT("SELECT tojson(f) FROM test_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("[5,6]", to_json_str(row_block->row(0).column(0)));
  // Apply ToJson() to the SET<FROZEN<SET>> column.
  CHECK_VALID_STMT("SELECT tojson(sf) FROM test_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("[[7,8]]", to_json_str(row_block->row(0).column(0)));
  // Apply ToJson() to the SET<FROZEN<UDT>> column.
  CHECK_VALID_STMT("SELECT tojson(su) FROM test_udt");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("[{\"v1\":9,\"v2\":0}]", to_json_str(row_block->row(0).column(0)));

  CHECK_VALID_STMT("CREATE TABLE test_udt2 (h int PRIMARY KEY, u frozen<udt>)");
  CHECK_VALID_STMT("INSERT INTO test_udt2 (h, u) values (1, {v1:33,v2:44})");
  // Apply ToJson() to the FROZEN<UDT> column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt2");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("{\"v1\":33,\"v2\":44}", to_json_str(row_block->row(0).column(0)));

  CHECK_VALID_STMT("CREATE TABLE test_udt3 (h int PRIMARY KEY, u list<frozen<udt>>)");
  CHECK_VALID_STMT("INSERT INTO test_udt3 (h, u) values (1, [{v1:44,v2:55}, {v1:66,v2:77}])");
  // Apply ToJson() to the LIST<UDT> column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt3");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ("[{\"v1\":44,\"v2\":55},{\"v1\":66,\"v2\":77}]",
            to_json_str(row_block->row(0).column(0)));

  CHECK_VALID_STMT("CREATE TABLE test_udt4 (h int PRIMARY KEY, "
                   "u map<frozen<udt>, frozen<udt>>)");
  CHECK_VALID_STMT("INSERT INTO test_udt4 (h, u) values "
                   "(1, {{v1:44,v2:55}:{v1:66,v2:77}, {v1:88,v2:99}:{v1:11,v2:22}})");
  // Apply ToJson() to the MAP<FROZEN<UDT>:FROZEN<UDT>> column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt4");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(("{\"{\\\"v1\\\":44,\\\"v2\\\":55}\":{\"v1\":66,\"v2\":77},"
             "\"{\\\"v1\\\":88,\\\"v2\\\":99}\":{\"v1\":11,\"v2\":22}}"),
            to_json_str(row_block->row(0).column(0)));

  CHECK_VALID_STMT("CREATE TABLE test_udt5 (h int PRIMARY KEY, "
                   "u map<frozen<list<frozen<udt>>>, frozen<set<frozen<udt>>>>)");
  CHECK_VALID_STMT("INSERT INTO test_udt5 (h, u) values "
                   "(1, {[{v1:44,v2:55}, {v1:66,v2:77}]:{{v1:88,v2:99},{v1:11,v2:22}}})");
  // Apply ToJson() to the MAP<FROZEN<LIST<FROZEN<UDT>>>:FROZEN<SET<FROZEN<UDT>>>> column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt5");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(("{\"[{\\\"v1\\\":44,\\\"v2\\\":55},{\\\"v1\\\":66,\\\"v2\\\":77}]\":"
             "[{\"v1\":11,\"v2\":22},{\"v1\":88,\"v2\":99}]}"),
            to_json_str(row_block->row(0).column(0)));

  CHECK_VALID_STMT("CREATE TABLE test_udt6 (h int PRIMARY KEY, "
                   "u map<frozen<map<frozen<udt>, text>>, frozen<set<frozen<udt>>>>)");
  CHECK_VALID_STMT("INSERT INTO test_udt6 (h, u) values "
                   "(1, {{{v1:11,v2:22}:'text'}:{{v1:55,v2:66},{v1:77,v2:88}}})");
  // Apply ToJson() to the MAP<FROZEN<MAP<FROZEN<UDT>:TEXT>>:FROZEN<SET<FROZEN<UDT>>>>
  // column.
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt6");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  EXPECT_EQ(("{\"{\\\"{\\\\\\\"v1\\\\\\\":11,\\\\\\\"v2\\\\\\\":22}\\\":\\\"text\\\"}\":"
             "[{\"v1\":55,\"v2\":66},{\"v1\":77,\"v2\":88}]}"),
            to_json_str(row_block->row(0).column(0)));

  // Test UDT with case-sensitive field names and names with spaces.
  CHECK_VALID_STMT("CREATE TYPE udt7(v1 int, \"V2\" int, \"v  3\" int, \"V  4\" int)");
  CHECK_VALID_STMT("CREATE TABLE test_udt7 (h int PRIMARY KEY, u udt7)");
  CHECK_VALID_STMT("INSERT INTO test_udt7 (h, u) values "
                   "(1, {v1:11,\"V2\":22,\"v  3\":33,\"V  4\":44})");
  CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt7");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  // Verify that the column names in upper case are double quoted (see the case in Cassandra).
  EXPECT_EQ("{\"\\\"V  4\\\"\":44,\"\\\"V2\\\"\":22,\"v  3\":33,\"v1\":11}",
            to_json_str(row_block->row(0).column(0)));

  // Feature Not Supported: UDT field types cannot refer to other user-defined types.
  // https://github.com/YugaByte/yugabyte-db/issues/1630
  CHECK_INVALID_STMT("CREATE TYPE udt8(i1 int, u1 udt)");
  // CHECK_VALID_STMT("CREATE TABLE test_udt_in_udt (h int PRIMARY KEY, u udt8)");
  // CHECK_VALID_STMT("INSERT INTO test_udt_in_udt (h, u) values (1, {i1:33,u1:{v1:44,v2:55}})");
  // Apply ToJson() to the UDT<UDT> column.
  // CHECK_VALID_STMT("SELECT tojson(u) FROM test_udt_in_udt");
  // row_block = processor->row_block();
  // CHECK_EQ(row_block->row_count(), 1);
  // EXPECT_EQ("{\"i1\":33,\"u1\":{\"v1\":44,\"v2\":55}}",
  //           to_json_str(row_block->row(0).column(0)));
}

TEST_F(QLTestSelectedExpr, TestCastDecimal) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Test selecting with CAST.";

  // Test conversions FROM DECIMAL TO numeric types.

  // Create the table and insert some decimal value.
  CHECK_VALID_STMT("CREATE TABLE num_decimal (pk int PRIMARY KEY, dc decimal)");

  // Invalid values.
  CHECK_INVALID_STMT("INSERT INTO num_decimal (pk, dc) values (1, NaN)");
  CHECK_INVALID_STMT("INSERT INTO num_decimal (pk, dc) values (1, 'NaN')");
  CHECK_INVALID_STMT("INSERT INTO num_decimal (pk, dc) values (1, Infinity)");
  CHECK_INVALID_STMT("INSERT INTO num_decimal (pk, dc) values (1, 'Infinity')");
  CHECK_INVALID_STMT("INSERT INTO num_decimal (pk, dc) values (1, 'a string')");

  CHECK_VALID_STMT("INSERT INTO num_decimal (pk, dc) values (123, 456)");
  // Test various selects.
  {
    CHECK_VALID_STMT("SELECT * FROM num_decimal");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 2);
    ASSERT_EQ(row.column(0).int32_value(), 123);
    EXPECT_EQ(DecimalFromComparable(row.column(1).decimal_value()), Decimal("456"));
  }
  {
    CHECK_VALID_STMT("SELECT dc FROM num_decimal");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("456"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc as int) FROM num_decimal");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).int32_value(), 456);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc as double) FROM num_decimal");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).double_value(), 456.);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc as float) FROM num_decimal");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).float_value(), 456.f);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc as text) FROM num_decimal");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).string_value(), "456");
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc as decimal) FROM num_decimal");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("456"));
  }

  // Test value = MIN_BIGINT = -9,223,372,036,854,775,808 ~= -9.2E+18
  // (Using -9223372036854775807 instead of -9223372036854775808 due to a compiler
  // bug:  https://bugs.llvm.org/show_bug.cgi?id=21095)
  CHECK_VALID_STMT("INSERT INTO num_decimal (pk, dc) values (1, -9223372036854775807)");
  {
    CHECK_VALID_STMT("SELECT dc FROM num_decimal where pk=1");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("-9223372036854775807"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS bigint) FROM num_decimal where pk=1");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).int64_value(), -9223372036854775807LL);
  }
  {
    // INT32 overflow.
    CHECK_VALID_STMT("SELECT CAST(dc AS int) FROM num_decimal where pk=1");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).int32_value(), static_cast<int32_t>(-9223372036854775807LL));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS decimal) FROM num_decimal where pk=1");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
              Decimal("-9223372036854775807"));
  }

  // Test value 123.4E+18 > MAX_BIGINT = 9,223,372,036,854,775,807 ~= 9.2E+18
  CHECK_VALID_STMT("INSERT INTO num_decimal (pk, dc) values (2, 123456789012345678901)");
  {
    CHECK_VALID_STMT("SELECT dc FROM num_decimal where pk=2");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("123456789012345678901"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS decimal) FROM num_decimal where pk=2");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("123456789012345678901"));
  }
  // INT64 overflow.
  CHECK_INVALID_STMT("SELECT CAST(dc AS bigint) FROM num_decimal where pk=2");
  // VARINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(dc AS varint) FROM num_decimal where pk=2");

  // Test an extrim DECIMAL value.
  CHECK_VALID_STMT("INSERT INTO num_decimal (pk, dc) values "
                   "(3, -123123123123456456456456.789789789789123123123123)");
  {
    CHECK_VALID_STMT("SELECT dc FROM num_decimal where pk=3");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("-123123123123456456456456.789789789789123123123123"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS decimal) FROM num_decimal where pk=3");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("-123123123123456456456456.789789789789123123123123"));
  }
  // INT64 overflow.
  CHECK_INVALID_STMT("SELECT CAST(dc AS bigint) FROM num_decimal where pk=3");
  CHECK_INVALID_STMT("SELECT CAST(dc AS int) FROM num_decimal where pk=3");

  // Test a value > MAX_DOUBLE=1.79769e+308.
  CHECK_VALID_STMT("INSERT INTO num_decimal (pk, dc) values (4, 5e+308)");
  {
    CHECK_VALID_STMT("SELECT dc FROM num_decimal where pk=4");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("5e+308"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS decimal) FROM num_decimal where pk=4");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("5e+308"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS float) FROM num_decimal where pk=4");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    // FLOAT overflow = Infinity.
    EXPECT_EQ(row.column(0).float_value(), numeric_limits<float>::infinity());
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS double) FROM num_decimal where pk=4");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    // DOUBLE overflow = Infinity.
    EXPECT_EQ(row.column(0).double_value(), numeric_limits<double>::infinity());
  }
  // Not supported.
  CHECK_INVALID_STMT("SELECT CAST(dc AS varint) FROM num_decimal where pk=4");

  // Test a value > MAX_FLOAT=3.40282e+38.
  CHECK_VALID_STMT("INSERT INTO num_decimal (pk, dc) values (5, 5e+38)");
  {
    CHECK_VALID_STMT("SELECT dc FROM num_decimal where pk=5");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("5e+38"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS decimal) FROM num_decimal where pk=5");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("5e+38"));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS double) FROM num_decimal where pk=5");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).double_value(), 5.e+38);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dc AS float) FROM num_decimal where pk=5");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    // FLOAT overflow = Infinity.
    EXPECT_EQ(row.column(0).float_value(), numeric_limits<float>::infinity());
  }
  // VARINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(dc AS varint) FROM num_decimal where pk=5");

  // Test conversions FROM numeric types TO DECIMAL.

  // Create the table and insert some float value.
  CHECK_VALID_STMT("CREATE TABLE numbers (pk int PRIMARY KEY, flt float, dbl double, vari varint, "
                                         "i8 tinyint, i16 smallint, i32 int, i64 bigint)");
  CHECK_VALID_STMT("INSERT INTO numbers (pk, flt) values (1, 456.7)");
  // Test various selects.
  {
    CHECK_VALID_STMT("SELECT flt FROM numbers");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).float_value(), 456.7f);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(flt as float) FROM numbers");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).float_value(), 456.7f);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(flt as decimal) FROM numbers");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    double num = EXPECT_RESULT(DecimalFromComparable(row.column(0).decimal_value()).ToDouble());
    EXPECT_LT(fabs(num - 456.7), 0.001);
  }
  // Test -MAX_BIGINT=-9223372036854775807
  CHECK_VALID_STMT("INSERT INTO numbers (pk, i64) values (2, -9223372036854775807)");
  {
    CHECK_VALID_STMT("SELECT i64 FROM numbers where pk=2");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).int64_value(), -9223372036854775807LL);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(i64 as bigint) FROM numbers where pk=2");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).int64_value(), -9223372036854775807LL);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(i64 as decimal) FROM numbers where pk=2");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("-9223372036854775807"));
  }
  // Test VARINT:
  CHECK_VALID_STMT("INSERT INTO numbers (pk, vari) values (3, "
                   "-123456789012345678901234567890123456789012345678901234567890)");
  {
    CHECK_VALID_STMT("SELECT vari FROM numbers where pk=3");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).varint_value(), CHECK_RESULT(VarInt::CreateFromString(
        "-123456789012345678901234567890123456789012345678901234567890")));
  }
  {
    CHECK_VALID_STMT("SELECT CAST(vari as decimal) FROM numbers where pk=3");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(DecimalFromComparable(row.column(0).decimal_value()),
        Decimal("-123456789012345678901234567890123456789012345678901234567890"));
  }
  // VARINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(vari as varint) FROM numbers where pk=3");

  // Test MAX_FLOAT=3.40282e+38
  CHECK_VALID_STMT("INSERT INTO numbers (pk, flt) values (4, 3.40282e+38)");
  // Test various selects.
  {
    CHECK_VALID_STMT("SELECT flt FROM numbers where pk=4");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).float_value(), 3.40282e+38f);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(flt as float) FROM numbers where pk=4");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).float_value(), 3.40282e+38f);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(flt as decimal) FROM numbers where pk=4");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    double num = EXPECT_RESULT(DecimalFromComparable(row.column(0).decimal_value()).ToDouble());
    EXPECT_LT(fabs(num - 3.40282e+38), 1e+31);
  }

  // Test MAX_DOUBLE=1.79769e+308
  CHECK_VALID_STMT("INSERT INTO numbers (pk, dbl) values (5, 1.79769e+308)");
  // Test various selects.
  {
    CHECK_VALID_STMT("SELECT dbl FROM numbers where pk=5");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).double_value(), 1.79769e+308);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dbl as double) FROM numbers where pk=5");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    EXPECT_EQ(row.column(0).double_value(), 1.79769e+308);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dbl as decimal) FROM numbers where pk=5");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    double num = EXPECT_RESULT(DecimalFromComparable(row.column(0).decimal_value()).ToDouble());
    EXPECT_EQ(num, 1.79769e+308);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(dbl AS float) FROM numbers where pk=5");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    // FLOAT overflow = Infinity.
    EXPECT_EQ(row.column(0).float_value(), numeric_limits<float>::infinity());
  }
  // VARINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(dbl as varint) FROM numbers where pk=5");
}

TEST_F(QLTestSelectedExpr, TestCastTinyInt) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  LOG(INFO) << "Test selecting with CAST.";

  // Try to convert FROM TINYINT TO a numeric type.

  // Create the table and insert some decimal value.
  CHECK_VALID_STMT("CREATE TABLE num_tinyint (pk int PRIMARY KEY, ti tinyint)");
  CHECK_VALID_STMT("INSERT INTO num_tinyint (pk, ti) values (1, 123)");
  // Test various selects.
  {
    CHECK_VALID_STMT("SELECT * FROM num_tinyint");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 2);
    ASSERT_EQ(row.column(0).int32_value(), 1);
    ASSERT_EQ(row.column(1).int8_value(), 123);
  }
  {
    CHECK_VALID_STMT("SELECT ti FROM num_tinyint");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).int8_value(), 123);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as smallint) FROM num_tinyint");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).int16_value(), 123);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as int) FROM num_tinyint");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).int32_value(), 123);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as bigint) FROM num_tinyint");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).int64_value(), 123);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as double) FROM num_tinyint");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).double_value(), 123.);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as float) FROM num_tinyint");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).float_value(), 123.f);
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as text) FROM num_tinyint");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).string_value(), "123");
  }
  {
    CHECK_VALID_STMT("SELECT CAST(ti as decimal) FROM num_tinyint");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(DecimalFromComparable(row.column(0).decimal_value()), Decimal("123"));
  }
  // VARINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(ti AS varint) FROM num_tinyint");
  // TINYINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(ti as tinyint) FROM num_tinyint");

  // Try value > MAX_TINYINT = 127
  CHECK_INVALID_STMT("INSERT INTO num_tinyint (pk, ti) values (2, 256)");

  // Try to convert FROM a numeric type TO TINYINT.

  // Create the table and insert some float value.
  CHECK_VALID_STMT("CREATE TABLE numbers (pk int PRIMARY KEY, flt float, dbl double, vari varint, "
                                         "i8 tinyint, i16 smallint, i32 int, i64 bigint)");
  CHECK_VALID_STMT("INSERT INTO numbers (pk, flt, dbl, vari, i8, i16, i32, i64) values "
                                       "(1, 456.7, 123.456, 256, 123, 123, 123, 123)");
  // Test various selects.
  {
    CHECK_VALID_STMT("SELECT i8 FROM numbers");
    std::shared_ptr<QLRowBlock> row_block = processor->row_block();
    ASSERT_EQ(row_block->row_count(), 1);
    const QLRow& row = row_block->row(0);
    ASSERT_EQ(row.column_count(), 1);
    ASSERT_EQ(row.column(0).int8_value(), 123);
  }
  // TINYINT is not supported for CAST.
  CHECK_INVALID_STMT("SELECT CAST(i16 as tinyint) FROM numbers");
  CHECK_INVALID_STMT("SELECT CAST(i32 as tinyint) FROM numbers");
  CHECK_INVALID_STMT("SELECT CAST(i64 as tinyint) FROM numbers");
  CHECK_INVALID_STMT("SELECT CAST(flt as tinyint) FROM numbers");
  CHECK_INVALID_STMT("SELECT CAST(dbl as tinyint) FROM numbers");
  CHECK_INVALID_STMT("SELECT CAST(vari as tinyint) FROM numbers");
}

TEST_F(QLTestSelectedExpr, TestTserverTimeout) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());
  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();
  const char *create_stmt = "CREATE TABLE test_table(h int, primary key(h));";
  CHECK_VALID_STMT(create_stmt);
  // Insert a row whose hash value is '1'.
  CHECK_VALID_STMT("INSERT INTO test_table(h) VALUES(1);");
  // Make sure a select statement works.
  CHECK_VALID_STMT("SELECT count(*) FROM test_table WHERE h = 1;");
  // Set a flag to simulate tserver timeout and now check that select produces an error.
  FLAGS_test_tserver_timeout = true;
  CHECK_INVALID_STMT("SELECT count(*) FROM test_table WHERE h = 1;");
}

TEST_F(QLTestSelectedExpr, ScanRangeTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT * FROM test_range WHERE h = 5 AND r1 = 5 AND r2 >= 5 AND r2 <= 6;");
  std::shared_ptr<QLRowBlock> row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 2);
  {
    const QLRow& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
  {
    const QLRow& row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
}

TEST_F(QLTestSelectedExpr, ScanRangeTestReverse) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT(
      "SELECT * FROM test_range WHERE h = 5 AND r1 >= 5 AND r1 <= 6 AND r2 >= 5 AND r2 <= 6 ORDER "
      "BY r1 DESC;");
  std::shared_ptr<QLRowBlock> row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 4);
  {
    const QLRow& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 6);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
  {
    const QLRow& row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 6);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
  {
    const QLRow& row = row_block->row(2);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
  {
    const QLRow& row = row_block->row(3);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
}

TEST_F(QLTestSelectedExpr, ScanRangeTestIncDec) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2)) WITH "
      "CLUSTERING ORDER BY (r1 ASC, r2 DESC);";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT * FROM test_range WHERE h = 5 AND r1 = 5 AND r2 >= 5 AND r2 <= 6;");
  std::shared_ptr<QLRowBlock> row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 2);
  {
    const QLRow& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
  {
    const QLRow& row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
}

TEST_F(QLTestSelectedExpr, ScanRangeTestIncDecReverse) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2)) WITH "
      "CLUSTERING ORDER BY (r1 ASC, r2 DESC);";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT(
      "SELECT * FROM test_range WHERE h = 5 AND r1 >= 5 AND r1 <= 6 AND r2 >= 5 AND r2 <= 6 ORDER "
      "BY r1 DESC;");
  std::shared_ptr<QLRowBlock> row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 4);
  {
    const QLRow& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 6);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
  {
    const QLRow& row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 6);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
  {
    const QLRow& row = row_block->row(2);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
  {
    const QLRow& row = row_block->row(3);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
}

TEST_F(QLTestSelectedExpr, ScanChoicesTest) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      CHECK_VALID_STMT(strings::Substitute(
          "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT * FROM test_range WHERE h = 5 AND r1 in (5) and r2 in (5, 6)");
  std::shared_ptr<QLRowBlock> row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 2);
  {
    const QLRow& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 5);
    CHECK_EQ(row.column(3).int32_value(), 5);
  }
  {
    const QLRow& row = row_block->row(1);
    CHECK_EQ(row.column(0).int32_value(), 5);
    CHECK_EQ(row.column(1).int32_value(), 5);
    CHECK_EQ(row.column(2).int32_value(), 6);
    CHECK_EQ(row.column(3).int32_value(), 6);
  }
}

TEST_F(QLTestSelectedExpr, ScanRangeTestIncDecAcrossHashCols) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2)) WITH "
      "CLUSTERING ORDER BY (r1 ASC, r2 DESC);";
  CHECK_VALID_STMT(create_stmt);

  const int max_h = 48;
  for (int h = 0; h < max_h; h++) {
    for (int r1 = 0; r1 < 10; r1++) {
      for (int r2 = 0; r2 < 10; r2++) {
        CHECK_VALID_STMT(strings::Substitute(
            "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
      }
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT h, r1, r2, payload FROM test_range WHERE r1 = 5 AND r2 > 4 AND r2 < 7;");
  std::shared_ptr<QLRowBlock> row_block = processor->row_block();
  EXPECT_EQ(row_block->row_count(), 2 * max_h);
  vector<bool> seen(max_h, false);
  for (int h = 0; h < row_block->row_count(); h++) {
    const QLRow& row = row_block->row(h);
    LOG(INFO) << "got " << row.ToString();
    seen[row.column(0).int32_value()] = true;
    EXPECT_EQ(row.column(1).int32_value(), 5);
    if (h % 2 == 0) {
      EXPECT_EQ(row.column(2).int32_value(), 6);
      EXPECT_EQ(row.column(3).int32_value(), 6);
    } else {
      EXPECT_EQ(row.column(2).int32_value(), 5);
      EXPECT_EQ(row.column(3).int32_value(), 5);
    }
  }
  CHECK_EQ(seen.size(), max_h);
  for (int h = 0; h < max_h; h++) {
    CHECK_EQ(seen[h], true);
  }
}

TEST_F(QLTestSelectedExpr, ScanChoicesTestIncDecAcrossHashCols) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  TestQLProcessor* processor = GetQLProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char* create_stmt =
      "CREATE TABLE test_range(h int, r1 int, r2 int, payload int, PRIMARY KEY ((h), r1, r2)) WITH "
      "CLUSTERING ORDER BY (r1 ASC, r2 DESC);";
  CHECK_VALID_STMT(create_stmt);

  const int max_h = 48;
  for (int h = 0; h < max_h; h++) {
    for (int r1 = 0; r1 < 10; r1++) {
      for (int r2 = 0; r2 < 10; r2++) {
        CHECK_VALID_STMT(strings::Substitute(
            "INSERT INTO test_range (h, r1, r2, payload) VALUES($0, $1, $2, $2);", h, r1, r2));
      }
    }
  }

  // Checking Row
  CHECK_VALID_STMT("SELECT h, r1, r2, payload FROM test_range WHERE r1 in (5) AND r2 in (5, 6);");
  std::shared_ptr<QLRowBlock> row_block = processor->row_block();
  EXPECT_EQ(row_block->row_count(), 2 * max_h);
  vector<bool> seen(max_h, false);
  for (int h = 0; h < row_block->row_count(); h++) {
    const QLRow& row = row_block->row(h);
    LOG(INFO) << "got " << row.ToString();
    seen[row.column(0).int32_value()] = true;
    EXPECT_EQ(row.column(1).int32_value(), 5);
    if (h % 2 == 0) {
      EXPECT_EQ(row.column(2).int32_value(), 6);
      EXPECT_EQ(row.column(3).int32_value(), 6);
    } else {
      EXPECT_EQ(row.column(2).int32_value(), 5);
      EXPECT_EQ(row.column(3).int32_value(), 5);
    }
  }
  CHECK_EQ(seen.size(), max_h);
  for (int h = 0; h < max_h; h++) {
    CHECK_EQ(seen[h], true);
  }
}

} // namespace ql
} // namespace yb

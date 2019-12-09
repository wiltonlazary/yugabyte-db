
## Creating the table

The table should be created with the `transactions` property enabled. The statement should look something as follows.

```sql
CREATE TABLE IF NOT EXISTS <TABLE_NAME> (...) WITH transactions = { 'enabled' : true };
```

### Java example

Here is an example of how to create a simple key-value table which has two columns with transactions enabled.

```java
String create_stmt =
  String.format("CREATE TABLE IF NOT EXISTS %s (k varchar, v varchar, primary key (k)) " +
                "WITH transactions = { 'enabled' : true };",
                tablename);
```


## Inserting or updating data

You can insert data by performing the sequence of commands inside a `BEGIN TRANSACTION` and `END TRANSACTION` block.

```sql
BEGIN TRANSACTION
  statement 1
  statement 2
END TRANSACTION;
```


### Java example

Here is a code snippet of how you would insert data into this table.

```java
// Insert two key values, (key1, value1) and (key2, value2) as a transaction.
String create_stmt = 
  String.format("BEGIN TRANSACTION" +
                "  INSERT INTO %s (k, v) VALUES (%s, %s);" +
                "  INSERT INTO %s (k, v) VALUES (%s, %s);" +
                "END TRANSACTION;",
                tablename, key1, value1,
                tablename, key2, value2;
```


## Prepare-bind transactions

You can prepare statements with transactions and bind variables to the prepared statements when executing the query.

### Java example

```java
String create_stmt = 
  String.format("BEGIN TRANSACTION" +
                "  INSERT INTO %s (k, v) VALUES (:k1, :v1);" +
                "  INSERT INTO %s (k, v) VALUES (:k1, :v2);" +
                "END TRANSACTION;",
                tablename, key1, value1,
                tablename, key2, value2;
PreparedStatement pstmt = client.prepare(create_stmt);

...

BoundStatement txn1 = pstmt.bind().setString("k1", key1)
                                  .setString("v1", value1)
                                  .setString("k2", key2)
                                  .setString("v2", value2);

ResultSet resultSet = client.execute(txn1);
```

## Sample Java Application

You can find a working example of using transactions with Yugabyte in our [sample applications](../../../quick-start/run-sample-apps/). This application writes out string keys in pairs, with each pair of keys having the same value written as a transaction. There are multiple readers and writers that update and read these pair of keys. The number of reads and writes to perform can be specified as a parameter.

Here is how you can try out this sample application.

```sh
Usage:
  java -jar yb-sample-apps.jar \
    --workload CassandraTransactionalKeyValue \
    --nodes 127.0.0.1:9042

  Other options (with default values):
      [ --num_unique_keys 1000000 ]
      [ --num_reads -1 ]
      [ --num_writes -1 ]
      [ --value_size 0 ]
      [ --num_threads_read 24 ]
      [ --num_threads_write 2 ]
```


Browse the [Java source code for the batch application](https://github.com/yugabyte/yugabyte-db/blob/master/java/yb-loadtester/src/main/java/com/yugabyte/sample/apps/CassandraTransactionalKeyValue.java) to see how everything fits together.

## Note on Linearizability

By default, the original Cassandra Java driver and the YugabyteDB Cassandra Java driver use `com.datastax.driver.core.policies.DefaultRetryPolicy`
which can retry requests upon timeout on client side.

Automatic retries can break linearizability of operations from the client point of view.
Therefore we have added `com.yugabyte.driver.core.policies.NoRetryOnClientTimeoutPolicy` which inherits behavior from DefaultRetryPolicy with one
exception - it results in an error in case the operation times out (with the `OperationTimedOutException`).

Under network partitions, this can lead to the case when client gets a successful response to retried request and treats
the operation as completed, but the value might get overwritten by an older operation due to retries.

To avoid such linearizability issues, use `com.yugabyte.driver.core.policies.NoRetryOnClientTimeoutPolicy` and handle
client timeouts in the application layer.

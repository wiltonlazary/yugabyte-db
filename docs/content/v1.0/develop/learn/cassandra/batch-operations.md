

## Inserting data

To insert data in a batch, prepared statements with the bind values are added to the write batch. This is done in order to reduce repeated statement parsing overhead.

### Java Example

In order to perform a batch insert operation in Java, first create a `BatchStatement` object. Next add the desired number of [prepared and bound insert statements](#) to it. Finally, execute the batch object. This is shown below.

```java
// Create a batch statement object.
BatchStatement batch = new BatchStatement();

// Create a prepared statement object to add to the batch.
PreparedStatement insert = client.prepare("INSERT INTO table (k, v) VALUES (?, ?);");

// Bind values to the prepared statement and add them to the batch.
for (...) {
  batch.add(insert.bind( ... <values for bind variables> ... ));
}

// Execute the batch operation.
ResultSet resultSet = client.execute(batch);
```

## Querying data

Reading more than one row can be achieved in a few different ways. Below is an outline of these.

### Range queries

Range queries are very efficient as the database keeps the data together on disk. Range queries can only be performed on clustering columns of the primary key. In order to perform range queries, the table should have been created with clustering columns. These use-cases generally need have a sort order for some of the primary key columns.

Consider a table which has a hash column `h` and two clustering columns `r1` and `r2`. The following range queries are efficient.

- Query a range of values for `r1` given `h`.

```sql
> SELECT * FROM table WHERE h = '...' AND r1 < '<upper-bound>' AND r1 > '<lower-bound>';
```

- Query a range of values for `r2` given `h` and `r1`.

```sql
> SELECT * FROM table WHERE h = '...' AND r1 = '...' AND r2 < '<upper-bound>' AND r2 > '<lower-bound>';
```

- Query a range of values for `r2` given `h` - **may not be efficient**. This query will need to iterate through all the unique values of `r1` in order to fetch the result and would be less efficient if a key has a lot of values for the `r1` column.

```sql
> SELECT * FROM table WHERE h = '...' AND r2 < '<upper-bound>' AND r2 > '<lower-bound>';
```

- Query a range of values for `r1` without `h` being specified - **may not be efficient**. This query will perform a full scan of the table and would be less efficient if the table is large.

```sql
> SELECT * FROM table WHERE r1 < '<upper-bound>' AND r1 > '<lower-bound>';
```



### IN clause

An `IN` operator allows specifying multiple keys to the `WHERE` clause and does simple batching of the `SELECT` statements.

Consider a table which has a hash column `h` and a clustering column `r`.

- Query a set of values of `h` - this operation will perform the lookups for the various hash keys and return the response. The read queries are batched at a tablet level and executed in parallel. This query will be more efficient than performing each lookup from the application.

```sql
> SELECT * FROM table WHERE h IN ('<value1>', '<value2>', ...);
```

- Query a set of values for `r` given one value of `h` - this query is efficient and will seek to the various values of `r` for the given value of `h`.

```sql
> SELECT * FROM table WHERE h = '...' AND r IN ('<value1>', '<value2>', ...);
```

- Query a set of values for `h` and a set of values for `r`. This query will do point lookups for each combination of the provided `h` and `r` values. For example, if the query specifies 3 values for `h` and 2 values for `r`, there will be 6 lookups performed internally and the result set could have upto 6 rows. 

```sql
> SELECT * FROM table WHERE h IN ('<value1>', '<value2>', ...) AND r IN ('<value1>', '<value2>', ...);
```


## Sample Java Application

You can find a working example of using transactions with Yugabyte in our [sample applications](../../../quick-start/run-sample-apps/). This application writes batched key-value pairs with a configurable number of keys per batch. There are multiple readers and writers running in parallel performing these batch writes.

Here is how you can try out this sample application.

```
Usage:
  java -jar yb-sample-apps.jar \
    --workload CassandraBatchKeyValue \
    --nodes 127.0.0.1:9042

  Other options (with default values):
    [ --num_unique_keys 1000000 ]
    [ --num_reads -1 ]
    [ --num_writes -1 ]
    [ --value_size 0 ]
    [ --num_threads_read 24 ]
    [ --num_threads_write 2 ]
    [ --batch_size 10 ]
    [ --table_ttl_seconds -1 ]
```


Browse the [Java source code for the batch application](https://github.com/yugabyte/yugabyte-db/blob/master/java/yb-loadtester/src/main/java/com/yugabyte/sample/apps/CassandraBatchKeyValue.java) to see how everything fits together.

This page documents backups for YugabyteDB’s [Cassandra compatible YCQL API](../../../api/ycql).

## Schema backup

- Backing up schema for one keyspace

In order to backup the schema for a particular keyspace, run the following command.

```sh
$ cqlsh -e "DESC KEYSPACE <keyspace name>" > schema.cql
```

- Backing up schema for entire cluster

In order to backup the schema for all tables across all keyspaces, run the following command.

```sh
cqlsh -e "DESC SCHEMA" > schema.cql
```

## Data backup

The following command exports all the data from a table in the CSV (comma separated value) format to the output file specified. Each row in the table is written to a separate line in the file with the various column values separated by the delimiter.

- Backing up all columns of the table

All columns of the table are exported by default.

```sh
cqlsh -e "COPY <keyspace>.<table> TO 'data.csv' WITH HEADER = TRUE;"
```

- Backing up select columns of the table

In order to backup selected columns of the table, specify the column names in a list.

```sh
cqlsh -e "COPY <keyspace>.<table> (<column 1 name>, <column 2 name>, ...) TO 'data.csv' WITH HEADER = TRUE;"
```

## Options

- Connecting to a remote host and port

The default host is `127.0.0.1` and the default port is `9042`. You can override these values as shown below.

```
cqlsh -e <command> <host> [<port>]
```

- Copy options

The syntax to specify options in the `COPY TO` command is shown below.

```sql
COPY table_name [( column_list )]
FROM 'file_name'[, 'file2_name', ...] | STDIN
[WITH option = 'value' [AND ...]]
```

There are a number of useful options in the `COPY TO` command used to perform the backup. Some of these are outlined below.


| Option  | Description |
| --------------- | ---------------- |
| DELIMITER | Character used to separate fields, default value is `,`.  |
| HEADER    | Boolean value (`true` or `false`). If true, inserts the column names in the first row of data on exports. Default value is `false`. |
| PAGESIZE | Page size for fetching results. Default value is `1000`. |
| PAGETIMEOUT | Page timeout for fetching results. Default value is `10`. |
| MAXREQUESTS | Maximum number of requests each worker can process in parallel. Default value is `6`. |
| MAXOUTPUTSIZE | Maximum size of the output file, measured in number of lines. When set, the output file is split into segment when the value is exceeded. Use `-1` for no maximum. Default value: `-1`. |


## Example

We are going to use the example shown in the [quick start](../../../quick-start/test-cassandra/) section in order to demonstrate how to perform backups.

This section assumes you already have a YugabyteDB cluster. You can install a local cluster on your laptop using [these quick start instructions](../../../quick-start/install/).

### Create a table with data

Create a keyspace for the stock ticker app.

```sql
cqlsh> CREATE KEYSPACE myapp;
```

Create the stock ticker table.

```sql
cqlsh> CREATE TABLE myapp.stock_market (
    stock_symbol text,
    ts text,
    current_price float,
    PRIMARY KEY (stock_symbol, ts)
);
```

Insert some sample data.

```sql
cqlsh> INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('AAPL','2017-10-26 09:00:00',157.41);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('AAPL','2017-10-26 10:00:00',157);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('FB','2017-10-26 09:00:00',170.63);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('FB','2017-10-26 10:00:00',170.1);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('GOOG','2017-10-26 09:00:00',972.56);
INSERT INTO myapp.stock_market (stock_symbol,ts,current_price) VALUES ('GOOG','2017-10-26 10:00:00',971.91);
```

You can query all the 6 rows we inserted by running the following command in `cqlsh`.

```sql
cqlsh> SELECT * FROM myapp.stock_market;
```
```
 stock_symbol | ts                  | current_price
--------------+---------------------+---------------
         GOOG | 2017-10-26 09:00:00 |        972.56
         GOOG | 2017-10-26 10:00:00 |     971.90997
         AAPL | 2017-10-26 09:00:00 |        157.41
         AAPL | 2017-10-26 10:00:00 |           157
           FB | 2017-10-26 09:00:00 |        170.63
           FB | 2017-10-26 10:00:00 |     170.10001

(6 rows)
```

### Backup the schema

Run the following in order to backup the schema of the keyspace `myapp`.

```sh
$ cqlsh -e "DESC KEYSPACE myapp" > myapp_schema.cql
```


The schema of the keyspace `myapp` along with the tables in it are saved to the file `myapp_schema.cql`.

```sh
$ cat myapp_schema.cql
```
```
CREATE KEYSPACE myapp WITH replication = {'class': 'SimpleStrategy', 'replication_factor': '3'}  AND durable_writes = true;

CREATE TABLE myapp.stock_market (
    stock_symbol text,
    ts text,
    current_price float,
    PRIMARY KEY (stock_symbol, ts)
) WITH CLUSTERING ORDER BY (ts ASC)
    AND default_time_to_live = 0;
```


### Backing up all the columns of the table

Run the following command in order to backup the data in the table `myapp.stock_market`.

```sh
$ cqlsh -e "COPY myapp.stock_market TO 'myapp_data.csv' WITH HEADER = TRUE ;"
```


All columns of the rows in the table `myapp.stock_market` are saved to the file `myapp_data.csv`.

```
$ cat myapp_data.csv
stock_symbol,ts,current_price
AAPL,2017-10-26 09:00:00,157.41
AAPL,2017-10-26 10:00:00,157
FB,2017-10-26 09:00:00,170.63
FB,2017-10-26 10:00:00,170.10001
GOOG,2017-10-26 09:00:00,972.56
GOOG,2017-10-26 10:00:00,971.90997
```

### Backing up some columns of the table

In order to backup a subset of columns, you can specify them in the backup command. In the example below, the `stock_symbol` and `ts` columns are backed up, while the `current_price` column is not.

```sh
$ cqlsh -e "COPY myapp.stock_market (stock_symbol, ts) TO 'myapp_data_partial.csv' WITH HEADER = TRUE ;"
```


The selected columns (`stock_symbol` and `ts`) of the rows in the table `myapp.stock_market` are saved to the file `myapp_data_partial.csv`.

```
$ cat myapp_data_partial.csv
stock_symbol,ts
AAPL,2017-10-26 09:00:00
AAPL,2017-10-26 10:00:00
FB,2017-10-26 09:00:00
FB,2017-10-26 10:00:00
GOOG,2017-10-26 09:00:00
GOOG,2017-10-26 10:00:00
```

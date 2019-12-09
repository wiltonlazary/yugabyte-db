---
title: Kafka Connect YugabyteDB
linkTitle: Kafka Connect YugabyteDB
description: Kafka Connect YugabyteDB
section: REFERENCE
menu:
  latest:
    identifier: kafka-connect-yugabytedb
    parent: connectors
    weight: 2930
---

There are two approaches of integrating [YugabyteDB](https://github.com/yugabyte/yugabyte-db) with Apache Kafka. Kafka provides [Kafka Connect](https://docs.confluent.io/3.0.0/connect/intro.html), a connector SDK for building such integrations.

<img src="https://raw.githubusercontent.com/yugabyte/yb-kafka-connector/master/logos/dsql-kafka.png" align="center" alt="Kafka Connect YugabyteDB Connector Architecture"/>

## Kafka Connect YugabyteDB Source Connector

The Kafka Connect YugabyteDB source connector streams table updates in YugabyteDB to Kafka topics. It is based on YugabyteDB's Change Data Capture (CDC) feature. CDC allows the connector to simply subscribe to these table changes and then publish the changes to selected Kafka topics.

## Kafka Connect YugabyteDB Sink Connector

The Kafka Connect YugabyteDB Sink Connector delivers data from Kafka topics into YugabyteDB tables. The connector subscribes to specific topics in Kafka and then writes to specific tables in YugabyteDB as soon as new messages are received in the selected topics.

### Prerequisites

For building and using this project, the following tools must be installed on your system.

- JDK - 1.8+
- Maven - 3.3+
- Clone this repo into `~/yb-kafka/yb-kafka-connector/` directory.

### Steps to set up and run the Kafka Connect YugabyteDB Sink Connector

1. Set up and start Kafka

Download the Apache Kafka `tar` file.

    ```sh
    mkdir -p ~/yb-kafka
    cd ~/yb-kafka
    wget http://apache.cs.utah.edu/kafka/2.0.0/kafka_2.11-2.0.0.tgz
    tar -xzf kafka_2.11-2.0.0.tgz
    ```
Any latest version can be used — this is an example.

2. Start Zookeeper and Kafka server

    ```sh
    ~/yb-kafka/kafka_2.11-2.0.0/bin/zookeeper-server-start.sh config/zookeeper.properties &
    ~/yb-kafka/kafka_2.11-2.0.0/bin/kafka-server-start.sh config/server.properties &
    ```

3. Create a Kafka topic.

    ```
    $ ~/yb-kafka/kafka_2.11-2.0.0/bin/kafka-topics.sh --create --zookeeper localhost:2181--replication-factor 1 --partitions 1 --topic test
    ```
    This needs to be done only once.
     
4. Run the following to produce data in that topic:

    ```sh
    $ ~/yb-kafka/kafka_2.11-2.0.0/bin/kafka-console-producer.sh --broker-list localhost:9092--topic test_topic
    ```

5. Just cut-and-paste the following lines at the prompt:
     
     ```
     {"key" : "A", "value" : 1, "ts" : 1541559411000}
     {"key" : "B", "value" : 2, "ts" : 1541559412000}
     {"key" : "C", "value" : 3, "ts" : 1541559413000}
     ```
     Feel free to `Ctrl-C` this process or switch to a different shell as more values can be added later as well to the same topic.

2. Install YugabyteDB and create the database table.

    [Install YugabyteDB and start a local cluster](https://docs.yugabyte.com/quick-start/install/).
    Create a database and table by running the following command. You can find `cqlsh` in the `bin`  subdirectory located inside the YugabyteDB installation folder.

    ```postgresql
    yugabyte=# CREATE DATABASE IF NOT EXISTS demo;
    yugabyte=# \c demo
    demo=# CREATE TABLE demo.test_table (key text, value bigint, ts timestamp, PRIMARY KEY (key));
    ```

3. Set up and run the Kafka Connect Sink

    Setup the required jars needed by connect

    ```sh
    $ cd ~/yb-kafka/yb-kafka-connector/
    mvn clean install -DskipTests
    $ cp  ~/yb-kafka/yb-kafka-connector/target/yb-kafka-connnector-1.0.0.jar ~/yb-kafkakafka_2.11-2.0.0/libs/
    $ cd ~/yb-kafka/kafka_2.11-2.0.0/libs/
    $ wget http://central.maven.org/maven2/io/netty/netty-all/4.1.25.Finalnetty-all-4.1.25.Final.jar
    $ wget http://central.maven.org/maven2/com/yugabyte/cassandra-driver-core/3.2.0-yb-18cassandra-driver-core-3.2.0-yb-18.jar
    $ wget http://central.maven.org/maven2/com/codahale/metrics/metrics-core/3.0.1metrics-core-3.0.1.jar
    ```

    Finally, run the Kafka Connect YugabyteDB Sink Connector in standalone mode:

    ```sh
    $ ~/yb-kafka/kafka_2.11-2.0.0/bin/connect-standalone.sh ~/yb-kafka/yb-kafka-connector/resourcesexamples/kafka.connect.properties ~/yb-kafka/yb-kafka-connector/resources/examplesyugabyte.sink.properties 
    ```

    *Notes*:

    - Setting the `bootstrap.servers` to a remote host/ports in the`kafka.connect.properties`file can help connect to any accessible existing Kafka cluster.
    - The `database` and `tablename` values in the `yugabyte.sink.properties` file should match the values in the ysqlsh commands in step 5.
    - The `topics` value should match the topic name from producer in step 6.
    - Setting the `yugabyte.cql.contact.points` to a non-local list of host and ports will help connect to any remote-accessible existing YugabyteDB cluster.
   - Check the console output (optional).

     You should see something like this (relevant lines from `YBSinkTask.java`) on the console:

    ```
    [2018-10-28 16:24:16,037] INFO Start with keyspace=demo, table=test_table(com.yb.connect.sink.YBSinkTask:79)
    [2018-10-28 16:24:16,054] INFO Connecting to nodes: /127.0.0.1:9042,/127.0.0.2:9042,127.0.0.3:9042 (com.yb.connect.sink.YBSinkTask:189)
    [2018-10-28 16:24:16,517] INFO Connected to cluster: cluster1(com.yb.connect.sink.YBSinkTask:155)
    [2018-10-28 16:24:16,594] INFO Processing 3 records from Kafka.(com.yb.connect.sink.YBSinkTask:95)
    [2018-10-28 16:24:16,602] INFO Insert INSERT INTO demo.test_table(key,ts,value) VALUES (?,?,? (com.yb.connect.sink.YBSinkTask:439)
    [2018-10-28 16:24:16,612] INFO Prepare SinkRecord ...
    [2018-10-28 16:24:16,618] INFO Bind 'ts' of type timestamp(com.yb.connect.sink.YBSinkTask:255)
    ...
    ```

4. Confirm that the rows are in the target table in the YugabyteDB cluster, using `ysqlsh`.

   ```postgresql
   demo=# select * from demo.test_table;
   ```
   ```
   key | value | ts
   ----+-------+---------------------------------
     A |     1 | 2018-11-07 02:56:51.000000+0000
     C |     3 | 2018-11-07 02:56:53.000000+0000
     B |     2 | 2018-11-07 02:56:52.000000+0000
   ```

   Note that the timestamp value gets printed as a human-readable date format automatically.

## Future work

- Add more data types.
- Add more tests.
- Add restartability.

## License

This software is distributed under an Apache 2.0 license. See the [LICENSE.txt](https://github.com/YugaByte/yb-kafka-connector/LICENSE) file for details.

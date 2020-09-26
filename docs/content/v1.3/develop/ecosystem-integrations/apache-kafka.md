---
title: Apache Kafka
linkTitle: Apache Kafka
description: Apache Kafka
block_indexing: true
menu:
  v1.3:
    identifier: apache-kafka
    parent: ecosystem-integrations
    weight: 571
isTocNested: true
showAsideToc: true
---

In this tutorial, we are going to use the [Kafka Connect-based Sink Connector for YugabyteDB](https://github.com/yugabyte/yb-kafka-connector) to store events from Apache Kafka into YugabyteDB using YugabyteDB's [YCQL](../../../api/ycql) API.

## 1. Start local cluster

Start a YugabyteDB cluster on your [local machine](../../../quick-start/install/). Check that you are able to connect to YugabyteDB using `cqlsh` by doing the following.

```sh
$ ./bin/cqlsh localhost
```

```
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```

Create the table that would store the Kafka events.

```sql
cqlsh> CREATE KEYSPACE IF NOT EXISTS demo;
cqlsh> CREATE TABLE demo.test_table (key text, value bigint, ts timestamp, PRIMARY KEY (key));
```

## 2. Download Apache Kafka

Download from the [Apache Kafka downloads page](https://kafka.apache.org/downloads). This tutorial uses the `2.11` version of Apache Kafka.

```sh
$ mkdir -p ~/yb-kafka && cd ~/yb-kafka
$ wget http://apache.cs.utah.edu/kafka/2.0.0/kafka_2.11-2.0.0.tgz
$ tar xvfz kafka_2.11-2.0.0.tgz && cd kafka_2.11-2.0.0
```

## 3. Install the Kafka Sink Connector for YugabyteDB

Clone the git `yb-kafka-connector` git repo.

```sh
$ cd ~/yb-kafka
$ git clone https://github.com/yugabyte/yb-kafka-connector.git
$ cd yb-kafka-connector/
```

Build the repo to get the connector jar.

```sh
$ mvn clean install -DskipTests
```

The connector jar `yb-kafka-connnector-1.0.0.jar` is now placed in the `./target` directory. Copy this jar to the libs directory in Kafka Home.

```sh
$ cp ./target/yb-kafka-connnector-1.0.0.jar ~/yb-kafka/kafka_2.11-2.0.0/libs/
```

Go to the Kafka libs directory and get the additional jars that the connector depends on (including the driver for the YCQL API)

```sh
$ cd ~/yb-kafka/kafka_2.11-2.0.0/libs/
$ wget http://central.maven.org/maven2/io/netty/netty-all/4.1.25.Final/netty-all-4.1.25.Final.jar
$ wget http://central.maven.org/maven2/com/yugabyte/cassandra-driver-core/3.2.0-yb-18/cassandra-driver-core-3.2.0-yb-18.jar
$ wget http://central.maven.org/maven2/com/codahale/metrics/metrics-core/3.0.1/metrics-core-3.0.1.jar
```

## 4. Start ZooKeeper and Kafka

Now you can start ZooKeeper and Kafka as shown below.

```sh
$ cd ~/yb-kafka/kafka_2.11-2.0.0
```

```sh
$ ./bin/zookeeper-server-start.sh config/zookeeper.properties &
```

```sh
$ ./bin/kafka-server-start.sh config/server.properties &
```

Now create the Kafka topic that will be used to persist messages in the YugabyteDB table we created earlier.

```sh
$ ./bin/kafka-topics.sh --create \
    --zookeeper localhost:2181 \
    --replication-factor 1 \
    --partitions 1 \
    --topic test
```

## 5. Start Kafka Sink Connector for YugabyteDB

At this point, we have YugabyteDB's YCQL APU running at 9042 port with the `test_table` table created in the `demo` keyspace. We also have Kafka running at the 9092 port with the `test_topic` topic created. We are ready to start the connector.

```sh
$ ./bin/connect-standalone.sh \
    ~/yb-kafka/yb-kafka-connector/resources/examples/kafka.connect.properties \
    ~/yb-kafka/yb-kafka-connector/resources/examples/yugabyte.sink.properties
```

The `yugabyte.sink.properties` file used above (and shown below) has the configuration necessary for this sink to work correctly. You will have to change this file to the Kafka topic and YugabyteDB table necessary for your application.

```
# Sample yugabyte sink properties.

name=yugabyte-sink
connector.class=com.yb.connect.sink.YBSinkConnector

topics=test_topic

yugabyte.cql.keyspace=demo
yugabyte.cql.tablename=test_table
```

## 6. Produce events for Kafka

We can now produce some events into Kafka using the `kafka-console-producer.sh` utility that ships with Kafka.

```sh
$ ~/yb-kafka/kafka_2.11-2.0.0/bin/kafka-console-producer.sh
    --broker-list localhost:9092 \
    --topic test_topic
```

Enter the following.

```
{"key" : "A", "value" : 1, "ts" : 1541559411000}
{"key" : "B", "value" : 2, "ts" : 1541559412000}
{"key" : "C", "value" : 3, "ts" : 1541559413000}
```

## 7. Verify events in YugabyteDB

The events above should now show up as rows in the YugabyteDB table.

```sql
cqlsh> SELECT * FROM demo.test_table;
```

```
key | value | ts
----+-------+---------------------------------
  A |     1 | 2018-11-07 02:56:51.000000+0000
  C |     3 | 2018-11-07 02:56:53.000000+0000
  B |     2 | 2018-11-07 02:56:52.000000+0000
```

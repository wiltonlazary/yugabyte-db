---
title: Overview
linkTitle: Overview
description: Overview
type: page
menu:
  v1.0:
    identifier: overview
    parent: introduction
    weight: 30
---

## What is YugabyteDB?

YugabyteDB is Apache 2.0 open source, transactional, high-performance database for planet-scale applications. It is meant to be a system-of-record/authoritative database that geo-distributed applications can rely on for correctness and availability. It allows applications to easily scale up and scale down across multiple regions in the public cloud, on-premises datacenters or across hybrid environments without creating operational complexity or increasing the risk of outages.

In terms of data model and APIs, YugabyteDB currently supports 3 APIs. 

1. [Cassandra-compatible YCQL](../../api/cassandra/) - YCQL is compatible with [Apache Cassandra Query Language (CQL)](https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html). It also extends CQL by adding [distributed ACID transactions](../../explore/transactional/), [strongly consistent secondary indexes](../../explore/transactional/secondary-indexes/) and a [native JSON data type](../../explore/transactional/json-documents/).

2. [Redis-compatible YEDIS](../../api/redis/) - YugabyteDB supports an auto-sharded, clustered, elastic [Redis](https://redis.io/commands)-as-a-Database in a driver compatible manner with its YEDIS API. YEDIS also extends Redis with a new native [Time Series](https://blog.yugabyte.com/extending-redis-with-a-native-time-series-data-type-e5483c7116f8) data type.

3. [PostgreSQL (Beta)](../../api/postgresql/) - Compatible with the SQL language in PostgreSQL.

## What makes YugabyteDB unique?

YugabyteDB is a single operational database that brings together 3 must-have needs of user-facing cloud applications, namely ACID transactions, high performance and multi-region scalability. Monolithic SQL databases offer transactions and performance but do not have ability to scale across multi-regions. Distributed NoSQL databases offer performance and multi-region scalablility but give up on transactional guarantees. Additionally, it is built for the modern cloud native era and is completely open source both at the core and the API layer.

### 1. Transactional

- [Distributed acid transactions](../../explore/transactional/) that allow multi-row updates across any number of shards at any scale.

- Transactional key-document [storage engine](../../architecture/concepts/persistence/) that's backed by self-healing, strongly consistent [replication](../../architecture/concepts/replication/).

### 2. High Performance

- Low latency for geo-distributed applications with multiple [read consistency levels](../../architecture/concepts/replication/#tunable-read-consistency) and [read-only replicas](../../architecture/concepts/replication/#read-only-replicas).

- High throughput for ingesting and serving ever-growing datasets.

### 3. Planet-Scale

- [Global data distribution](../../explore/planet-scale/global-distribution/) that brings consistent data close to users through multi-region and multi-cloud deployments.

- [Auto-sharding](../../explore/planet-scale/auto-sharding/) and [auto-rebalancing](../../explore/planet-scale/auto-rebalancing/) to ensure uniform load balancing across all nodes even for very large clusters.

### 4. Cloud Native

- Built for the container era with [highly elastic scaling](../../explore/cloud-native/linear-scalability/) and infrastructure portability, including [Kubernetes-driven orchestration](../../quick-start/install/#kubernetes).

- [Self-healing database](../../explore/cloud-native/fault-tolerance/) that automatically tolerates any failures common in the inherently unreliable modern cloud infrastructure.

### 5. Open Source

- Fully functional distributed database available under [Apache 2.0 open source license](https://github.com/yugabyte/yugabyte-db/). Upgrade to [Enterprise Edition](https://www.yugabyte.com/product/compare/) anytime.

- Multi-API/multi-model database that extends existing popular and open APIs including Cassandra, Redis and PostgreSQL.

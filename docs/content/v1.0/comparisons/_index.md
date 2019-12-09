---
title: Comparisons
linkTitle: Comparisons
description: Comparisons
image: /images/section_icons/index/comparisons.png
headcontent: This page highlights how YugabyteDB compares against other operational databases in the NoSQL and distributed SQL categories. Click on the database name in the table header to see a more detailed comparison.
menu:
  v1.0:
    identifier: comparisons
    weight: 1070
---

## NoSQL Databases

Feature | [Apache Cassandra](cassandra/) | [Redis](redis/) | [MongoDB](mongodb/) | [Apache HBase](hbase/) |AWS DynamoDB | [MS Azure CosmosDB](azure-cosmos/)| YugabyteDB
--------|-----------|-------|---------|--------|-------------|--------------|-----------------
Linear Read &amp; Write Scalability | <i class="fas fa-check"></i> |<i class="fas fa-times"></i>| <i class="fas fa-check"></i> |<i class="fas fa-check"></i>| <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Automated Failover &amp; Repair | <i class="fas fa-check"></i> |<i class="fas fa-times"></i>| <i class="fas fa-check"></i> |<i class="fas fa-check"></i>|<i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Auto Sharding | <i class="fas fa-check"></i> |<i class="fas fa-times"></i>|<i class="fas fa-check"></i> |<i class="fas fa-check"></i>| <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Auto Rebalancing | <i class="fas fa-check"></i> |<i class="fas fa-times"></i>| <i class="fas fa-check"></i> |<i class="fas fa-check"></i>|<i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Distributed ACID Transactions | <i class="fas fa-times"></i> |<i class="fas fa-times"></i>| <i class="fas fa-times"></i> |<i class="fas fa-times"></i>| <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i>
Consensus Driven Strongly Consistent Replication | <i class="fas fa-times"></i> |<i class="fas fa-times"></i>| <i class="fas fa-times"></i> |<i class="fas fa-times"></i>| <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i>
Strongly Consistent Secondary Indexes | <i class="fas fa-times"></i> |<i class="fas fa-times"></i>| <i class="fas fa-times"></i> |<i class="fas fa-times"></i>| <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i>
Multiple Read Consistency Levels | <i class="fas fa-check"></i> |<i class="fas fa-times"></i>| <i class="fas fa-check"></i> |<i class="fas fa-times"></i>| <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Low, Predictable p99 Latencies | <i class="fas fa-times"></i> |<i class="fas fa-check"></i>| <i class="fas fa-times"></i> |<i class="fas fa-times"></i>|<i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
High Data Density| <i class="fas fa-times"></i> |<i class="fas fa-times"></i>| <i class="fas fa-times"></i> |<i class="fas fa-check"></i>| <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i>
Cloud-Native Reconfigurability | <i class="fas fa-times"></i> |<i class="fas fa-times"></i>| <i class="fas fa-times"></i> |<i class="fas fa-times"></i>| <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Open API | CQL   |Redis| MongoQL |HBase| Proprietary | CQL, MongoQL | Cassandra-compatible YCQL, Redis-compatible YEDIS
Open Source | Apache 2.0 | 3-Clause BSD| AGPL 3.0 | Apache 2.0| <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | Apache 2.0


## Distributed SQL Databases

Feature |  Clustrix | CockroachDB | AWS Aurora | [MS Azure CosmosDB](azure-cosmos/) | [Google Spanner](google-spanner/) | YugabyteDB
--------|---------|-------------|------------|----------------|----------------|-------------
Linear Write Scalability | <i class="fas fa-check"></i> |  <i class="fas fa-check"></i> | <i class="fas fa-times"></i> |<i class="fas fa-check">| <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Linear Read Scalability | <i class="fas fa-check"></i> |  <i class="fas fa-check"></i> | <i class="fas fa-check"></i> |<i class="fas fa-check">| <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Automated Failover &amp; Repair| <i class="fas fa-check"></i>| <i class="fas fa-check"></i> | <i class="fas fa-times"></i> |<i class="fas fa-check">| <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Auto Sharding  |<i class="fas fa-check"></i>| <i class="fas fa-check"></i> | <i class="fas fa-times"></i> |<i class="fas fa-check">| <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Auto Rebalancing |<i class="fas fa-check"></i>| <i class="fas fa-check"></i> | <i class="fas fa-times"></i> |<i class="fas fa-check">| <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Distributed ACID Transactions |<i class="fas fa-check"></i>| <i class="fas fa-check"></i> | <i class="fas fa-check"></i> |<i class="fas fa-times"></i>| <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
SQL Joins|<i class="fas fa-check"></i>| <i class="fas fa-check"></i> | <i class="fas fa-check"></i> |<i class="fas fa-times"></i>| <i class="fas fa-check"></i> | <i class="fas fa-times"></i>
Consensus Driven Strongly Consistent Replication |<i class="fas fa-check"></i>| <i class="fas fa-check"></i> | <i class="fas fa-times"></i> |<i class="fas fa-times">| <i class="fas fa-check"></i> |<i class="fas fa-check"></i>
Global Consistency Across Multi-DC/Regions |<i class="fas fa-times"></i>| <i class="fas fa-check"></i> | <i class="fas fa-times"></i> |<i class="fas fa-times">| <i class="fas fa-check"></i> |<i class="fas fa-check"></i>
Multiple Read Consistency Levels | <i class="fas fa-times"></i> |<i class="fas fa-times"></i>| <i class="fas fa-times"></i> |<i class="fas fa-check"></i>| <i class="fas fa-times"></i> | <i class="fas fa-check"></i>
Cloud-Native Reconfigurability |<i class="fas fa-times"></i>| <i class="fas fa-check"></i> | <i class="fas fa-check"></i> |<i class="fas fa-check">| <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Low, Predictable p99 Latencies | <i class="fas fa-times"></i> |<i class="fas fa-times"></i>| <i class="fas fa-check"></i> |<i class="fas fa-check"></i>|<i class="fas fa-check"></i> | <i class="fas fa-check"></i> 
SQL Compatibility |MySQL| PostgreSQL | MySQL, PostgreSQL |Read Only| Read Only| PostgreSQL (BETA)
Open Source | <i class="fas fa-times"></i>| Apache 2.0 | <i class="fas fa-times"></i> | <i class="fas fa-times"></i>| <i class="fas fa-times"></i> | Apache 2.0


{{< note title="Note" >}}
The <i class="fas fa-check"></i> or <i class="fas fa-times"></i> with respect to any particular feature of a 3rd party database is based on our best effort understanding from publicly available information. Readers are always recommended to perform their own independent research to understand the finer details.
{{< /note >}}


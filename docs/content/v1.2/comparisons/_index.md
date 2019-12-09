---
title: Comparisons
linkTitle: Comparisons
description: Comparisons
image: /images/section_icons/index/comparisons.png
headcontent: This page highlights how YugabyteDB compares against other operational databases in the distributed SQL and NoSQL categories. Click on the database name in the table header to see a more detailed comparison.
section: FAQ
menu:
  v1.2:
    identifier: comparisons
    weight: 2720
---

## Distributed SQL Databases

Feature | [CockroachDB](cockroachdb/) | TiDB | Amazon Aurora | [MS Azure CosmosDB](azure-cosmos/) | [Google Cloud Spanner](google-spanner/) | YugabyteDB
--------|-----------------|------------|----------------|----------------|-------------|-----------
Horizontal Write Scalability | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check">| <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Auto Sharding & Rebalancing   | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Automated Failover &amp; Repair  | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Distributed ACID Transactions  | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
SQL Joins | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> |<i class="fas fa-times"></i>| <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Serializable Isolation Level | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Global Consistency Across Multi-DC/Regions | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-times"></i> | <i class="fas fa-times"> | <i class="fas fa-check"></i> |<i class="fas fa-check"></i>
Multiple Read Consistency Levels | <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i>
Low, Predictable p99 Latencies | <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> 
SQL Compatibility | PostgreSQL | MySQL | MySQL, PostgreSQL | Read Only | Proprietary | PostgreSQL (BETA)
Open Source | Apache 2.0 | Apache 2.0 | <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | Apache 2.0


## NoSQL Databases

Feature  | [MongoDB](mongodb/) | [FoundationDB](foundationdb/) | [Apache Cassandra](cassandra/) |[Amazon DynamoDB](amazon-dynamodb/) | [MS Azure CosmosDB](azure-cosmos/)| YugabyteDB
--------|-----------|-------|--------|-------------|--------------|-----------------
Horizontal Write Scalability | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> |<i class="fas fa-check"></i>| <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Automated Failover &amp; Repair | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> |<i class="fas fa-check"></i>|<i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Auto Sharding & Rebalancing | <i class="fas fa-check"></i> |<i class="fas fa-check"></i> |<i class="fas fa-check"></i>| <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Distributed ACID Transactions  | <i class="fas fa-times"></i> |<i class="fas fa-check"></i> | <i class="fas fa-times"></i>| <i class="fas fa-check"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i>
Consensus Driven Strongly Consistent Replication  | <i class="fas fa-times"></i> |<i class="fas fa-check"></i> | <i class="fas fa-times"></i>| <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i>
Strongly Consistent Secondary Indexes  | <i class="fas fa-times"></i> |<i class="fas fa-check"></i> | <i class="fas fa-times"></i>| <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i>
Multiple Read Consistency Levels | <i class="fas fa-check"></i> | <i class="fas fa-check"></i> |<i class="fas fa-check"></i>| <i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
Low, Predictable p99 Latencies | <i class="fas fa-times"></i> | <i class="fas fa-times"></i> |<i class="fas fa-times"></i>|<i class="fas fa-check"></i> | <i class="fas fa-check"></i> | <i class="fas fa-check"></i>
High Data Density| <i class="fas fa-times"></i> | <i class="fas fa-times"></i> |<i class="fas fa-times"></i>| <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | <i class="fas fa-check"></i>
API | MongoDB QL | Proprietary KV, MongoDB QL | Cassandra QL | Proprietary KV, Document | Cassandra QL, MongoDB QL | YCQL w/ Cassandra QL roots
Open Source | SSPL | Apache 2.0 | Apache 2.0 | <i class="fas fa-times"></i> | <i class="fas fa-times"></i> | Apache 2.0

{{< note title="Note" >}}
The <i class="fas fa-check"></i> or <i class="fas fa-times"></i> with respect to any particular feature of a 3rd party database is based on our best effort understanding from publicly available information. Readers are always recommended to perform their own independent research to understand the finer details.
{{< /note >}}


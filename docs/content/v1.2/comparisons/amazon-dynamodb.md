---
title: Amazon DynamoDB
linkTitle: Amazon DynamoDB
description: Amazon DynamoDB
menu:
  v1.2:
    parent: comparisons
    weight: 1105
isTocNested: false
showAsideToc: true
---

## Astronomical AWS Bills and Slow Releases

Amazon DynamoDB is a fully managed NoSQL database offered by Amazon Web Services. While it works great for smaller scale applications, the limitations it poses in the context of larger scale applications are not well understood.

![Amazon DynamoDB Issues](/images/comparisons/amazon-dynamodb-issues.png)

Our post [11 Things You Wish You Knew Before Starting with DynamoDB](https://blog.yugabyte.com/11-things-you-wish-you-knew-before-starting-with-dynamodb/) highlights the above items in better detail.

## YugabyteDB Gives 3x Agility at 1/10th Cost 

YugabyteDB is an open source [multi-API/multi-model](https://blog.yugabyte.com/polyglot-persistence-vs-multi-api-multi-model-which-one-makes-multi-cloud-easy) database with transactional consistency, low latency and geo-distribution built into the core of a common storage engine. It is a drop-in replacement for [Apache Cassandra](../../api/ycql/) and [Redis](../../api/yedis/) given its protocol-level compatibility with the languages spoken by these databases (PostgreSQL compatibility is currently in beta). As a Consistent and Partition-tolerant (CP) database with native JSONB document data type, high performance secondary indexes, cloud native operational ease and ability to handle high data density, it serves as an excellent alternative to not only Amazon DynamoDB but also negates the need for a separate RDBMS and a separate cache.

![Amazon DynamoDB Issues](/images/comparisons/yugabyte-db-beats-amazon-dynamodb.png)

Our post [DynamoDB vs MongoDB vs Cassandra for Fast Growing Geo-Distributed Apps](https://blog.yugabyte.com/dynamodb-vs-mongodb-vs-cassandra-for-fast-growing-geo-distributed-apps/) further details the differences between YugabyteDB and DynamoDB.

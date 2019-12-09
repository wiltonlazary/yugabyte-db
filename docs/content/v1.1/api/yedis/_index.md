---
title: YEDIS API
linkTitle: YEDIS
description: Yugabyte DIctionary Service (YEDIS)
summary: Reference for Redis-compatible YEDIS API
image: /images/section_icons/api/yedis.png
headcontent: 
menu:
  v1.1:
    identifier: api-redis
    parent: api
    weight: 2000
isTocNested: true
showAsideToc: true
---

## Introduction
YEDIS API is a transactional (single-key ACID) key-value API that is compatible with the Redis commands library. A Redis client can connect, send request, and receive result from YugabyteDB.

## Data Types
The following datatypes can be read and written via the YEDIS API. All data are on-disk persistent in YugabyteDB.<br>

Data Type | Development Status |
---------|-------------|
String | Supported |
Hash | Supported |
Set | Supported |
Sorted set | Supported |
List | Not yet supported |
Time series | New datatype in Yugabyte! |

## Commands
Redis-cli or any Redis applications can access YugabyteDB using the YEDIS API. The following Redis commands are accepted.

Command | Description |
--------|-------------|
<b>String Data Type </b> |
[`APPEND`](append/) | Append data to end of string |
[`DEL`](del/) | Delete keys from a database |
[`EXISTS`](exists/) | Check if the keys are present |
[`EXPIRE`](expire/) | Set key timeout in seconds |
[`EXPIREAT`](expireat/) | Set key timeout as timestamp |
[`GET`](get/) | Read string value |
[`GETRANGE`](getrange/) | Read substring |
[`GETSET`](getset/) | Atomically read and write a string |
[`INCR`](incr/) | Increment the value by one |
[`KEYS`](keys/) | Retrieve all keys matching a pattern
[`PEXPIRE`](expire/) | Set key timeout in milliseconds |
[`PEXPIREAT`](expireat/) | Set key timeout as timestamp in milliseconds |
[`PTTL`](pttl/) | Get time to live for key in milliseconds |
[`TTL`](ttl/) | Get time to live for key in seconds |
<b> Hash Data Type </b>|
[`HDEL`](hdel/) | Remove specified entries from a hash |
[`HEXISTS`](hexists/) | Check if the subkeys are present in the hash |
[`HGET`](hget/) | Read a field in hash |
[`HGETALL`](hgetall/) | Read all the contents in a hash |
[`HKEYS`](hkeys/) | Read all value-keys in a hash |
[`HLEN`](hlen/) | Get the number of entries in a hash |
[`HMGET`](hmget/) | Read values for the given keys in a hash |
[`HMSET`](hmset/) | Write values for the given keys in a hash |
[`HSET`](hset/) | Write one entry in a hash |
[`HSTRLEN`](hstrlen/) | Read the length of a specified entry in a hash |
[`HVALS`](hvals/) | Read all values in a hash |
<b> Set Data Type </b>|
[`SADD`](sadd/) | Add entries to a set |
[`SCARD`](scard/) | Read the number of entries in a set |
[`SET`](set/) | Write or overwrite a string value |
[`SETRANGE`](setrange/) | Write a subsection of a string |
[`SISMEMBER`](sismember/) | Check if the members are present in a set |
[`SMEMBERS`](smembers/) | Read all members of a set |
[`SREM`](srem/) | Remove members from a set |
[`STRLEN`](strlen/) | Read the length of a string |
<b> Time Series Data Type </b>|
[`TSADD`](tsadd/) | Add a time series entry |
[`TSCARD`](tscard/) | Retrieve the number of elements in the given time series |
[`TSGET`](tsget/) | Retrieve a time series entry |
[`TSLASTN`](tslastn/) | Retrieve the latest N time series entries for a given time series |
[`TSRANGEBYTIME`](tsrangebytime/) | Retrieve time series entries for a given time range |
[`TSREM`](tsrem/) | Delete a time series entry |
[`TSREVRANGEBYTIME`](tsrevrangebytime/) | Retrieve time series entries for a given time range ordered from newest to oldest |
<b> Sorted Set Data Type </b>|
[`ZADD`](zadd/) | Add a sorted set entry |
[`ZCARD`](zcard/) | Get cardinality of a sorted set |
[`ZRANGE`](zrange/) | Retrieve sorted set entries for given index range
[`ZRANGEBYSCORE`](zrangebyscore/) | Retrieve sorted set entries for a given score range |
[`ZREM`](zrem/) | Delete a sorted set entry |
[`ZREVRANGE`](zrevrange/) | Retrieve sorted set entries for given index range ordered from highest to lowest score |
<b> General </b>|
[`AUTH`](auth/) | Authenticates a client connection to YEDIS API |
[`CONFIG`](config/) | Not fully supported. Only used to set the required password(s) for YEDIS API |
[`ECHO`](echo/) | Output messages |
[`MONITOR`](monitor/) | Debugging tool to see all requests that are processed by a YEDIS API server |
[`ROLE`](role/) | Read role of a node |
<b> Database </b>|
[`FLUSHALL`](flushall/) | Delete all keys from all databases |
[`FLUSHDB`](flushdb/) | Delete all keys from a database |
[`CREATEDB`](createdb/) | Create a new yedis database |
[`LISTDB`](listdb/) | List all the yedis databases present |
[`DELETEDB`](deletedb/) | Delete a yedis database |
[`SELECT`](select/) | Select the target database to communcate with |
<b> Pub-Sub </b>|
[`PUBSUB`](pubsub/) | Used to query the state of the Pub/Sub system. |
[`PUBLISH`](publish/) | Publishes a message to the specified channel |
[`SUBSCRIBE`](subscribe/) | Subscribes the client to the specified channel(s) |
[`PSUBSCRIBE`](psubscribe/) | Subscribes the client to the specified pattern(s)  |
[`UNSUBSCRIBE`](unsubscribe/) | Unubscribes the client from the specified channel(s) |
[`PUNSUBSCRIBE`](punsubscribe/) | Unubscribes the client from the specified pattern(s) |

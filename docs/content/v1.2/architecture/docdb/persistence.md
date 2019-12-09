---
title: Persistence
linkTitle: Persistence
description: Persistence on top of RocksDB
menu:
  v1.2:
    identifier: docdb-persistence
    parent: docdb
    weight: 1146
isTocNested: true
showAsideToc: true
---

Once data is replicated via Raft across a majority of the tablet-peers, it is applied to each tablet peer’s local DocDB document storage layer.

## Storage Model

This storage layer is a persistent **key to object/document** store. The storage model is shown in the figure below:

![cql_row_encoding](/images/architecture/cql_row_encoding.png)

The keys and the corresponding document values are described below.

### DocDB Key

The **keys** in DocDB document model are compound keys consisting of 1 or more **hash organized components**, followed by 0 or more **ordered (range) components**. These components are stored in their data type specific sort order; both ascending and descending sort order is supported for each ordered component of the key.

### DocDB Value

The **values** in DocDB document data model can be:

* **primitive types**: such as int32, int64, double, text, timestamp, etc.
* **non-primitive types (sorted maps)**: These objects map scalar keys to values, which could be either scalar or sorted maps as well.

This model allows multiple levels of nesting, and corresponds to a JSON-like format. Other data
structures like lists, sorted sets etc. are implemented using DocDB’s object type with special key
encodings. In DocDB, [hybrid timestamps](../../transactions/distributed-txns/) of each update are recorded carefully, so that it is possible to recover the state of any document at some point in the past. Overwritten or deleted versions of data are garbage-collected as soon as there are no transactions reading at a snapshot at which the old value would be visible.

#### Encoding Documents

The documents are stored using a key-value store based on RocksDB, which is typeless. The documents
are converted to multiple key-value pairs along with timestamps. Because documents are spread across
many different key-values, it’s possible to partially modify them cheaply.

For example, consider the following document stored in DocDB:

```
DocumentKey1 = {
	SubKey1 = {
		SubKey2 = Value1
		SubKey3 = Value2
	},
	SubKey4 = Value3
}
```

Keys we store in RocksDB consist of a number of components, where the first component is a "document
key", followed by a few scalar components, and finally followed by a MVCC timestamp (sorted in
reverse order). Each component in the DocumentKey, SubKey, and Value, are PrimitiveValues, which are
just (type, value) pairs, which can be encoded to and decoded from strings. When we encode primitive
values in keys, we use a binary-comparable encoding for the value, so that sort order of the
encoding is the same as the sort order of the value.

#### Updates and Deletes

Assume that the example document above was written at time T10 entirely. Internally the above
example’s document is stored using 5 RocksDB key value pairs:

```
DocumentKey1, T10 -> {} // This is an init marker
DocumentKey1, SubKey1, T10 -> {}
DocumentKey1, SubKey1, SubKey2, T10 -> Value1
DocumentKey1, SubKey1, SubKey3, T10 -> Value2
DocumentKey1, SubKey4, T10 -> Value3
```

Deletions of Documents and SubDocuments are performed by writing a single Tombstone marker at the
corresponding value. During compaction, overwritten or deleted values are cleaned up to reclaim
space.

## Mapping SQL rows to DocDB

For YSQL (and YCQL) tables, every row is a document in DocDB.

### Primary Key Columns

The document key contains the full primary key with column values in organized in the following order:

* A 16-bit hash of the hash column values is stored first
* The partition (hash) column(s) are stored next
* The clustering (range) column(s) are stored next

Each data type supported in YSQL (or YCQL) is represented by a unique byte. The type prefix is also present in the primary key’s hash or range components

### Non-Primary Key Columns

The non-primary key columns correspond to subdocuments within the document. The subdocument key corresponds to the column ID. There’s a unique byte for each data type we support in YSQL (or YCQL). The values are prefixed with the corresponding byte. If a column is a non-primitive type (such as a map or set), the corresponding subdocument is an Object.

We use a binary-comparable encoding to translate the value for each YCQL type to strings that go to the KV-Store.

### Data Expiry in YCQL

In YCQL there are two types of TTL, the table TTL and column level TTL. The column TTLs are stored
with the value using the same encoding as Redis. The Table TTL is not stored in DocDB (it is stored
in master’s syscatalog as part of the table’s schema). If no TTL is present at the column’s value,
the table TTL acts as the default value.

Furthermore, CQL has a distinction between rows created using Insert vs Update. We keep track of
this difference (and row level TTLs) using a "liveness column", a special system column invisible to
the user. It is added for inserts, but not updates: making sure the row is present even if all
non-primary key columns are deleted only in the case of inserts.

## YCQL - Collection Type Example

Consider the following YCQL table schema:

```sql
CREATE TABLE msgs (user_id text,
                   msg_id int,
                   msg text,
                   msg_props map<text, text>,
      PRIMARY KEY ((user_id), msg_id));
```

### Insert a Row

```
T1: INSERT INTO msgs (user_id, msg_id, msg, msg_props)
      VALUES ('user1', 10, 'msg1', {'from' : 'a@b.com', 'subject' : 'hello'});
```

The entries in DocDB at this point will look like the following:

```
(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'
```

### Update Subset of Columns

```sql
T2: UPDATE msgs
       SET msg_props = msg_props + {'read_status' : 'true'}
     WHERE user_id = 'user1', msg_id = 10
```

The entries in DocDB at this point will look like the following:

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
<b>(hash1, 'user1', 10), msg_props_column_id, 'read_status', T2 -> 'true'</b>
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'
</code>
</pre>

### Update Entire Row

```sql
T3: INSERT INTO msgs (user_id, msg_id, msg, msg_props)
        VALUES (‘user1’, 20, 'msg2', {'from' : 'c@d.com', 'subject' : 'bar'});
```

The entries in DocDB at this point will look like the following:

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
(hash1, 'user1', 10), msg_props_column_id, 'read_status', T2 -> 'true'
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'
<b>(hash1, 'user1', 20), liveness_column_id, T3 -> [NULL]
(hash1, 'user1', 20), msg_column_id, T3 -> 'msg2'
(hash1, 'user1', 20), msg_props_column_id, 'from', T3 -> 'c@d.com'
(hash1, 'user1', 20), msg_props_column_id, 'subject', T3 -> 'bar'</b></code>
</pre>

### Delete a Row

Delete a single column from a row
```sql
T4: DELETE msg_props       
      FROM msgs
     WHERE user_id = 'user1'
       AND msg_id = 10;
```

Even though, in the example above, the column being deleted is a non-primitive column (a map), this
operation only involves adding a delete marker at the correct level, and does not incur any read
overhead. The logical layout in DocDB at this point is shown below.

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
<b>(hash1, 'user1', 10), msg_props_column_id, T4 -> [DELETE]</b>
<s>(hash1, 'user1', 10), msg_props_column_id, 'from', T1 -> 'a@b.com'
(hash1, 'user1', 10), msg_props_column_id, 'read_status', T2 -> 'true'
(hash1, 'user1', 10), msg_props_column_id, 'subject', T1 -> 'hello'</s>
(hash1, 'user1', 20), liveness_column_id, T3 -> [NULL]
(hash1, 'user1', 20), msg_column_id, T3 -> 'msg2'
(hash1, 'user1', 20), msg_props_column_id, 'from', T3 -> 'c@d.com'
(hash1, 'user1', 20), msg_props_column_id, 'subject', T3 -> 'bar'</code>
</pre>
*Note: The KVs that are displayed in “strike-through” font are logically deleted.*

Note: The above is not the physical layout per se, as the writes happen in a log-structured manner.
When compactions happen, the space for the KVs corresponding to the deleted columns is reclaimed, as
shown below.

<pre>
<code>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'
(hash1, 'user1', 20), liveness_column_id, T3 -> [NULL]
(hash1, 'user1', 20), msg_column_id, T3 -> 'msg2'
(hash1, 'user1', 20), msg_props_column_id, 'from', T3 -> 'c@d.com'
(hash1, 'user1', 20), msg_props_column_id, 'subject', T3 -> 'bar'

T5: DELETE FROM msgs    // Delete entire row corresponding to msg_id 10
     WHERE user_id = 'user1'
            AND msg_id = 10;

<b>(hash1, 'user1', 10), T5 -> [DELETE]</b>
<s>(hash1, 'user1', 10), liveness_column_id, T1 -> [NULL]
(hash1, 'user1', 10), msg_column_id, T1 -> 'msg1'</s>
(hash1, 'user1', 20), liveness_column_id, T3 -> [NULL]
(hash1, 'user1', 20), msg_column_id, T3 -> 'msg2'
(hash1, 'user1', 20), msg_props_column_id, 'from', T3 -> 'c@d.com'
(hash1, 'user1', 20), msg_props_column_id, 'subject', T3 -> 'bar'</code>
</pre>

## YCQL - Time-To-Live (TTL) Example

**Table Level TTL**: YCQL allows the TTL property to be specified at the table level. In this case, we do not store the TTL on a per KV basis in RocksDB; but the TTL is implicitly enforced on reads as well as during compactions (to reclaim space).

**Row and Column Level TTL**: YCQL allows the TTL property to be specified at the level of each INSERT/UPDATE operation. In such cases, the TTL is stored as part of the RocksDB value.

Below, we will look at how the row-level TTL is achieved in detail.

<pre>
<code>CREATE TABLE page_views (page_id text,
                         views int,
                         category text,
     PRIMARY KEY ((page_id)));
</code>
</pre>

### Insert Row With TTL

<pre>
<code>
T1: INSERT INTO page_views (page_id, views)
        VALUES ('abc.com', 10)
        USING TTL 86400

// The entries in DocDB will look like the following

(hash1, 'abc.com'), liveness_column_id, T1 -> (TTL = 86400) [NULL]
(hash1, 'abc.com'), views_column_id, T1 -> (TTL = 86400) 10
</code>
</pre>

### Update Row With TTL

<pre>
<code>
T2: UPDATE page_views
     USING TTL 3600
       SET category = 'news'
     WHERE page_id = 'abc.com';

// The entries in DocDB will look like the following

(hash1, 'abc.com'), liveness_column_id, T1 -> (TTL = 86400) [NULL]
(hash1, 'abc.com'), views_column_id, T1 -> (TTL = 86400) 10
<b>(hash1, 'abc.com'), category_column_id, T2 -> (TTL = 3600) 'news'</b></code>
</pre>


## Mapping YEDIS data to DocDB

Redis is a schemaless data store. There is only one primitive type (string) and some collection
types. In this case, the documents are pretty simple. For primitive values, the document consists of
only one value. The document key is just a string prefixed with a hash. Redis collections are single
level documents. Maps correspond to SubDocuments which are discussed above. Sets are stored as maps
with empty values, and Lists have indexes as keys. For non-primitive values (e.g., hash, set type),
we store the type in parent level initial value, which is sorted before the subkeys. Any redis value
can have a TTL, which is stored in the RocksDB-value.

![redis_docdb_overview](/images/architecture/redis_docdb_overview.png)

### Redis Example

| Timestamp | Command                                   | New Key-Value pairs added in RocksDB                                                                            |
|:---------:|:-----------------------------------------:|:---------------------------------------------------------------------------------------------------------------:|
|T1         |SET key1 value1 EX 15                      |(h1, key1), T1 -> 15, value1                                                                                     |
|T2         |HSET key2 subkey1 value1                   |(h2, key2), T2 -> [Redis-Hash-Type]<br/>(h2, key2), subkey1, T2 -> value1                                        |
|T3         |HSET key2 subkey2 value2                   |(h2, key2), subkey2, T3 -> value2                                                                                |
|T4         |DEL key2                                   |(h2, key2), T4 -> Tombstone                                                                                      |
|T5         |HMSET key2 subkey1 new_val1 subkey3 value3 |(h2, key2), T2 -> [Redis-Hash-Type]<br/>(h2, key2), subkey1, T5 -> new_val1<br/>(h2, key2), subkey3, T5 -> value3|
|T6         |SADD key3 value4 value5                    |(h3, key3), T6 -> [Redis-Set-Type]<br/>(h3, key3), value4, T6 -> [NULL]<br/>(h3, key3), value5, T6 -> [NULL]     |
|T7         |SADD key3 value6                           |(h3, key3), value6, T7 -> [NULL]                                                                                 |

Although they are added out of order, we get a sorted view of the items in the key value store when
reading, as shown below:

```
(h1, key1), T1 -> 15, value1
(h2, key2), T5 -> [Redis-Hash-Type]
(h2, key2), T4 -> Tombstone
(h2, key2), T2 -> [Redis-Hash-Type]
(h2, key2), subkey1, T5 -> new_val1
(h2, key2), subkey1, T2 -> value1
(h2, key2), subkey2, T3 -> value2
(h2, key2), subkey3, T5 -> value3
(h3, key3), T6 -> [Redis-Set-Type]
(h3, key3), value6, T7 -> [NULL]
(h3, key3), value4, T6 -> [NULL]
(h3, key3), value5, T6 -> [NULL]
```

Using an iterator, it is easy to reconstruct the hash and set contents efficiently.


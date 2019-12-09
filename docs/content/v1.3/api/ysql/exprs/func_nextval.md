---
title: nextval()
summary: Get the next value from the session's sequence cache
description: nextval()
menu:
  v1.3:
    identifier: api-ysql-exprs-nextval
    parent: api-ysql-exprs
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `nextval( sequence_name )` function to return the next value from the sequence cache for the current session. If no more values are available in the cache, the session allocates a block of numbers for the cache and returns the first one. The number of elements allocated is determined by the `cache` option specified as part of the `CREATE SEQUENCE` statement.

## Semantics

### _sequence_name_

Specify the name of the sequence.

- An error is raised if a sequence reaches its minimum or maximum value.

## Examples

### Create a simple sequence that increments by 1 every time nextval() is called

```sql
postgres=# CREATE SEQUENCE s;
```

```
CREATE SEQUENCE
```

Call nextval() a couple of times.

```sql
postgres=# SELECT nextval('s');
```

```
 nextval
---------
       1
(1 row)
```

```sql
postgres=# SELECT nextval('s');
```

```
 nextval
---------
       2
(1 row)
```

### Create a sequence with a cache of 3 values

```sql
postgres=# CREATE SEQUENCE s2 CACHE 3;
```

```
CREATE SEQUENCE
```

In the same session, call `nextval()`. The first time it's called, the session's cache will allocate numbers 1, 2, and 3. This means that the data for this sequence will have its `last_val` set to 3. This modification requires two RPC requests.

```sql
SELECT nextval('s2');
```

```
 nextval
---------
       1
(1 row)
```

The next call of `nextval()` in the same session will not generate new numbers for the sequence, so it is much faster than the first `nextval()` call because it will just use the next value available from the cache.

```sql
SELECT nextval('s2');
```

```
nextval
---------
       2
(1 row)
```

## See also

[`CREATE SEQUENCE`](../create_sequence)
[`DROP SEQUENCE`](../drop_sequence)
[`currval()`](../currval_sequence)
[`lastval()`](../lastval_sequence)
[Other YSQL Statements](..)

---
title: BOOLEAN
summary: Boolean values of false or true
description: BOOLEAN Type
block_indexing: true
menu:
  v1.3:
    parent: api-cassandra
    weight: 1380
isTocNested: true
showAsideToc: true
---

## Synopsis

`BOOLEAN` data type is used to specify values of either `true` or `false`.

## Syntax

```
type_specification ::= BOOLEAN

boolean_literal ::= TRUE | FALSE
```

## Semantics

- Columns of type `BOOLEAN` cannot be part of the `PRIMARY KEY`.
- Columns of type `BOOLEAN` can be set, inserted, and compared.
- In `WHERE` and `IF` clause, `BOOLEAN` columns cannot be used as a standalone expression. They must be compared with either `true` or `false`. For example, `WHERE boolean_column = TRUE` is valid while `WHERE boolean_column` is not.
- Implicitly, `BOOLEAN` is neither comparable nor convertible to any other data types.

## Examples

```sql
cqlsh:example> CREATE TABLE tasks (id INT PRIMARY KEY, finished BOOLEAN);
```

```sql
cqlsh:example> INSERT INTO tasks (id, finished) VALUES (1, false);
```

```sql
cqlsh:example> INSERT INTO tasks (id, finished) VALUES (2, false);
```

```sql
cqlsh:example> UPDATE tasks SET finished = true WHERE id = 2;
```

```sql
cqlsh:example> SELECT * FROM tasks;
```

```
id | finished
----+----------
  2 |     True
  1 |    False
```

## See also

[Data Types](..#data-types)

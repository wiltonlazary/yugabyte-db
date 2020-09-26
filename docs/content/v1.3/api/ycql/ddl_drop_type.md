---
title: DROP TYPE
summary: Drop a user-defined data type
description: DROP TYPE
block_indexing: true
menu:
  v1.3:
    parent: api-cassandra
    weight: 1280
isTocNested: true
showAsideToc: true
---

## Synopsis

The `DROP TYPE` statement removes an existing user-defined data type.

## Syntax

### Diagram

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="376" height="50" viewbox="0 0 376 50"><path class="connector" d="M0 22h5m53 0h10m49 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m88 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="49" height="25" rx="7"/><text class="text" x="78" y="22">TYPE</text><rect class="literal" x="147" y="5" width="32" height="25" rx="7"/><text class="text" x="157" y="22">IF</text><rect class="literal" x="189" y="5" width="64" height="25" rx="7"/><text class="text" x="199" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#type-name"><rect class="rule" x="283" y="5" width="88" height="25"/><text class="text" x="293" y="22">type_name</text></a></svg>

### Grammar

```
drop_type ::= DROP TYPE [ IF EXISTS ] type_name;
```
Where

- `type_name` is an identifier (possibly qualified with a keyspace name).

## Semantics

- An error is raised if the specified `type_name` does not exist unless `IF EXISTS` option is used.
- A user-defined `type_name` cannot be dropped if it is currently used in a table or another type.

## Examples

```sql
cqlsh:example> CREATE TYPE person(first_name TEXT, last_name TEXT, email TEXT);
```

```sql
cqlsh:example> DROP TYPE person;
```

## See also

[`CREATE TABLE`](../ddl_create_table)
[`DROP TYPE`](../ddl_drop_keyspace)
[Other CQL Statements](..)

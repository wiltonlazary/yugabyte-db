---
title: ABORT statement [YSQL]
headerTitle: ABORT
linkTitle: ABORT
description: Use the ABORT statement to roll back the current transaction and discards all updates by the transaction.
block_indexing: true
menu:
  v2.1:
    identifier: api-ysql-commands-txn-abort
    parent: api-ysql-commands
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `ABORT` statement to roll back the current transaction and discards all updates by the transaction.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <i class="fas fa-file-alt" aria-hidden="true"></i>
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <i class="fas fa-project-diagram" aria-hidden="true"></i>
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
    {{% includeMarkdown "../syntax_resources/commands/abort.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/abort.diagram.md" /%}}
  </div>
</div>

## Semantics

### *abort*

#### ABORT [ TRANSACTION | WORK ]

##### WORK

Add optional keyword — has no effect.

##### TRANSACTION

Add optional keyword — has no effect.

- The `SERIALIZABLE` isolation level not yet supported. (This is currently in progress).
- Currently YugabyteDB will always use the snapshot isolation level internally. For more information, see [Transaction isolation levels](../../../../architecture/transactions/isolation-levels/).

## Examples

Create a sample table.

```postgresql
yugabyte=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

Begin a transaction and insert some rows.

```postgresql
yugabyte=# BEGIN TRANSACTION; SET TRANSACTION ISOLATION LEVEL REPEATABLE READ; 
```

```postgresql
yugabyte=# INSERT INTO sample(k1, k2, v1, v2) VALUES (1, 2.0, 3, 'a'), (1, 3.0, 4, 'b');
```

Open the YSQL shell (`ysqlsh`) and begin another transaction to insert some more rows.

```postgresql
yugabyte=# BEGIN TRANSACTION; SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
```

```postgresql
yugabyte=# INSERT INTO sample(k1, k2, v1, v2) VALUES (2, 2.0, 3, 'a'), (2, 3.0, 4, 'b');
```

In each shell, check the only the rows from the current transaction are visible.

First shell.

```postgresql
yugabyte=# SELECT * FROM sample; -- run in first shell
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  1 |  3 |  4 | b
(2 rows)
```

Second shell.

```postgresql
yugabyte=# SELECT * FROM sample; -- run in second shell
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  2 |  2 |  3 | a
  2 |  3 |  4 | b
(2 rows)
```

Commit the first transaction and abort the second one.

```postgresql
yugabyte=# COMMIT TRANSACTION; -- run in first shell.
```

Abort the current transaction (from the first shell).

```postgresql
yugabyte=# ABORT TRANSACTION; -- run second shell.
```

In each shell, check that only the rows from the committed transaction are visible.

```postgresql
yugabyte=# SELECT * FROM sample; -- run in first shell.
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  1 |  3 |  4 | b
(2 rows)
```

```postgresql
yugabyte=# SELECT * FROM sample; -- run in second shell.
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  1 |  3 |  4 | b
(2 rows)
```

## See also

- [`BEGIN`](../txn_begin)
- [`COMMIT`](../txn_commit)
- [`ROLLBACK`](../txn_rollback)

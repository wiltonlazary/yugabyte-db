---
title: LOCK
linkTitle: LOCK
summary: Lock a table
description: LOCK
block_indexing: true
menu:
  v1.3:
    identifier: api-ysql-commands-lock
    parent: api-ysql-commands
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `LOCK` statement to lock a table.

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
    {{% includeMarkdown "../syntax_resources/commands/lock_table,lockmode.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/lock_table,lockmode.diagram.md" /%}}
  </div>
</div>

## Semantics

### *lock_table*

#### *name*

Specify an existing table to lock.

### *lockmode*

- Only `ACCESS SHARE` lock mode is supported at this time.
- All other modes listed in *lockmode* are under development.

```
ACCESS SHARE
  | ROW SHARE
  | ROW EXCLUSIVE
  | SHARE UPDATE EXCLUSIVE
  | SHARE
  | SHARE ROW EXCLUSIVE
  | EXCLUSIVE
  | ACCESS EXCLUSIVE
```

## See also

- [`SET TRANSACTION`](../txn_set)

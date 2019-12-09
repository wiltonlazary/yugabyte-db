---
title: SET CONSTRAINTS
linkTitle: SET CONSTRAINTS
summary: SET CONSTRAINTS
description: SET CONSTRAINTS
menu:
  v1.3:
    identifier: api-ysql-commands-set-constraints
    parent: api-ysql-commands
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `SET CONSTRAINTS` statement to set the timing of constraint checking within the current transaction.

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
    {{% includeMarkdown "../syntax_resources/commands/set_constraints.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/set_constraints.diagram.md" /%}}
  </div>
</div>

## Semantics

Attributes in the `SET CONSTRAINTS` statement comply with the behavior defined in the SQL standard, except that it does not apply to `NOT NULL` and `CHECK` constraints.

### *set_constraints*

```
SET CONSTRAINTS { ALL | *name [ , ... ] } { DEFERRED | IMMEDIATE }
```

### ALL

Change the mode of all deferrable constraints.

### *name*

Specify one or a list of constraint names.

### DEFERRED

Set constraints to not be checked until transaction commit.

Uniqueness and exclusion constraints are checked immediately, unless marked `DEFERRABLE`.

### IMMEDIATE

Set constraints to take effect retroactively.

See also

- [`ALTER TABLE`](../ddl_alter_table)

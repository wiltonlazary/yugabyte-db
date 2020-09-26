---
title: ALTER DATABASE
linkTitle: ALTER DATABASE
summary: Alter database
description: ALTER DATABASE
block_indexing: true
menu:
  v1.3:
    identifier: api-ysql-commands-alter-db
    parent: api-ysql-commands
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `ALTER DATABASE` statement to redefine the attributes of a database.

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
    {{% includeMarkdown "../syntax_resources/commands/alter_database,alter_database_option.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/alter_database,alter_database_option.diagram.md" /%}}
  </div>
</div>

## Semantics

{{< note title="Note" >}}

Some options in DATABASE are under development.

{{< /note >}}

### *name*

Specify the name of the database to be altered.

### *tablespace_name*

Specify the new tablespace that is associated with the database.

### ALLOW_CONNECTIONS

Specify `false` to disallow connections to this database. Default is `true`, which allows this database to be cloned by any user with `CREATEDB` privileges.

### CONNECTION_LIMIT

Specify how many concurrent connections can be made to this database. Default of `-1` allows unlimited concurrent connections.

### IS_TEMPLATE

S`true` — This database can be cloned by any user with `CREATEDB` privileges.
Specify `false` to Only superusers or the owner of the database can clone it.

## See also

- [`CREATE DATABASE`](../ddl_create_database)
- [`CREATE TABLESPACE`](../ddl_create_tablespace)
- [`DROP DATABASE`](../ddl_drop_database)
- [`SET`](../cmd_set)

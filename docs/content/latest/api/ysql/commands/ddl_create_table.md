---
title: CREATE TABLE
linkTitle: CREATE TABLE
summary: Create a new table in a database
description: CREATE TABLE
menu:
  latest:
    identifier: api-ysql-commands-create-table
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_create_table/
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `CREATE TABLE` statement to create a new table in a database. It defines the table name, column names and types, primary key, and table properties.

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
    {{% includeMarkdown "../syntax_resources/commands/create_table,table_elem,column_constraint,table_constraint,key_columns,hash_columns,range_columns,storage_parameters,storage_parameter,index_parameters,references_clause.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_table,table_elem,column_constraint,table_constraint,key_columns,hash_columns,range_columns,storage_parameters,storage_parameter,index_parameters,references_clause.diagram.md" /%}}
  </div>
</div>

## Semantics

Create a table with *table_name*. If `qualified_name` already exists in the specified database, an error will be raised unless the `IF NOT EXISTS` clause is used.

### Primary key

Primary key can be defined in either `column_constraint` or `table_constraint`, but not in both.
There are two types of primary key columns:

- `Hash primary key columns`: The primary key may have zero or more leading hash-partitioned columns.
By default, only the first column is treated as the hash-partition column. But this behavior can be modified by explicit use of the HASH annotation.

- `Range primary key columns`: A table can have zero or more range primary key columns and it controls the top-level ordering of rows within a table (if there are no hash partition columns) or the ordering of rows among rows that share a common set of hash partitioned column values. By default, the range primary key columns are stored in ascending order. But this behavior can be controlled by explicit use of `ASC` or `DESC`.

For example, if the primary key specification is `PRIMARY KEY ((a, b) HASH, c DESC)` then columns `a` & `b` are used together to hash partition the table, and rows that share the same values for `a` and `b` are stored in descending order of their value for `c`.

If the primary key specification is `PRIMARY KEY(a, b)`, then column `a` is used to hash partition
the table and rows that share the same value for `a` are stored in ascending order of their value
for `b`.

### Foreign key

`FOREIGN KEY` and `REFERENCES` specifies that the set of columns can only contain values that are present in the referenced column(s) of the referenced table. It is used to enforce referential integrity of data.

### Unique

This enforces that the set of columns specified in the `UNIQUE` constraint are unique in the table, that is, no two rows can have the same values for the set of columns specified in the `UNIQUE` constraint.

### Check

This is used to enforce that data in the specified table meets the requirements specified in the `CHECK` clause.

### Default

This clause is used to specify a default value for the column. If an `INSERT` statement does not specify a value for the column, then the default value is used. If no default is specified for a column, then the default is NULL.

### Temporary or Temp

Using this qualifier will create a temporary table. Temporary tables are only visible in the current client session or transaction in which they are created and are automatically dropped at the end of the session or transaction. Any indexes created on temporary tables are temporary as well.

### Split Into

The `SPLIT INTO` clause specifies the number of tablets that will be created for the table. This is useful for two data center (2DC) deployments. See example below: [Create CDC table specifying number of tablets](#create-cdc-table-specifying-number-of-tablets).

### Storage parameters
Storage parameters [as defined by PostgreSQL](https://www.postgresql.org/docs/11/sql-createtable.html#SQL-CREATETABLE-STORAGE-PARAMETERS) are ignored and only present for compatibility with PostgreSQL.

## Examples

### Table with primary key

```postgresql
yugabyte=# CREATE TABLE sample(k1 int,
                               k2 int,
                               v1 int,
                               v2 text,
                               PRIMARY KEY (k1, k2));
```

In this example, the first column `k1` will be `HASH`, while second column `k2` will be `ASC`.

```
yugabyte=# \d sample
               Table "public.sample"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 k1     | integer |           | not null |
 k2     | integer |           | not null |
 v1     | integer |           |          |
 v2     | text    |           |          |
Indexes:
    "sample_pkey" PRIMARY KEY, lsm (k1 HASH, k2)
```

### Table with range primary key

```postgresql
yugabyte=# CREATE TABLE range(k1 int,
                              k2 int,
                              v1 int,
                              v2 text,
                              PRIMARY KEY (k1 ASC, k2 DESC));
```

### Table with check constraint

```postgresql
yugabyte=# CREATE TABLE student_grade(student_id int,
                                      class_id int,
                                      term_id int,
                                      grade int CHECK (grade >= 0 AND grade <= 10),
                                      PRIMARY KEY (student_id, class_id, term_id));
```

### Table with default value

```postgresql
yugabyte=# CREATE TABLE cars(id int PRIMARY KEY,
                             brand text CHECK (brand in ('X', 'Y', 'Z')),
                             model text NOT NULL,
                             color text NOT NULL DEFAULT 'WHITE' CHECK (color in ('RED', 'WHITE', 'BLUE')));
```

### Table with foreign key constraint

Define two tables with a foreign keys constraint.

```postgresql
yugabyte=# CREATE TABLE products(id int PRIMARY KEY,
                                 descr text);
yugabyte=# CREATE TABLE orders(id int PRIMARY KEY,
                               pid int REFERENCES products(id) ON DELETE CASCADE,
                               amount int);
```

Insert some rows.

```postgresql
yugabyte=# SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL SERIALIZABLE;
yugabyte=# INSERT INTO products VALUES (1, 'Phone X'), (2, 'Tablet Z');
yugabyte=# INSERT INTO orders VALUES (1, 1, 3), (2, 1, 3), (3, 2, 2);

yugabyte=# SELECT o.id AS order_id, p.id as product_id, p.descr, o.amount FROM products p, orders o WHERE o.pid = p.id;
```

```
order_id | product_id |  descr   | amount
----------+------------+----------+--------
        1 |          1 | Phone X  |      3
        2 |          1 | Phone X  |      3
        3 |          2 | Tablet Z |      2
(3 rows)
```

Inserting a row referencing a non-existent product is not allowed.

```postgresql
yugabyte=# INSERT INTO orders VALUES (1, 3, 3);
```

```
ERROR:  insert or update on table "orders" violates foreign key constraint "orders_pid_fkey"
DETAIL:  Key (pid)=(3) is not present in table "products".
```

Deleting a product will cascade to all orders (as defined in the `CREATE TABLE` statement above).

```postgresql
yugabyte=# DELETE from products where id = 1;
yugabyte=# SELECT o.id AS order_id, p.id as product_id, p.descr, o.amount FROM products p, orders o WHERE o.pid = p.id;
```

```
 order_id | product_id |  descr   | amount
----------+------------+----------+--------
        3 |          2 | Tablet Z |      2
(1 row)
```

### Table with unique constraint

```postgresql
yugabyte=# CREATE TABLE translations(message_id int UNIQUE,
                                     message_txt text);
```

### Create table specifying number of tablets

You can use the `CREATE TABLE` statement with the `SPLIT INTO` clause to specify the number of tablets for the table.
This is useful for two data center (2DC) deployments that require identical number of tablets on both clusters.

```postgresql
yugabyte=# CREATE TABLE tracking (id int PRIMARY KEY) SPLIT (INTO 10 TABLETS);
```

## See also

- [`ALTER TABLE`](../ddl_alter_table)
- [`CREATE TABLE AS`](../ddl_create_table_as)
- [`CREATE TABLESPACE`](../ddl_create_tablespace)
- [`DROP TABLE`](../ddl_drop_table)

---
title: The WITH clause [YSQL]
headerTitle: The WITH clause (Common Table Expression)
linkTitle: WITH clause
description: How to use the WITH clause a.k.a. Common Table Expression
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: with-clause
    parent: the-sql-language
    weight: 200
aliases:
  - /latest/api/ysql/with-clause/
isTocNested: true
showAsideToc: true
---

The `WITH` clause (sometimes known as the _common table expression_) can be used as part of a `SELECT` statement, an `INSERT` statement, an `UPDATE` statement, or a `DELETE` statement. For this reason, the functionality is described in this dedicated section.

For example, a `WITH` clause can be used to provide values for, say, an `INSERT` like this:

```plpgsql
set client_min_messages = warning;
drop table if exists t1 cascade;
create table t1(k int primary key, v int not null);

with a(k, v) as (
  select g.v, g.v*2 from generate_series(11, 20) as g(v)
  )
insert into t1
select k, v from a;

select k, v from t1 order by k;
```

This is the result:

```
 k  | v  
----+----
 11 | 22
 12 | 24
 13 | 26
 14 | 28
 15 | 30
 16 | 32
 17 | 34
 18 | 36
 19 | 38
 20 | 40
```

Moreover, data-changing substatements (`INSERT`, `UPDATE` , and `DELETE`) can be used in a `WITH` clause and, when these use a `RETURNING` clause, the returned values can be used in another data-changing statement like this:

```plpgsql
set client_min_messages = warning;
drop table if exists t2 cascade;
create table t2(k int primary key, v int not null);

with moved_rows as (
  delete from t1
  where k > 15
  returning k, v)
insert into t2(k, v)
select k, v from moved_rows;

(
  select 't1' as table_name, k, v from t1
  union all
  select 't2' as table_name, k, v from t2
  )
order by table_name, k;
```

The central notion is that a `WITH` clause lets you name one or more substatements (which each may be either `SELECT`, `VALUES`, `INSERT`, `UPDATE`, or `DELETE`) so that you can then refer to such a substatement by name, either in a subsequent substatement in that `WITH` clause or in the overall statement's final, main, substatement (which may be either `SELECT`, `INSERT`, `UPDATE`, or `DELETE`—but _not_ `VALUES`). In this way, a `WITH` clause substatement is analogous, in the declarative programming domain of SQL, to a procedure or a function in the imperative programming domain of a procedural language.

Notice that a schema-level view achieves the same effect, for `SELECT` or `VALUES`, as does a substatement in a view with respect to the named substatement's use. However a schema-level view cannot name a data-changing substatement. In this way, a `WITH` clause substatement brings valuable unique functionality. It also brings the modular programming benefit of hiding names, and the implementations that they stand for, from scopes that have no interest in them.

Finally, the use of a _recursive_ substatement in a `WITH` clause enables graph analysis. For example, an _"employees"_ table often has a self-referential foreign key like _"manager_id"_ that points to the table's primary key, _"employee_id"_. `SELECT` statements that use a recursive `WITH` clause allow the reporting structure to be presented in various ways. For example, this result shows the reporting paths of employees, in an organization with a strict hierarchical reporting scheme, in depth-first order. See the section [Pretty-printing the top-down depth-first report of paths](./emps-hierarchy/#pretty-printing-the-top-down-depth-first-report-of-paths).

```
 emps hierarchy 
----------------
 mary
   fred
     alfie
     dick
   george
   john
     alice
     bill
       joan
     edgar
   susan
```

The remainder of this section has the following subsections:

- [`WITH` clause—SQL syntax and semantics](./with-clause-syntax-semantics/)

- [The recursive `WITH` substatement](./recursive-with/)

- [Using a WITH clause recursive substatement to traverse a hierarchy](./emps-hierarchy/)

{{< tip title="Performance considerations." >}}

A SQL statement that uses a `WITH` clause sometimes gets a worse execution plan than the semantically equivalent statement that _doesn’t_ use a `WITH` clause. The explanation is usually that a “push-down” optimization of a restriction or projection hasn’t penetrated into the `WITH` clause’s substatement. You can usually avoid this problem by manually pushing down what you’d hope would be done automatically into your spellings of the `WITH` clause’s substatements.

Anyway, the ordinary good practice principle holds even more here: always check the execution plans of the SQL statements that your application issues, on representative samples of data, before moving the code to the production environment.

{{< /tip >}}


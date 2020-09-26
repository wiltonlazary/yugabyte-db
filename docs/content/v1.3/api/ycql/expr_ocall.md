---
title: Operator Call
summary: Compounding expression using operators.
description: Built-in Operator Call
block_indexing: true
menu:
  v1.3:
    parent: api-cassandra
    weight: 1360
isTocNested: true
showAsideToc: true
---

An expression with operators is a compound expression that combines multiple expressions using builtin operators. The following sections discuss the supported operators in Yugabyte.

## Nullary operations

| Operator | Description |
|----------|-------------|
| `EXISTS`, `NOT EXISTS` | predicate for existence of a row |

<li> `EXISTS` and `NOT EXISTS` can only be used in the `IF` clause.

## Unary operations

| Operator | Description |
|----------|-------------|
| `-` | numeric negation |
| `+` | no-op |
| `NOT` | Logical (boolean) negation |

<li> Unary `-` and `+` can only be used with constant expressions such as `-77`.

## Binary operations

| Operator | Description |
|----------|-------------|
| `OR`, `AND`| Logical (boolean) expression |
| `=`, `!=`, `<`, `<=`, `>`, `>=` | Comparison expression |
| `+` | Addition, append, or prepend |
| `-` | Subtraction or removal |
| `*` | Multiplication. Not yet supported |
| `/` | Division. Not yet supported |
| `ISNULL`, `IS NOT NULL` | Not yet supported comparison expression. |

<li>The first argument of comparison operators must be a column. For example, `column_name = 7`.</li>
<li>Comparing `NULL` with others always yields a `false` value. Operator `ISNULL` or `IS NULL` must be used when comparing with `NULL`.</li>
<li>When `+` and `-` are applied to a NULL argument of `COUNTER` data type, the NULL expression is replaced with a zero value before the computation. When these operators are applied to a NULL expression of all other numeric data types, the computed value is always NULL.</li>
<li>Operator `+` either prepends or appends a value to a LIST while operator `-` removes elements from LIST.</li>
<li>Operator `+` inserts new distinct elements to a MAP or SET while operator `-` removes elements from them.</li>

## See also

[All Expressions](..#expressions)

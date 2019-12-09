---
title: MONEY Datatypes
linktitle: Money
summary: MONEY Datatypes
description: MONEY Datatypes
menu:
  v1.2:
    identifier: api-ysql-datatypes-money
    parent: api-ysql-datatypes
isTocNested: true
showAsideToc: true
---

## Synopsis
MONEY datatype represents currency with a fixed precision for fraction.

Datatype | Description | Min | Max |
---------|-------------|-----|-----|
MONEY | 8 bytes | -92233720368547758.08 | +92233720368547758.07 |

## Description

```
type_specification ::= MONEY
```

- To avoid precision loss, MONEY value can be cast to NUMERIC type before applying calculations.


---
title: SERIAL Datatypes
linktitle: Serial
summary: SERIAL Datatypes
description: SERIAL Datatypes
menu:
  v1.2:
    identifier: api-ysql-datatypes-serial
    parent: api-ysql-datatypes
isTocNested: true
showAsideToc: true
---

## Synopsis
SMALLSERIAL, SERIAL, and BIGSERIAL are short notation for sequences of SMALLINTs, INTEGERs, and BIGINTs respectively.

## Description

```
type_specification ::= SMALLSERIAL | SERIAL | BIGSERIAL
```

- Columns of serial types are auto-incremented.
- `SERIAL` does not imply that an index is created on the column.


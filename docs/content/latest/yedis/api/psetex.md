---
title: PSETEX
linkTitle: PSETEX
description: PSETEX
menu:
  latest:
    parent: api-yedis
    weight: 2272
aliases:
  - /latest/api/redis/psetex
  - /latest/api/yedis/psetex
isTocNested: true
showAsideToc: true
---

## Synopsis

<b>`PSETEX key ttl_in_msec string_value`</b><br>
This command sets the value of `key` to be `string_value`, and sets the key to expire in `ttl_in_msec` milli-seconds.

## Return Value

Returns status string.

## Examples

```sh
$ PSETEX yugakey 1000 "Yugabyte"
```

```
"OK"
```

```sh
$ GET yugakey
```

```
"Yugabyte"
```

```sh
$ PTTL yugakey
```

```
(integer) 900 
```

## See also

[`append`](../append/), [`set`](../set/), [`setex`](../setex/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`setrange`](../setrange/), [`strlen`](../strlen/)

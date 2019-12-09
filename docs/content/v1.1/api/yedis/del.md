---
title: DEL
linkTitle: DEL
description: DEL
menu:
  v1.1:
    parent: api-redis
    weight: 2040
isTocNested: true
showAsideToc: true
---
Single-key request only. Requests with multiple keys are not yet supported.

## Synopsis
<!-- <b>`DEL key [key ...]`</b><br> -->
<b>`DEL key`</b><br>
This command deletes the given `key`.

<li>If the `key` does not exist, it is ignored and not counted toward the total number of removed keys.</li>

## Return Value
Returns number of keys that were removed.

## Examples

```sh
$ SET yuga1 "America"
```

```
"OK"
```

```sh
$ SET yuga2 "Africa"
```

```
"OK"
```

```sh
$ DEL yuga1
```

```
1
```

```sh
$ DEL not_a_key
```

```
0
```

```sh
$ DEL yuga1 yuga2
```

```
"ERR del: Wrong number of arguments"
```

## See Also
[`exists`](../exists/), [`flushall`](../flushall/), [`flushdb`](../flushdb/), [`get`](../get/), [`getrange`](../getrange/), [`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`sadd`](../sadd/), [`set`](../set/)

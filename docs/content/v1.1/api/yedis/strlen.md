---
title: STRLEN
linkTitle: STRLEN
description: STRLEN
menu:
  v1.1:
    parent: api-redis
    weight: 2320
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`STRLEN key`</b><br>
This command finds the length of the string value that is associated with the given `key`.
<li> If `key` is associated with a non-string value, an error is raised.</li>
<li> If `key` does not exist, 0 is returned.</li>

## Return Value
Returns length of the specified string.

## Examples

```sh
$ SET yugakey "string value"
```

```
"OK"
```

```sh
$ STRLEN yugakey
```

```
12
```

```sh
$ STRLEN undefined_key
```

```
0
```

## See Also
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`set`](../set/), [`setrange`](../setrange/)

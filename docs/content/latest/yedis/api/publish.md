---
title: PUBLISH
linkTitle: PUBLISH
description: PUBLISH
menu:
  latest:
    parent: api-yedis
    weight: 2551
aliases:
  - /latest/api/redis/publish
  - /latest/api/yedis/publish
isTocNested: true
showAsideToc: true
---

## Synopsis

<b>`PUBLISH channel message`</b><br>
This command publishes the given message to the specified channel. All subscribers that are subscribed to the specified channel
across all the Yugabyte YEDIS API server(s) in the cluster will receive the message.

## Return value

Returns, as an integer value, the number of subscribers that the message was forwarded to.

## Examples

```sh
$ PUBLISH channel message
```

```
2
```

## See also

[`pubsub`](../pubsub/), 
[`subscribe`](../subscribe/), 
[`unsubscribe`](../unsubscribe/), 
[`psubscribe`](../psubscribe/), 
[`punsubscribe`](../punsubscribe/)

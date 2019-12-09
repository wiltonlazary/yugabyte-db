---
title: 4. Test YEDIS API
linkTitle: 4. Test YEDIS API
description: Test YugabytE DIctionary Service (YEDIS) API
menu:
  v1.0:
    parent: quick-start
    weight: 140
---

After [creating a local cluster](../create-local-cluster/), follow the instructions below to test YugabyteDB's Redis-compatible YEDIS API.

[**redis-cli**](https://redis.io/topics/rediscli) is a command line interface to interact with a Redis server. For ease of use, YugabyteDB ships with the 4.0.1 version of redis-cli in its bin directory.

## 1. Connect with redis-cli

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#macos" class="nav-link active" id="macos-tab" data-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#linux" class="nav-link" id="linux-tab" data-toggle="tab" role="tab" aria-controls="linux" aria-selected="false">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
    <li>
    <a href="#docker" class="nav-link" id="docker-tab" data-toggle="tab" role="tab" aria-controls="docker" aria-selected="false">
      <i class="fab fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="#kubernetes" class="nav-link" id="kubernetes-tab" data-toggle="tab" role="tab" aria-controls="kubernetes" aria-selected="false">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/test-redis.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/test-redis.md" /%}}
  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/test-redis.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "kubernetes/test-redis.md" /%}}
  </div>
</div>


## 2. Simple key-value types

Insert a key and a value.

```sql
127.0.0.1:6379> set mykey somevalue
```

```
"OK"
```

Query the value by the key.

```sql
127.0.0.1:6379> get mykey
```

```
"somevalue"
```

Check if the key exists.

```sql
127.0.0.1:6379> exists mykey
```

```
(integer) 1
```


If the value is a number, it can be incremented.

```sql
127.0.0.1:6379> set counter 100
```

```
"OK"
```

```sql
127.0.0.1:6379> incr counter
```

```
(integer) 101
```

```sql
127.0.0.1:6379> incr counter
```

```
(integer) 102
```

```sql
127.0.0.1:6379> get counter
```

```
"102"
```


## 3. Hash data types

You can create a Redis Hash data type as follows. This models the data for user id 1000 with the following attributes {username : john, birthyear : 1977, verified : 1}.

```sql
127.0.0.1:6379> hmset user:1000 username john birthyear 1977 verified 1
```

```sh
OK
```

You can retrieve specific attributes for user id 1000 as follows.

```sql
127.0.0.1:6379> hget user:1000 username
```

```sh
"john"
```

```sql
127.0.0.1:6379> hget user:1000 birthyear
```

```sh
"1977"
```

You can fetch multiple attributes with a single command as follows.

```sql
127.0.0.1:6379> hmget user:1000 username birthyear no-such-field
```

```sh
1) "john"
2) "1977"
3) (nil)
```

You can fetch all attributes by using the `hgetall` command.

```sql
127.0.0.1:6379> hgetall user:1000
```

```sh
1) "birthyear"
2) "1977"
3) "username"
4) "john"
5) "verified"
6) "1"
```

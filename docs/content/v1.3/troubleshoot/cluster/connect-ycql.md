---
title: YCQL connection issues
linkTitle: YCQL connection issues
description: Cannot connect to YCQL API
block_indexing: true
menu:
  v1.3:
    parent: troubleshoot-cluster
    weight: 822
isTocNested: true
showAsideToc: true
---

## 1. Are YugabyteDB processes running?

First, ensure that the expected YugabyteDB processes on the current node.
At a minimum, the tserver process needs to be running to be able to connect to this node with a CQL client or application.
Additionally, depending on the setup, you might expect a master process to also be running on this node.
Follow the instructions on the [Check Processes](../../nodes/check-processes/) page.

## 2. Is the Cassandra-compatible YCQL API running?

If the tserver process is running, make sure the YCQL API is enabled and listening on the YCQL port (default `9042`).

```sh
$ lsof -i :9042
```
```
COMMAND     PID   USER   FD   TYPE     DEVICE SIZE/OFF NODE NAME
yb-tserve 81590 centos  131u  IPv4 0xdeadbeef      0t0  TCP localhost:9042 (LISTEN)
```
Note: You may need to install `lsof` first.

When running a local cluster with `yb-ctl` you should see all the nodes here with different IPs. For instance:
```sh
$ lsof -i :9042
```
```
COMMAND     PID   USER   FD   TYPE     DEVICE SIZE/OFF NODE NAME
yb-tserve 81590 centos  131u  IPv4 0xdeadbeef      0t0  TCP localhost:9042 (LISTEN)
yb-tserve 81593 centos  131u  IPv4 0xdeadbeef      0t0  TCP 127.0.0.2:9042 (LISTEN)
yb-tserve 81596 centos  131u  IPv4 0xdeadbeef      0t0  TCP 127.0.0.3:9042 (LISTEN)
```
If there is another process using this port you might need to stop that and restart the tserver process.
Otherwise, if no process is listening but the tserver is running, check the value of the `--cql_proxy_bind_address` flag passed to the 
tserver process.

## 3. Can cqlsh connect locally?

Once on the machine where YugabyteDB is running, use `cqlsh` to connect to the local YugabyteDB instance.
Depending on your installation, you may need to install `cqlsh`, otherwise you can find it in the Yugabyte `bin` directory. 
Try running:
```sh
$ cqlsh <yb-local-address>
```
where `<yb-local-address>` is the address where the Yugabyte CQL server is listening (e.g. as returned by `lsof`). For instance, in the example above, it is `localhost` (or, additionally, `127.0.0.2` and `127.0.0.3` for the `yb-ctl` case).

If `cqlsh` can connect, the issue is likely a network issue with the original client not being able to access this node where YugabyteDB is running. See also [Are Master or TServer Endpoints Accessible?](../../nodes/check-processes#cannot-access-master-or-tserver-endpoints).

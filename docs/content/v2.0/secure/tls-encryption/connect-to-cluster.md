---
title: Connect to clusters
linkTitle: Connect to clusters
description: Connect to clusters
headcontent: Connect to clusters
image: /images/section_icons/secure/tls-encryption/connect-to-cluster.png
block_indexing: true
menu:
  v2.0:
    identifier: connect-to-cluster
    parent: tls-encryption
    weight: 40
isTocNested: true
showAsideToc: true
---

To connect CLIs, tools, and APIs to a remote YugabyteDB cluster when client-to-server encryption is enabled, you need to generate client certificate files that enable the client to connect to the YugabyteDB cluster.

## Prerequisites

Before you can enable and use server-to-server encryption, you need to create and configure server certificates for each node of your YugabyteDB cluster. For information, see [Create client certificates](../client-certificates).

For each client that will connect to a YugabyteDB cluster, you need the following three files to be accessible on the client computer.

- `root.crt` — root certificate file
  - To generate, see [Prepare root configuration file](../prepare-nodes/#generate-root-config)
- `yugabytedb.crt` — private node certificate
  - To generate, see [Generate private key for each node](../prepare-nodes/#generate-private-key-for-each-node)
- `yugabytedb.key` — private node key
  - To generate, see [Generate private key for each node](../prepare-nodes/#generate-private-key-for-each-node)

All three files should be available in the `~/.yugabytedb`, the default location for TLS certificates when running the YSQL shell (`ysqlsh`) locally.

## Connect to a YugabyteDB cluster

For each of the clients below, the steps assume that you have:

- Added the required client certificates to the `~/.yugabytedb` directory (or a directory specified using the `--certs_for_clients_dir` option). For details, see [Create client certificates](../client-certificates).
- [Enabled client-to-server encryption](../client-to-server) on the YB-TServer nodes of your YugabyteDB cluster.
- [Enabled server-to-server encryption](../server-to-server) on the YugabyteDB cluster.

## ysqlsh

To open the YSQL shell (`ysqlsh`) using a YugabyteDB cluster with encryption enabled, you need to add configuration options (flags) to the  

The `ysqlsh` CLI is available in the `bin` directory of your YugabyteDB home directory.

To connect to a remote YugabyteDB cluster, you need to have a local copy of `ysqlsh` available. You can use the `ysqlsh` CLI available on a locally installed YugabyteDB.

To open the local `ysqlsh` CLI and access your YugabyteDB cluster, run `ysqlsh` with the following configuration options set:

- host: `-h <node-ip-address>` (required for remote node; default is `127.0.0.1`)
- port: `-p <port>` (optional; default is `5433`)
- user: `-U <username>` (optional; default is `yugabyte`)
- TLS/SSL: `"sslmode=require"` (this flag is required)

```sh
$ ./bin/ysqlsh -h 127.0.0.1 -p 5433 -U yugabyte "sslmode=require"
```

```
$ ./bin/ysqlsh
ysqlsh (11.2-YB-2.0.11.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.

yugabyte=#
```

## yb-admin

To enable `yb-admin` to connect with a cluster having TLS enabled, pass in the extra argument of `certs_dir_name` with the directory location where the root certificate is present. The `yb-admin` tool is present on the cluster node in the `~/master/bin/` directory. The `~/yugabyte-tls-config` directory on the cluster node contains all the certificates.

For example, the command below will list the master information for the TLS enabled cluster:

```sh
export MASTERS=node1:7100,node2:7100,node3:7100
./bin/yb-admin --master_addresses $MASTERS -certs_dir_name ~/yugabyte-tls-config list_all_masters
```

You should see the following output format:

```sh
Master UUID	RPC Host/Port	State	Role
UUID_1 		node1:7100  	ALIVE 	FOLLOWER
UUID_2		node2:7100     	ALIVE 	LEADER
UUID_3 		node3:7100     	ALIVE 	FOLLOWER
```

## cqlsh

To enable `cqlsh` to connect to a YugabyteDB cluster with encryption enabled, you need to set the following environment variables:

Variable       | Description                  |
---------------|------------------------------|
`SSL_CERTFILE` | The root certificate file (`ca.crt`). |
`SSL_USERCERT` | The user certificate file  (`node.<name>.crt`). |
`SSL_USERKEY`  | The user key file (`node.<name>.key`).  |

To set the environment variables, use the following `export` commands:

```sh
$ export SSL_CERTFILE=<path to file>/ca.crt
$ export SSL_USERCERT=<path to file>/node.<name>.crt
$ export SSL_USERKEY=<path to file>/node.<name>.key
```

Next connect using the `--ssl` flag.

### Local cluster

```sh
$ ./bin/cqlsh --ssl
```

You should see the following output:

```sql
Connected to local cluster at X.X.X.X:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> DESCRIBE KEYSPACES;

system_schema  system_auth  system
```

### Remote cluster

To connect to a remote YugabyteDB cluster, you need to have a local copy of `cqlsh` available. You can usse the `cqlsh` CLI available on a locally installed YugabyteDB.

To open the local `cqlsh` CLI and access the remote cluster, run `cqlsh` with configuration options set for the host and port of the remote cluster. You must also add the `--ssl` flag to enable the use of the client-to-server encryption using TLS (successor to SSL).

```sh
$ ./bin/cqlsh -h <node-ip-address> -p <port> --ssl
```

- *node-ip-address*: the IP address of the remote node.
- *port*: the port of the remote node.

For example, if the host is `127.0.0.2`, the port is `5433`, and the user is `yugabyte`, run the following command to connect:

```sh
$ ./bin/cqlsh -h 127.0.0.2 -p 5433 --ssl -U yugabyte
```

You should see the following output:

```sql
Connected to local cluster at X.X.X.X:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> DESCRIBE KEYSPACES;

system_schema  system_auth  system
```

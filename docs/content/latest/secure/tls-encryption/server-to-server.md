---
title: 2. Server-server encryption
linkTitle: 2. Server-server encryption
description: 2. Server-server encryption
headcontent: Enable server to server encryption between YB-Masters and YB-TServers.
image: /images/section_icons/secure/tls-encryption/server-to-server.png
aliases:
  - /secure/tls-encryption/server-to-server
menu:
  latest:
    identifier: server-to-server
    parent: tls-encryption
    weight: 742
isTocNested: true
showAsideToc: true
---

To enable server-server (or node-to-node) encryption, start the YB-Master and YB-TServer servers using the appropriate configuration options described here.

Configuration option           | Service                  | Description                  |
-------------------------------|--------------------------|------------------------------|
`use_node_to_node_encryption`  | YB-Master, YB-TServer | Optional, default value is `false`. Set to `true` to enable encryption between the various YugabyteDB server processes. |
`allow_insecure_connections`   | YB-Master only           | Optional, defaults to `true`. Set to `false` to disallow any process with unencrypted communication from joining this cluster. Default value is `true`. Note that this flag requires the `use_node_to_node_encryption` to be enabled. |
`certs_dir`                    | YB-Master, YB-TServer | Optional. This directory should contain the configuration that was prepared in the a step for this node to perform encrypted communication with the other nodes. Default value for YB-Masters is `<data drive>/yb-data/master/data/certs` and for YB-TServers this location is `<data drive>/yb-data/tserver/data/certs` |

## Start the YB-Master server

You can enable access control by starting the `yb-master` processes minimally with the `--use_node_to_node_encryption=true` configuration option as described above. Your command should look similar to that shown below:

```
bin/yb-master                               \
    --fs_data_dirs=<data directories>       \
    --master_addresses=<master addresses>   \
    --certs_dir=/home/centos/tls/$NODE_IP   \
    --allow_insecure_connections=false      \
    --use_node_to_node_encryption=true
```

You can read more about bringing up the YB-Masters for a deployment in the section on [manual deployment of a YugabyteDB cluster](../../../deploy/manual-deployment/start-masters/).

## Start the YB-TServer server

You can enable access control by starting the `yb-tserver` server minimally with the `--use_node_to_node_encryption=true` flag as described above. Your command should look similar to that shown below:

```
bin/yb-tserver                                  \
    --fs_data_dirs=<data directories>           \
    --tserver_master_addrs=<master addresses>   \
    --certs_dir /home/centos/tls/$NODE_IP       \
    --use_node_to_node_encryption=true &
```

For more about bringing up the YB-TServers for a deployment, see the section on [manual deployment of a YugabyteDB cluster](../../../deploy/manual-deployment/start-tservers/).

## Connect to the cluster

Since we have only enabled encryption between the database servers, we should be able to connect to this cluster using `cqlsh` without enabling encryption as shown below.

```sh
$ ./bin/cqlsh
```

```
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```

```sh
cqlsh> DESCRIBE KEYSPACES;
```

```sh
system_schema  system_auth  system

cqlsh>
```

{{< note title="Note" >}}
Since we have not enforced client to server encrypted communication, connecting to this cluster using `cqlsh` without TLS encryption enabled would work.
{{< /note >}}

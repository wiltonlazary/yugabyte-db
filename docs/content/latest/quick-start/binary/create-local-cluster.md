## 1. Create a local cluster

You can use the [`yb-ctl`](../../admin/yb-ctl/) utility, located in the `bin` directory of the YugabyteDB package, to create and administer a local cluster. The default data directory is `$HOME/yugabyte-data`. You can change the location of the data directory by using the [`--data_dir` configuration option](../../admin/yb-ctl/#data-dir).

To quickly create a 1-node or 3-node local cluster, follow the steps below. For details on using the `yb-ctl create` command and the cluster configuration, see [Create a local cluster](../../admin/yb-ctl/#create-cluster) in the utility reference.

### Create a 1-node cluster with RF of 1

To create a 1-node cluster with a replication factor (RF) of 1, run the following `yb-ctl create` command. 

```sh
$ ./bin/yb-ctl create
```

The initial cluster creation may take a minute or so **without any output** on the prompt.

### Create a 3-node cluster with RF of 3

To run a distributed SQL cluster locally for testing and development, you can quickly create a 3-node cluster with RF of 3 by running the following command.

```sh
$ ./bin/yb-ctl --rf 3 create
```

You can now check `$HOME/yugabyte-data` to see `node-i` directories created where `i` represents the `node_id` of the node. Inside each such directory, there will be two disks, `disk1` and `disk2`, to highlight the fact that YugabyteDB can work with multiple disks at the same time. Note that the IP address of `node-i` is by default set to `127.0.0.i`.

Clients can now connect to the YSQL and YCQL APIs at `localhost:5433` and `localhost:9042` respectively.

## 2. Check cluster status with "yb-ctl status"

To see the `yb-master` and `yb-tserver` processes running locally, run the `yb-ctl status` command.

### Example

For a 1-node cluster, the `yb-ctl status` command will show that you have 1 `yb-master` process and 1 `yb-tserver` process running on the localhost. For details about the roles of these processes in a YugabyteDB cluster (aka Universe), see [Universe](../../architecture/concepts/universe/).

```sh
$ ./bin/yb-ctl status
```

```
----------------------------------------------------------------------------------------------------
| Node Count: 1 | Replication Factor: 1                                                            |
----------------------------------------------------------------------------------------------------
| JDBC                : jdbc:postgresql://127.0.0.1:5433/postgres                                  |
| YSQL Shell          : bin/ysqlsh                                                                 |
| YCQL Shell          : bin/cqlsh                                                                  |
| YEDIS Shell         : bin/redis-cli                                                              |
| Web UI              : http://127.0.0.1:7000/                                                     |
| Cluster Data        : /Users/yugabyte/yugabyte-data                                             |
----------------------------------------------------------------------------------------------------
----------------------------------------------------------------------------------------------------
| Node 1: yb-tserver (pid 20696), yb-master (pid 20693)                                            |
----------------------------------------------------------------------------------------------------
| JDBC                : jdbc:postgresql://127.0.0.1:5433/postgres                                  |
| YSQL Shell          : bin/ysqlsh                                                                 |
| YCQL Shell          : bin/cqlsh                                                                  |
| YEDIS Shell         : bin/redis-cli                                                              |
| data-dir[0]         : /Users/yugabyte/yugabyte-data/node-1/disk-1/yb-data                       |
| yb-tserver Logs     : /Users/yugabyte/yugabyte-data/node-1/disk-1/yb-data/tserver/logs          |
| yb-master Logs      : /Users/yugabyte/yugabyte-data/node-1/disk-1/yb-data/master/logs           |
----------------------------------------------------------------------------------------------------
```

## 3. Check cluster status with Admin UI

Node 1's [YB-Master Admin UI](../../reference/configuration/yb-master/#admin-ui) is available at `http://127.0.0.1:7000` and the [YB-TServer Admin UI](../../reference/configuration/yb-tserver/#admin-ui) is available at `http://127.0.0.1:9000`. If you created a multi-node cluster, you can visit the other nodes' Admin UIs by using their corresponding IP addresses.

### 3.1 Overview and Master status

Node 1's master Admin UI home page shows that we have a cluster (aka a Universe) with `Replication Factor` of 1 and `Num Nodes (TServers)` as 1. The `Num User Tables` is 0 since there are no user tables created yet. YugabyteDB version number is also shown for your reference.

![master-home](/images/admin/master-home-binary-rf1.png)

The Masters section highlights the 1 yb-master along with its corresponding cloud, region and zone placement.

### 3.2 TServer status

Clicking on the `See all nodes` takes us to the Tablet Servers page where we can observe the 1 tserver along with the time since it last connected to this master via regular heartbeats. Since there are no user tables created yet, we can see that the `Load (Num Tablets)` is 0. As new tables get added, new tablets (aka shards) will get automatically created and distributed evenly across all the available tablet servers.

![master-home](/images/admin/master-tservers-list-binary-rf1.png)

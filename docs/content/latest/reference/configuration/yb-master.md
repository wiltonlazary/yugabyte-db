---
title: yb-master
linkTitle: yb-master
description: yb-master
menu:
  latest:
    identifier: yb-master
    parent: configuration
    weight: 2450
aliases:
  - admin/yb-master
isTocNested: 3
showAsideToc: true
---

Use the `yb-master` binary and its options to configure the [YB-Master](../../../architecture/concepts/yb-master) server. The `yb-master` executable file is located in the `bin` directory of YugabyteDB home.

## Syntax

```sh
yb-master [ option  ] | [ option ]
```

### Example

```sh
$ ./bin/yb-master \
--master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--rpc_bind_addresses 172.151.17.130 \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
--replication_factor=3
```

### Online help

To display the online help, run `yb-master --help` from the YugabyteDB home directory.

```sh
$ ./bin/yb-master --help
```

## Configuration options

- [General](#general-options)
- [YSQL](#ysql-options)
- [Logging](#logging-options)
- [Cluster](#cluster-options)
- [Geo-distribution](#geo-distribution-options)
- [Security](#security-options)
- [Change data capture (CDC)](#change-data-capture-cdc-options)

---

### General options

#### --version

Shows version and build information, then exits.

#### --flagfile

Specifies the configuration file to load flags from.

#### --master_addresses

Specifies a comma-separated list of all RPC addresses for `yb-master` consensus-configuration.

{{< note title="Note" >}}

The number of comma-separated values should match the total number of YB-Master server (or the replication factor).

{{< /note >}}

Mandatory.

Default: `127.0.0.1:7100`

#### --fs_data_dirs

Specifies a comma-separated list of directories where the `yb-master` will place all it's `yb-data/master` data directory.

Mandatory.

#### --fs_wal_dirs

The directory where the `yb-master` will place its write-ahead logs. May be the same as one of the directories listed in `--fs_data_dirs`, but not a sub-directory of a data directory.

Default: Same value as `--fs_data_dirs`

#### --rpc_bind_addresses

Specifies a comma-separated list of addresses to bind to for RPC connections.

Mandatory.

Default: `0.0.0.0:7100`

#### --server_broadcast_addresses

Specifies the public IP or DNS hostname of the server (along with an optional port).

Default: `0.0.0.0:7100`

#### --webserver_interface

Specifies the bind address for web server user interface access.

Default: `0.0.0.0`

#### --webserver_port

Specifies the web server monitoring port.

Default: `7000`

#### --webserver_doc_root

Monitoring web server home.

Default: The `www` directory in the YugabyteDB home directory.

---

### YSQL options

#### --enable_ysql

Enables the YSQL API when value is `true`. Replaces the deprecated `--start_pgsql_proxy` option.

Default: `true`

---

### Logging options

#### --alsologtoemail

Sends log messages to these email addresses in addition to logfiles.

Default: `""`

#### --colorlogtostderr

Color messages logged to `stderr` (if supported by terminal).

Default: `false`

#### --logbuflevel

Buffer log messages logged at this level (or lower).

Valid values: `-1` (don't buffer); `0` (INFO); `1` (WARN); `2` (ERROR); `3` (FATAL)

Default: `0`

#### --logbufsecs

Buffer log messages for at most this many seconds.

Default: `30`

#### --logemaillevel

Email log messages logged at this level, or higher. 

Values: `0` (all); `1` (WARN), `2` (ERROR), `3` (FATAL), `999` (none)

Default: `999`

#### --logmailer

The mailer used to send logging email messages.

Default: `"/bin/mail"

#### --logtostderr

Write log messages to `stderr` instead of `logfiles`.

Default: `false`

#### --log_dir

The directory to write `yb-master` log files.

Default: Same as [`--fs_data_dirs`](#fs-data-dirs)

#### --log_link

Put additional links to the log files in this directory.

Default: `""`

#### --log_prefix

Prepend the log prefix to each log line.

Default:  `true`

#### --max_log_size

The maximum log size, in megabytes (MB). A value of `0` will be silently overridden to `1`.

Default: `1800` (1.8 GB)

#### --minloglevel

The minimum level to log messages. Values are: `0` (INFO), `1` (WARN), `2` (ERROR), `3` (FATAL).

Default: `0` (INFO)

#### --stderrthreshold

Log messages at, or above, this level are copied to `stderr` in addition to log files.

Default: `2`

---

### Cluster options

#### --yb_num_shards_per_tserver

Specifies the number of shards per YB-TServer per table when a user table is created.

Default: Server automatically picks a valid default internally, typically 8.

#### --max_clock_skew_usec

The expected maximum clock skew, in microseconds (µs), between any two servers in your deployment.

Default: `50000` (50,000 µs = 50ms)

#### --replication_factor

The number of replicas, or copies of data, to store for each tablet in the universe.

Default: `3`

---

### Geo-distribution options

Settings related to managing geo-distributed clusters and Raft consensus.

#### --leader_failure_max_missed_heartbeat_periods

The maximum heartbeat periods that the leader can fail to heartbeat in before the leader is considered to be failed. The total failure timeout, in milliseconds, is [`--raft_heartbeat_interval_ms`](#raft-heartbeat-interval-ms) multiplied by `--leader_failure_max_missed_heartbeat_periods`.

For read replica clusters, set the value to `10` on both YB-Master and YB-TServer servers.  Because the the data is globally replicated, RPC latencies are higher. Use this flag to increase the failure detection interval in such a higher RPC latency deployment.

Default: `6`

#### --placement_zone

The name of the availability zone (AZ), or rack, where this instance is deployed.

Default: `rack1`

#### --placement_region

Name of the region or data center where this instance is deployed.

Default: `datacenter1`

#### --placement_cloud

Name of the cloud where this instance is deployed.

Default: `cloud1`

#### --use_private_ip

Determines when to use private IP addresses. Possible values are `never` (default),`zone`,`cloud` and `region`. Based on the values of the `placement_*` configuration options.

Default: `never`

#### --raft_heartbeat_interval_ms

The heartbeat interval, in milliseconds, for Raft replication. The leader produces heartbeats to followers at this interval. The followers expect a heartbeat at this interval and consider a leader to have failed if it misses several in a row.

Default: `500`

---

### Security options

For details on enabling server-server encryption, see [Server-server encryption](../../secure/tls-encryption/server-to-server).

#### --certs_dir

Directory that contains certificate authority, private key, and certificates for this server.

Default: `""` (Uses `<data drive>/yb-data/master/data/certs`.)

#### --allow_insecure_connections

Allow insecure connections. Set to `false` to prevent any process with unencrypted communication from joining a cluster. Note that this option requires the [`use_node_to_node_encryption`](#use-node-to-node-encryption) to be enabled.

Default: `true`

#### --dump_certificate_entries

Dump certificate entries.

Default: `false`

#### --use_node_to_node_encryption

Enable server-server, or node-to-node, encryption between YugabyteDB YB-Master and YB-TServer servers in a cluster or universe. To work properly, all YB-Master servers must also have their [`--use_node_to_node_encryption`](../yb-master/#use-node-to-node-encryption) setting enabled. When enabled, then [`--allow_insecure_connections`](#allow-insecure-connections) must be disabled.

Default: `false`

---

### Change data capture (CDC) options

To learn more about CDC, see [Change data capture (CDC)](../../architecture/#cdc-architecture).

For other CDC configuration options, see [YB-TServer's CDC options](../yb-tserver/#change-data-capture-cdc-options).

#### --cdc_state_table_num_tablets

The number of tablets to use when creating the CDC state table.

Default: `0` (Use the same default number of tablets as for regular tables.)

#### --cdc_wal_retention_time_secs

WAL retention time, in seconds, to be used for tables for which a CDC stream was created. If you change the value, make sure that [`yb-tserver --cdc_wal_retention_time_secs`](../yb-tserver/#cdc-wal-retention-time-secs) is also updated with the same value.

Default: `14400` (4 hours)

## Admin UI

The Admin UI for yb-master is available at http://localhost:7000.

### Home

Home page of the YB-Master server that gives a high level overview of the cluster. Note all YB-Master servers in a cluster show identical information.

![master-home](/images/admin/master-home-binary-with-tables.png)

### Tables

List of tables present in the cluster.

![master-tables](/images/admin/master-tables.png)

### Tablet servers

List of all nodes (aka YB-TServer servers) present in the cluster.

![master-tservers](/images/admin/master-tservers-list-binary-with-tablets.png)

### Debug

List of all utilities available to debug the performance of the cluster.

![master-debug](/images/admin/master-debug.png)

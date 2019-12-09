---
title: YB-TServer service
linkTitle: YB-TServer service
description: YB-TServer service
menu:
  latest:
    identifier: yb-tserver
    parent: configuration
    weight: 2440
aliases:
  - admin/yb-tserver
isTocNested: 3
showAsideToc: true
---

The [YB-TServer](../../../architecture/concepts/yb-tserver/) binary (`yb-tserver`) is located in the `bin` directory of YugabyteDB home.

## Syntax

```sh
yb-tserver [ options ]
```

### Example

```sh
$ ./bin/yb-tserver \
--tserver_master_addrs 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--rpc_bind_addresses 172.151.17.130 \
--start_pgsql_proxy \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" &
```

### Online help

To display the online help, run `yb-tserver --help` from the YugabyteDB home directory.

```sh
$ ./bin/yb-tserver --help
```

## Configuration options

- [Help](#help-options)
- [General](#general-options)
- [Logging](#logging-options)
- [Geo-distribution](#geo-distribution-options)
- [YSQL](#ysql-options)
- [YCQL](#ycql-options)
- [YEDIS](#yedis-options)
- [Performance](#performance-options)
- [Write Ahead Log (WAL)](#write-ahead-log-wal-options)
- [Security](#security-options)
- [Change data capture (CDC)](#change-data-capture-cdc-options)

---

### Help options

#### --help

Displays help on all options.

#### --helpon

Displays help on modules named by the specified option (or flag) value.

---

### General options

#### --flagfile

Specifies the file to load the configuration options (or flags) from. The configuration settings, or flags, must be in the same format as supported by the command line options.

#### --version

Shows version and build info, then exits.

#### --tserver_master_addrs

Comma-separated list of all the `yb-master` RPC addresses. Mandatory.

{{< note title="Note" >}}

The number of comma-separated values should match the total number of YB-Master services (or the replication factor).

{{< /note >}}

Default: `127.0.0.1:7100`

#### --fs_data_dirs

Comma-separated list of directories where the `yb-tserver` will place it's `yb-data/tserver` data directory. Mandatory.

#### --max_clock_skew_usec

Specifies the expected maximum clock skew, in microseconds (µs) between any two nodes in your deployment.

Default: `50000` (50,000 µs = 50ms)

#### --rpc_bind_addresses

Specifies the comma-separated list of addresses to bind to for RPC connections.

Default: `0.0.0.0:9100`

#### --server_broadcast_addresses

Public IP or DNS hostname of the server (with an optional port).

Default: `0.0.0.0:9100`

#### --use_private_ip

Determines when to use private IP addresses. Possible values are `never` (default),`zone`,`cloud` and `region`. Based on the values of the [placement (`--placement_*`) configuration options](#placement-options).

Default: `never`

#### --webserver_interface

Address to bind for the web server user interface.

Default: `0.0.0.0` (`127.0.0.1`)

#### --webserver_port

The port for monitoring the web server.

Default: `9000`

#### --webserver_doc_root

Monitoring web server home.

Default: The `www` directory in the YugabyteDB home directory.

---

### Logging options

#### --log_dir

The directory to write `yb-tserver` log files.

Default: Same as [`--fs_data_dirs`](#fs-data-dirs)

#### --logemaillevel

Email log messages logged at this level, or higher. Values: `0` (all), 1, 2, `3` (FATAL), `999` (none)

Default: `999`

#### --logmailer

The mailer used to send logging email messages.

Default: `"/bin/mail"

#### --logtostderr

Write log messages to `stderr` instead of `logfiles`.

Default: `false`

#### --max_log_size

The maximum log size, in megabytes (MB). A value of `0` will be silently overridden to `1`.

Default: `1800` (1.8 GB)

#### --minloglevel

The minimum level to log messages. Values are: `0` (INFO), `1`, `2`, `3` (FATAL).

Default: `0` (INFO)

#### --stderrthreshold

Log messages at, or above, this level are copied to `stderr` in addition to log files.

Default: `2`

#### --stop_logging_if_full_disk

Stop attempting to log to disk if the disk is full.

Default: `false`

---

### Geo-distribution options

Settings related to managing geo-distributed clusters and Raft consensus.

#### --leader_failure_max_missed_heartbeat_periods

The maximum heartbeat periods that the leader can fail to heartbeat in before the leader is considered to be failed. The total failure timeout, in milliseconds (ms), is [`--raft_heartbeat_interval_ms`](#raft-heartbeat-interval-ms) multiplied by `--leader_failure_max_missed_heartbeat_periods`.

For read replica clusters, set the value to `10` on both YB-Master and YB-TServer services.  Because the the data is globally replicated, RPC latencies are higher. Use this flag to increase the failure detection interval in such a higher RPC latency deployment.

Default: `6`

#### --placement_zone

The name of the availability zone, or rack, where this instance is deployed.

Default: `rack1`

#### --placement_region

Specifies the name of the region, or data center, where this instance is deployed.

Default: `datacenter1`

#### --placement_cloud

Specifies the name of the cloud where this instance is deployed.

Default: `cloud1`

#### --raft_heartbeat_interval_ms

The heartbeat interval, in milliseconds (ms), for Raft replication. The leader produces heartbeats to followers at this interval. The followers expect a heartbeat at this interval and consider a leader to have failed if it misses several in a row.

Default: `500`

---

### YSQL options

The following options, or flags, support the use of the [YSQL API](../../api/ysql/).

#### --enable_ysql

Enables the YSQL API. Replaces the deprecated `--start_pgsql_proxy` option.

Default: `true`

#### --ysql_enable_auth

Enables YSQL authentication.

{{< note title="Note" >}}

**Yugabyte 2.0:** Assign a password for the default `yugabyte` user to be able to sign in after enabling YSQL authentication.

**Yugabyte 2.0.1:** When YSQL authentication is enabled, you can sign into `ysqlsh` using the default `yugabyte` user that has a default password of `yugabyte".

{{< /note >}}

Default: `false`

#### --pgsql_proxy_bind_address

Specifies the TCP/IP bind addresses for the YSQL API. The default value of `0.0.0.0:5433` allows listening for all IPv4 addresses access to localhost on port `5433`. The `--pgsql_proxy_bind_address` value overwrites `listen_addresses` (default value of `127.0.0.1:5433`) that controls which interfaces accept connection attempts.

To specify fine-grained access control over who can access the server, use [`--ysql_hba_conf`](#ysql-hba-conf).

Default: `0.0.0.0:5433`

{{< note title="Note" >}}

When using local YugabyteDB clusters built using the

{{< /note >}}

#### --pgsql_proxy_webserver_port

Specifies the web server port for YSQL metrics monitoring.

Default: `13000`

#### --ysql_hba_conf

Specifies a comma-separated list of PostgreSQL client authentication settings that is written to the `ysql_hba.conf` file.

For details on using `--ysql_hba_conf` to specify client authentication, see [Configure YSQL client authentication](../secure/authentication/ysql-client-authentication.md).

Default: `"host all all 0.0.0.0/0 trust,host all all ::0/0 trust"`

#### --ysql_pg_conf

Comma-separated list of PostgreSQL setting assignments.

#### --ysql_timezone

Specifies the time zone for displaying and interpreting timestamps.

Default: Uses the YSQL time zone.

#### --ysql_datestyle

Specifies the display format for data and time values.

Default: Uses the YSQL display format.

#### --ysql_max_connections

Specifies the maximum number of concurrent YSQL connections.

Default: `300`

#### --ysql_default_transaction_isolation

Specifies the default transaction isolation level.

Valid values: `READ UNCOMMITTED`, `READ COMMITTED`, `REPEATABLE READ`, and `SERIALIZABLE`.

Default: `READ COMMITTED` (implemented in YugabyteDB as `REPEATABLE READ`)

{{< note title="Note" >}}

YugabyteDB supports two transaction isolation levels: `REPEATABLE READ` (aka snapshot) and `SERIALIZABLE`. The transaction isolation levels of `READ UNCOMMITTED` and `READ COMMITTED` are implemented in YugabyteDB as `REPEATABLE READ`.

{{< /note >}}

#### --ysql_log_statement

Specifies the types of YSQL statements that should be logged. 

Valid values: `none` (off), `ddl` (only data definition queries, such as create/alter/drop), `mod` (all modifying/write statements, includes DDLs plus insert/update/delete/trunctate, etc), and `all` (all statements).

Default: `none`

#### --ysql_log_min_messages

Specifies the lowest YSQL message level to log.

---

### YCQL options

The following options, or flags, support the use of the [YCQL API](../../api/ycql/).

#### --use_cassandra_authentication

Specify `true` to enable YCQL authentication (`username` and `password`), enable YCQL security statements (`CREATE ROLE`, `DROP ROLE`, `GRANT ROLE`, `REVOKE ROLE`, `GRANT PERMISSION`, and `REVOKE PERMISSION`), and enforce permissions for YCQL statements.

Default: `false`

#### --cql_proxy_bind_address

Specifies the bind address for the YCQL API.

Default: `0.0.0.0:9042` (`127.0.0.1:9042`)

#### --cql_proxy_webserver_port

Specifies the port for monitoring YCQL metrics.

Default: `12000`

---

### YEDIS options

The following options, or flags, support the use of the YEDIS API.

#### --redis_proxy_bind_address

Specifies the bind address for the YEDIS API.

Default: `0.0.0.0:6379`

#### --redis_proxy_webserver_port

Specifies the port for monitoring YEDIS metrics.

Default: `11000`

---

### Performance options

#### --enable_ondisk_compression

Enable Snappy compression at the the cluster level.

Default: `true`

#### --rocksdb_compact_flush_rate_limit_bytes_per_sec

Used to control rate of memstore flush and SSTable file compaction.

Default: `256MB`

#### --remote_bootstrap_rate_limit_bytes_per_sec

Rate control across all tablets being remote bootstrapped from or to this process.

Default: `256MB`

#### --yb_num_shards_per_tserver

The number of shards per YB-TServer per table when a user table is created.

Default: Server automatically picks a valid default internally, typically `8`.

---

### Write Ahead Log (WAL) options

#### --fs_wal_dirs

The directory where the `yb-tserver` will place its write-ahead logs. May be the same as one of the directories listed in `--fs_data_dirs`, but not a sub-directory of a data directory.

Default: Same as `--fs_data_dirs`

#### --durable_wal_write

If set to `false`, the writes to the Raft log are synced to disk every `interval_durable_wal_write_ms` milliseconds or every `bytes_durable_wal_write_mb` MB, whichever comes first. This default setting is recommended only for multi-AZ or multi-region deployments where the zones/regions are independent failure domains and there isn't a risk of correlated power loss. For single AZ deployments, this flag should be set to `true`.

Default: `false`

#### --interval_durable_wal_write_ms

When [`--durable_wal_write`](#durable-wal-write) is false, writes to the Raft log are synced to disk every `--interval_durable_wal_write_ms` or [`--bytes_durable_wal_write_mb`](#bytes-durable-wal-write-mb), whichever comes first.

Default: `1000`

#### --bytes_durable_wal_write_mb

When `--durable_wal_write` is `false`, writes to the Raft log are synced to disk every `--bytes_durable_wal_write_mb` or `--interval_durable_wal_write_ms`, whichever comes first.

Default: `1`

---

### Security options

For details on enabling client-server encryption, see [Client-server encryption](../../secure/tls-encryption/client-to-server).

#### --certs_dir

Directory that contains certificate authority, private key, and certificates for this server.

Default: `""` (Uses `<data drive>/yb-data/tserver/data/certs`.)

#### --allow_insecure_connections

Allow insecure connections. Set to `false` to prevent any process with unencrypted communication from joining a cluster. Note that this option requires the [`use_node_to_node_encryption`](#use-node-to-node-encryption) to be enabled and [`use_client_to_server_encryption`](#use-client-to-server-encryption) to be enabled.

Default: `true`

#### --certs_for_client_dir

The directory that contains certificate authority, private key, and certificates for this server that should be used for client-to-server communications.

Default: `""` (Use the same directory as for server-to-server communications.)

#### --dump_certificate_entries

Dump certificate entries.

Default: `false`

#### --use_client_to_server_encryption

Use client-to-server, or client-server, encryption with YCQL. 

Default: `false`

#### --use_node_to_node_encryption

Enable server-server, or node-to-node, encryption between YugabyteDB YB-Master and YB-TServer services in a cluster or universe. To work properly, all YB-Master services must also have their [`--use_node_to_node_encryption`](../yb-master/#use-node-to-node-encryption) setting enabled. When enabled, then [`--allow_insecure_connections`](#allow-insecure-connections) must be disabled.

Default: `false`

---

### Change data capture (CDC) options

To learn about CDC, see [Change data capture (CDC)](../../architecture/cdc-architecture).

#### --cdc_rpc_timeout_ms

Timeout used for CDC->`yb-tserver` asynchronous RPC calls.

Default: `30000`

#### --cdc_state_checkpoint_update_interval_ms

RAte at which CDC state's checkpoint is updated.

Default: `15000`

#### --cdc_ybclient_reactor_threads

The number of reactor threads to be used for processing `ybclient` requests for CDC.

Default: `50`

## Admin UI

The Admin UI for the YB-TServer is available at `http://localhost:9000`.

### Home

Home page of the YB-TServer (`yb-tserver`) that gives a high level overview of this specific instance.

![tserver-home](/images/admin/tserver-home.png)

### Dashboards

List of all dashboards to review the ongoing operations:

![tserver-dashboards](/images/admin/tserver-dashboards.png)

### Tablets

List of all tablets managed by this specific instance, sorted by the table name.

![tserver-tablets](/images/admin/tserver-tablets.png)

### Debug

List of all utilities available to debug the performance of this specific instance.

![tserver-debug](/images/admin/tserver-debug.png)

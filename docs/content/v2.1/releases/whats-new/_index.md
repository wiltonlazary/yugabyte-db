---
title: What's new in 2.1.8
headerTitle: What's new in 2.1.8
linkTitle: What's new in 2.1.8
description: Enhancements, changes, and resolved issues in the latest YugabyteDB release.
headcontent: Features, enhancements, and resolved issues in the latest release.
image: /images/section_icons/quick_start/install.png
section: RELEASES
block_indexing: true
menu:
  v2.1:
    identifier: whats-new
    weight: 2589 
---

**Released:** June 19, 2020 (2.1.8.2-b1).

**New to YugabyteDB?** Follow [Quick start](../../quick-start/) to get started and running in less than five minutes.

**Looking for earlier releases?** History of earlier releases is available in [Earlier releases](../earlier-releases/) section.  

## Downloads

### Binaries

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.1.8.2-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.1.8.2-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.1.8.2-b1
```

## YSQL

- Add support for [`SPLIT INTO` clause for `CREATE INDEX`](../../api/ysql/commands/ddl_create_index/#split-into) statement. [#3047](https://github.com/yugabyte/yugabyte-db/issues/3047)
- Fix aggregate functions pushdown for columns with default values added after table creation. [#4376](https://github.com/yugabyte/yugabyte-db/issues/4376)
- Resolve timeout frequently encountered whe batch-loading data in YSQL by using client-specified timeouts for RPC requests instead of hardcoded values. [#4045](https://github.com/yugabyte/yugabyte-db/issues/4045)
- Fix incorrect cross-component dependency in DocDB found in builds using `ninja`. [#4474](https://github.com/yugabyte/yugabyte-db/issues/4474)
- Fix operation buffering in stored procedures to handle transactions correctly. [#4268](https://github.com/yugabyte/yugabyte-db/issues/4268)
- [DocDB] Ensure that fast path (pushed down) single row writes honor higher priority transactions and get aborted or retired instead. [#4316](https://github.com/yugabyte/yugabyte-db/issues/4316)
- [DocDB] Split copy table operations into smaller chunks (using byte size instead of count) for CREATE DATABASE statement. [#3743](https://github.com/yugabyte/yugabyte-db/issues/3743)
- Correctly push down `IS NULL` condition to DocDB. [#4499](https://github.com/yugabyte/yugabyte-db/issues/4499)
- Avoid ASAN failures after a large data set is uploaded by rearranging files and test functionalities. [#4488](https://github.com/yugabyte/yugabyte-db/issues/4488)
- Fix memory leaks by using memory context to manage object alloc and free. [#4490](https://github.com/yugabyte/yugabyte-db/issues/4490)
- Fix multi-touch cache and improve caching logic for read and write operations. [#4379](https://github.com/yugabyte/yugabyte-db/issues/4379)
- Improve index cost estimates by considering index uniqueness, included columns (index scan vs. index only scan), scan direction, and partial indexes. Also, disable merge joins for unsupported cases. [#4494](https://github.com/yugabyte/yugabyte-db/issues/4494) and [#4496](https://github.com/yugabyte/yugabyte-db/issues/4496)
- Add support for deferrable foreign key constraints. [#3995](https://github.com/yugabyte/yugabyte-db/issues/3995)
- Prevent dropping primary key constraint. [#3163](https://github.com/yugabyte/yugabyte-db/issues/3163)
- Push down SELECT <aggregate>(<const>), for example, SELECT COUNT(1) , to DocDB. [#4276](https://github.com/yugabyte/yugabyte-db/issues/4276)
- Fix rare core dumps due to concurrency issues in metrics webserver during shutdown. [#4092](https://github.com/yugabyte/yugabyte-db/issues/4092)

## YCQL

- Rename `cqlsh` to `ycqlsh`. [#3935](https://github.com/yugabyte/yugabyte-db/issues/3935)
- Update Cassandra Java driver version to `3.8.0-yb-4` and adds support for [`guava`](https://github.com/google/guava) 26 or later. The latest release of the driver is available in the [Yugabyte `cassandra-java-driver` repository](https://github.com/yugabyte/cassandra-java-driver/releases). [#3897](https://github.com/yugabyte/yugabyte-db/issues/3897)
- YB-TServers should not crash when attempting to log in using YCQL authentication without a password. [#4459](https://github.com/yugabyte/yugabyte-db/issues/4459)
- Performance degradation in `CassandraSecondaryIndex` workload resolved. [#4401](https://github.com/yugabyte/yugabyte-db/issues/4401)
- Add [`yb-admin import_snapshot`](../../admin/yb-admin/#import-snapshot) support for renaming a few tables (not all), but the specified name is equal to the old table name: `yb-admin import_snapshot <meta-file> ks old_table_name`. [#4280](https://github.com/yugabyte/yugabyte-db/issues/4280)
- For DDL creation with Spring Data Cassandra, change the `Enum` value from `JSON` to `JSONB` to allow schema creation to succeed programmatically involving JSON column types and update the `cassandra-java-driver` to `3.8.0-yb-5`. [#4481](https://github.com/yugabyte/yugabyte-db/issues/4481)
- Use the same timestamp for current time to compute multiple runtimes in output of `<tserver-ip>:13000/rpcz`. [#4418](https://github.com/yugabyte/yugabyte-db/issues/4418)
- Correctly push down `= NULL` condition to DocDB. [#4499](https://github.com/yugabyte/yugabyte-db/issues/4499)
- Reduce YCQL unprepared statement execution time by up to 98% (example: reduced time to insert a 5 MB string from 18 seconds to 0.25 seconds). [#4397](https://github.com/yugabyte/yugabyte-db/issues/4397) and [#3586](https://github.com/yugabyte/yugabyte-db/issues/3586)
  - Special thanks to [@ouvai59](https://github.com/ouvai59) for your contribution!

## YEDIS

- For `yugabyted`, do not start `redis` server by default. Resolves port conflict during startup. [#4057](https://github.com/yugabyte/yugabyte-db/issues/4057)

## System improvements

- New `yb-admin` command [`get_load_balancer_state`](../../admin/yb-admin/#get-load-balancer-state) to get the cluster load balancer state. [#4509](https://github.com/yugabyte/yugabyte-db/issues/4509)
- Avoid creating intent iterator when no transactions are running. [#4500](https://github.com/yugabyte/yugabyte-db/issues/4500)
- Increase default memory limit for `yb-master` for running in low-memory setups (`<=4 GB`). [#3742](https://github.com/yugabyte/yugabyte-installation/pull/3742)
- Improve RocksDB checkpoint directory cleanup if a tserver crashes or is restarted while performing a snapshot operation. [#4352](https://github.com/yugabyte/yugabyte-db/issues/4352)
- [DocDB] Use bloom filters for range-partitioned tables. The first primary key column is added to the bloom filter. [#4437](https://github.com/yugabyte/yugabyte-installation/pull/4437)
- [DocDB] Fix snapshots bootstrap order bu altering the load for transaction-aware snapshots. [#4470](https://github.com/yugabyte/yugabyte-db/issues/4470)
- [DocDB] Add [`yb-admin master_leader_stepdown`](../../admin/yb-admin/#master-leader-stepdown) command. [#4135](https://github.com/yugabyte/yugabyte-db/issues/4135)
- [DocDB] Reduce impact on CPU and throughput during node failures. [#4042](https://github.com/yugabyte/yugabyte-db/issues/4042)
- [DocDB] Add `yb-ts-cli` commands, [`flush_all_tablets`](../../admin/yb-ts-cli/#flush-all-tablets) and [`flush_tablet <tablet_id>`](../../admin/yb-ts-cli/#flush-tablet), to flush tablets. When used with rolling restarts, less time is spent applying WAL records to rocksdb. [#2785](https://github.com/yugabyte/yugabyte-db/issues/2785)
  - Special thanks to [mirageyjd](https://github.com/mirageyjd) for your contribution.
- [DocDB] Fix deadlock during tablet splitting. [#4312](https://github.com/yugabyte/yugabyte-db/issues/4312)
- Introduced load balancing throttle on the total number of tablets being remote bootstrapped, across the cluster. [#4053](https://github.com/yugabyte/yugabyte-db/issues/4053) and [#4436](https://github.com/yugabyte/yugabyte-db/issues/4436)
- [DocDB] Remove applied intent doc hybrid time during compaction. [#4535](https://github.com/yugabyte/yugabyte-db/issues/4535)
- [DocDB] Fixed BoundedRocksDbIterator::SeekToLast works incorrectly for 2nd post-split tablet. [#4542](https://github.com/yugabyte/yugabyte-db/issues/4542)
- [DocDB] Abort snapshot if table was deleted. [#4610](https://github.com/yugabyte/yugabyte-db/issues/4610)
- [DocDB] Backfill index without waiting indefinitely for pending transactions. [#3471](https://github.com/yugabyte/yugabyte-db/issues/3471)
- [DocDB] Fix `yb-master` rerunning snapshot operations after upgrade. [#4816](https://github.com/yugabyte/yugabyte-db/issues/4816)
- [Colocation] During load balancing operations, load balance each colocated tablet once. This fix removes unnecessary load balancing for every user table sharing that table and the parent table.
- Fix YB-Master hangs due to transaction status resolution. [#4410](https://github.com/yugabyte/yugabyte-db/issues/4410)
- Redirect the master UI to the master leader UI without failing when one master is down. [#4442](https://github.com/yugabyte/yugabyte-db/issues/4442) and [#3869](https://github.com/yugabyte/yugabyte-db/issues/3869)
- Avoid race in `change_metadata_operation`. Use atomic<P*> to avoid race between
`Finish()` and `ToString` from updating or accessing request. [#3912](https://github.com/yugabyte/yugabyte-db/issues/3912)
- Refactor `RaftGroupMetadata` to avoid keeping unnecessary `TableInfo` objects in memory. [#4354](https://github.com/yugabyte/yugabyte-db/issues/4354)
- Fix missing rows in unidirectional replication and fix race conditions with CDC and TransactionManager. [#4257](https://github.com/yugabyte/yugabyte-installation/pull/4257)
- Change intent iterator creation logic. [#4543](https://github.com/yugabyte/yugabyte-installation/pull/4543)
- Upgrade all Python scripts used to build and package the code to Python 3. [#1442](https://github.com/yugabyte/yugabyte-db/issues/1442)

## Yugabyte Platform

- Fix failure when adding a node on a TLS-enabled universe. [#4482](https://github.com/yugabyte/yugabyte-db/issues/4482)
- Improve latency tracking by splitting overall operation metrics into individual rows for each API. [#3825](https://github.com/yugabyte/yugabyte-db/issues/3825)
  - YCQL and YEDIS metrics include `ops`, `avg latency`, and `P99 latency`.
  - YSQL metrics include only `ops` and `avg latency`.
- Add metrics for RPC queue sizes of services, including YB-Master, YB-TServer, YCQL, and YEDIS. [#4294](https://github.com/yugabyte/yugabyte-db/issues/4294)
- Add option to edit configuration flags without requiring server restart. [#4433](https://github.com/yugabyte/yugabyte-db/issues/4433)
- When configuration flags are deleted in the YugabyteDB Admin Console, remove the flags from `server.conf` file. [#4341](https://github.com/yugabyte/yugabyte-db/issues/4341)
- When creating GCP instances, only use host project when specifying network.
- When creating a cloud provider configuration, display provider-level (non-k8s) settings for SSH ports and enabling airgapped installations. [#3615](https://github.com/yugabyte/yugabyte-db/issues/3615), [#4243](https://github.com/yugabyte/yugabyte-db/issues/4243), and [#4240](https://github.com/yugabyte/yugabyte-db/issues/4240).
- After removing a node and then adding a node, check for certificate and key files and create the files if needed. [#4551](https://github.com/yugabyte/yugabyte-db/issues/4551)
- Update to support Helm 3 deployments. Note: Helm 2 is no longer supported. For migrating existing Helm 2 universes to Helm 3, see [Migrate from Helm 2 to Helm 3](../../manage/enterprise-edition/migrate-to-helm3/). [#4416]((https://github.com/yugabyte/yugabyte-db/issues/4416))
- Change `QLTableRow` representation. [#4427](https://github.com/yugabyte/yugabyte-db/issues/4427)
- Fix CDC-related race conditions using `CDCServiceTxnTest.TestGetChangesForPendingTransaction`. [#4544](https://github.com/yugabyte/yugabyte-db/issues/4544)
- Revert validation on alerting email field to allow comma-separated emails in the form. [#4639](https://github.com/yugabyte/yugabyte-db/issues/4639)
- Add **Custom SMTP Configuration** section to **Health & Alerting** tab on customer profile page. [#4443](https://github.com/yugabyte/yugabyte-db/issues/4443)
- Fix Kubernetes pod container metrics not displaying in **Metrics** panel. [#4652](https://github.com/yugabyte/yugabyte-db/issues/4652)
- Fix **Backups** tab not rendering when there are no backups. [#4661](https://github.com/yugabyte/yugabyte-db/issues/4661)

{{< note title="Note" >}}

Prior to version 2.0, YSQL was still in beta. As a result, the 2.0 release included a backward-incompatible file format change for YSQL. If you have an existing cluster running releases earlier than 2.0 with YSQL enabled, then you will not be able to upgrade to version 2.0+. Export from your existing cluster and then import into a new cluster (v2.0 or later) to use existing data.

{{< /note >}}

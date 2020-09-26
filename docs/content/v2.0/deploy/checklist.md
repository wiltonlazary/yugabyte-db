---
title: Deploy checklist
linkTitle: Deployment checklist
description: Deployment checklist
block_indexing: true
menu:
  v2.0:
    identifier: checklist
    parent: deploy
    weight: 605
isTocNested: true
showAsideToc: true
---

## Overview

A YugabyteDB cluster consists of two distributed services - the YB-TServer service and the YB-Master service. The YB-Master service should be brought up first followed by the YB-TServer service. In order to bring up these distributed services, the respective servers (YB-Master or YB-TServer) need to be started across different nodes. Below are some considerations and recommendations on starting these services. The *deployment configurations* section below has detailed steps on how to setup YugabyteDB clusters.

## Basics

- YugabyteDB works on a variety of operating systems. For production workloads, the recommended operating systems are CentOS 7.x and RHEL 7.x.
- Set the appropriate [system limits using `ulimit`](../manual-deployment/system-config/#setting-ulimits) on each node running a YugabyteDB server.
- Use [ntp](../manual-deployment/system-config/#ntp) to synchronize time among the machines.

## Replication

YugabyteDB internally replicates data in a strongly consistent manner using Raft consensus protocol in order to survive node failure without compromising data correctness. The number of copies of the data represents the replication factor.

You would first need to choose a replication factor. You would need at least as many machines as the replication factor. YugabyteDB works with both hostnames or IP addresses. IP addresses are preferred at this point, they are more extensively tested. Below are some recommendations relating to the replication factor.

- The replication factor should be an odd number.
- The default replication factor is `3`.
  - A replication factor of `3` allows tolerating one machine failure.
  - A replication factor of `5` allows tolerating two machine failures.
  - More generally, if the replication factor is `n`, YugabyteDB can survive `(n - 1) / 2` failures without compromising correctness or availability of data.
- Number of YB-Master servers running in a cluster should match replication factor. Run each server on a separate machine to prevent losing data on failures.
- Number of YB-TServer servers running in the cluster should not be less than the replication factor. Run each server on a separate machine to prevent losing data on failures.
- Specify the replication factor using the `--replication_factor` when bringing up the YB-Master servers.

See the [yb-master command reference](../manual-deployment/start-masters) for more information.

## Hardware requirements

YugabyteDB is designed to run well on bare-metal machines, virtual machines (VMs), or containers.

### CPU and RAM

Allocate adequate CPU and RAM. YugabyteDB has good defaults for running on a wide range of machines, and has been tested from 2 core to 64 core machines, and up to 200GB RAM.

- Minimum configuration: **2 cores** and **2GB RAM**
- For higher performance:
  - **8 cores** or more
  - Add more CPU (compared to adding more RAM) to improve performance.

For typical OLTP workloads YugabyteDB, performance  improves with more aggregate CPU in the cluster. 
You can achieve this by using larger nodes or adding more nodes to a cluster. 
For high performance use cases, Yugabyte recommend nodes with at least 8 CPUs (preferably 16).
If you do not have enough CPUs, this will show up as higher latencies and eventually dropped requests.

Memory depends on your application query pattern. 
Writes require memory but only up to a certain point (4GB, but if you have a write-heavy workload you may need a little more). 
Beyond that, more memory generally helps improve the read throughput and latencies by caching data in the internal cache. 
If you do not have enough memory to fit the read working set, then you will typically 
experience higher read latencies because data has to be read from disk. 
Having a faster disk could help in some of these cases.

YugabyteDB explicitly manages a block cache, and doesn't need the entire data set to fit in memory. 
We do not rely on OS to keep data in its buffers. 
If you give YugabyteDB sufficient memory data accessed and present in block cache will stay in memory.

### Disks

- Use SSDs (solid state disks) for good performance.
- Both local or remote attached storage work with YugabyteDB. Since YugabyteDB internally replicates data for fault tolerance, remote attached storage which does its own additional replication is not a requirement. Local disks often offer better performance at a lower cost.
- Multi-disk nodes
      - Do not use RAID across multiple disks. YugabyteDB can natively handle multi-disk nodes (JBOD).
      - Create a data directory on each of the data disks and specify a comma separated list of those directories to the yb-master and yb-tserver servers via the `--fs_data_dirs` flag.
- Mount settings
      - XFS is the recommended filesystem.
      - Use the `noatime` setting when mounting the data drives.

YugabyteDB does not require any form of RAID, but runs optimally on a JBOD (just a bunch of disks) setup. 
It can also leverage multiple disks per node and has been tested beyond 10 TB of storage per node.

Write-heavy applications usually require more disk IOPS (especially if the size of each record is larger), 
therefore in this case the total IOPS that a disk can support matters. 
On the read side, if the data does not fit into the cache and data needs to be read 
from the disk in order to satisfy queries, the disk performance (latency and IOPS) will start to matter.

YugabyteDB uses per-tablet [size tiered compaction](../../architecture/concepts/yb-tserver/). 
Therefore the typical space amplification in YugabyteDB tends to be in the 10-20% range.

### Network

Below is a minimal list of default ports (along with the network access required) in order to use YugabyteDB.

- Each of the nodes in the YugabyteDB cluster must be able to communicate with each other using TCP/IP on the following ports:
      - 7100 (YB-Master RPC communication port)
      - 9100 (YB-TServer RPC communication port)
- In order to view the cluster dashboard, you need to be able to navigate to the following ports on the nodes
      - 7000 (Cluster dashboard viewable from any of the YB-Master servers)
- To use the database from the app, the following ports need to be accessible from the app (or commandline interface)
      - 9042 (which supports YCQL, YugabyteDB's Cassandra-compatible API)
      - 6379 (which supports YEDIS, YugabyteDB's Redis-compatible API)

### Default ports reference

The above deployment uses the various default ports listed below.

Service | Type | Port
--------|------| -------
`yb-master` | rpc | 7100
`yb-master` | admin web server | 7000
`yb-tserver` | rpc | 9100
`yb-tserver` | admin web server | 9000
`ysql` | rpc | 5433
`ysql` | admin web server | 13000
`ycql` | rpc | 9042
`ycql` | admin web server | 12000
`yedis` | rpc | 6379
`yedis` | admin web server | 11000
`node_exporter` | Prometheus node exporter | 9300
`ssh` | SSH access | 22

{{< note title="Note" >}}

In our Enterprise installs, we change the SSH port for added security.

{{< /note >}}

## Clock synchronization

For YugabyteDB to preserve data consistency, the clock drift and clock skew across different nodes must be bounded. This can be achieved by running clock synchronization software, such as [NTP](http://www.ntp.org/). Below are some recommendations on how to configure clock synchronization.

### Clock skew

Set a safe value for the maximum clock skew parameter (`--max_clock_skew_usec`) when starting the YugabyteDB servers. We recommend setting this parameter to twice the expected maximum clock skew between any two nodes in your deployment.

For example, if the maximum clock skew across nodes is expected to be no more than 250ms, then set the parameter to 500ms (`--max_clock_skew_usec=500000`).

### Clock drift

The maximum clock drift on any node should be bounded to no more than 500 PPM (or *parts per million*). This means that the clock on any node should drift by no more than 0.5ms per second. Note that 0.5ms per second is the standard assumption of clock drift in Linux.

{{< note title="Note" >}}

In practice, the clock drift would have to be orders of magnitude higher in order to cause correctness issues.

{{< /note >}}

## Running on public clouds

### Amazon Web Services (AWS)

- Use the `c5` or `i3` instance families.
- Recommended types are `i3.2xlarge`, `i3.4xlarge`, `c5.2xlarge`, `c5.4xlarge`
- For the `c5` instance family, use `gp2` EBS (SSD) disks that are **at least 250GB** in size, larger if more IOPS are needed.
      - The number of IOPS are proportional to the size of the disk.
      - In our testing, `gp2` EBS SSDs provide the best performance for a given cost among the various EBS disk options.
- Avoid running on [`t2` instance types](https://aws.amazon.com/ec2/instance-types/t2/). The `t2` instance types are burstable instance types. Their baseline performance and ability to burst are governed by CPU Credits, and makes it hard to get steady performance.

### Google Cloud

- Use the `n1-highcpu` instance family. As a second choice, `n1-standard` instance family works too.
- Recommended instance types are `n1-highcpu-8` and `n1-highcpu-16`.
- [Local SSDs](https://cloud.google.com/compute/docs/disks/#localssds) are the preferred storage option.
      - Each local SSD is 375 GB in size, but you can attach up to eight local SSD devices for 3 TB of total local SSD storage space per instance.
- As a second choice, [remote persistent SSDs](https://cloud.google.com/compute/docs/disks/#pdspecs) work well. Make sure the size of these SSDs are **at least 250GB** in size, larger if more IOPS are needed.
      - The number of IOPS are proportional to the size of the disk.
- Avoid running on `f1` or `g1` machine families. These are [burstable, shared core machines](https://cloud.google.com/compute/docs/machine-types#sharedcore) that may not deliver steady performance.

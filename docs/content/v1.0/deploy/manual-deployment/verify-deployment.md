---
title: Verify Deployment
linkTitle: 5. Verify Deployment
description: Verify Deployment
menu:
  v1.0:
    identifier: deploy-manual-deployment-verify-deployment
    parent: deploy-manual-deployment
    weight: 615
---

As before, we shall assume that we brought up a universe on four nodes with replication factor `3`. Let us assume their IP addresses are `172.151.17.130`, `172.151.17.220`, `172.151.17.140` and `172.151.17.150`


## Setup Redis-compatible YEDIS service

{{< note title="Note" >}}
If you want this cluster to be able to support Redis clients, you **must** perform this step.
{{< /note >}}

While the YCQL and PostgreSQL (Beta) services are turned on by default after all the yb-tservers start, the Redis-compatible YEDIS service is off by default. If you want this cluster to be able to support Redis clients, run the following command from any of the 4 instances. The command below will add the special Redis table into the DB and also start the YEDIS server on port 6379 on all instances.

```sh
$ ./bin/yb-admin --master_addresses 172.151.17.v1.0:7100,172.151.17.220:7100,172.151.17.v1.0:7100 setup_redis_table
```

## View the master UI dashboard

You should now be able to view the master dashboard on the ip address of any master. In our example, this is one of the following urls:

- http://172.151.17.v1.0:7000
- http://172.151.17.220:7000
- http://172.151.17.v1.0:7000

{{< tip title="Tip" >}}If this is a public cloud deployment, remember to use the public ip for the nodes, or a http proxy to view these pages.{{< /tip >}}<br>

## Connect clients

- Clients can connect to YugabyteDB's YCQL API at

```sh
172.151.17.v1.0:9042,172.151.17.220:9042,172.151.17.v1.0:9042,172.151.17.v1.0:9042
```

- Clients can connect to YugabyteDB's YEDIS API at

```sh
172.151.17.v1.0:6379,172.151.17.220:6379,172.151.17.v1.0:6379,172.151.17.v1.0:6379
```

- Clients can connect to YugabyteDB's PostgreSQL (Beta) API at

```sh
172.151.17.v1.0:5433,172.151.17.220:5433,172.151.17.v1.0:5433,172.151.17.v1.0:5433
```


## Default ports reference

The above deployment uses the various default ports listed below. 

Service | Type | Port 
--------|------| -------
`yb-master` | rpc | 7100
`yb-master` | admin web server | 7000
`yb-tserver` | rpc | 9100
`yb-tserver` | admin web server | 9000
`ycql` | rpc | 9042
`ycql` | admin web server | 12000
`yedis` | rpc | 6379
`yedis` | admin web server | 11000
`pgsql` | rpc | 5433
`pgsql` | admin web server | 13000

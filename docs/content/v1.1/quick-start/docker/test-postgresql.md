## 1. Create a new cluster
- Destroy any existing cluster.

```sh
$ ./yb-docker-ctl destroy
```


- Create a new cluster with YSQL API enabled. Note the additional option `enable_postgres` passed to the create cluster command. Also note that this requires at least version `1.1.2.0-b10` of YugabyteDB.

```sh
$ ./yb-docker-ctl create --rf 3 --enable_postgres
```

- Check status of the cluster

```sh
$ ./yb-docker-ctl status
```

```
ID             PID        Type       Node                 URL                       Status          Started At
ca16705b20bd   5861       tserver    yb-tserver-n3        http://192.168.64.7:9000  Running         2018-10-18T22:02:52.12697026Z
0a7deab4e4db   5681       tserver    yb-tserver-n2        http://192.168.64.6:9000  Running         2018-10-18T22:02:51.181289786Z
921494a8058d   5547       tserver    yb-tserver-n1        http://192.168.64.5:9000  Running         2018-10-18T22:02:50.187976253Z
0d7dc9436033   5345       master     yb-master-n3         http://192.168.64.4:7000  Running         2018-10-18T22:02:49.105792573Z
0b25dd24aea3   5191       master     yb-master-n2         http://192.168.64.3:7000  Running         2018-10-18T22:02:48.162506832Z
feea0823209a   5039       master     yb-master-n1         http://192.168.64.2:7000  Running         2018-10-18T22:02:47.163244578Z
```

- Run psql to connect to the service.

You can do this as shown below.

```sh
$ docker exec -it yb-tserver-n1 /home/yugabyte/postgres/bin/psql -h yb-tserver-n1 -p 5433 -U postgres
```

```
psql (10.4)
Type "help" for help.

postgres=#
```

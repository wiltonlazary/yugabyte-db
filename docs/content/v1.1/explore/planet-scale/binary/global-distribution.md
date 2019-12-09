## 1. Create a multi-zone universe in US West

If you have a previously running local universe, destroy it using the following.

```sh
$ ./bin/yb-ctl destroy
```

Start a new local universe with replication factor 3, and each replica placed in different zones (`us-west-2a`, `us-west-2b`, `us-west-2c`) in the `us-west-2` (Oregon) region of AWS. This can be done by running the following: 

```sh
$ ./bin/yb-ctl create --placement_info "aws.us-west-2.us-west-2a,aws.us-west-2.us-west-2b,aws.us-west-2.us-west-2c"
```

In this deployment, the YB Masters are each placed in a separate zone to allow them to survive the loss of a zone. You can view the masters on the [dashboard](http://localhost:7000/).

![Multi-zone universe masters](/images/ce/online-reconfig-multi-zone-masters.png)

You can view the tablet servers on the [tablet servers page](http://localhost:7000/tablet-servers).

![Multi-zone universe tservers](/images/ce/online-reconfig-multi-zone-tservers.png)

## 2. Start a workload

Run a simple key-value workload in a separate shell.

```sh
$ java -jar java/yb-sample-apps.jar \
    --workload CassandraKeyValue \
    --nodes 127.0.0.1:9042 \
    --num_threads_read 1 \
    --num_threads_write 1
```

You should now see some read and write load on the [tablet servers page](http://localhost:7000/tablet-servers).

![Multi-zone universe load](/images/ce/online-reconfig-multi-zone-load.png)

## 3. Add nodes in US East and Tokyo regions

### Add new nodes
Add a node in the zone `us-east-1a` of region `us-east-1`. 

```sh
$ ./bin/yb-ctl add_node --placement_info "aws.us-east-1.us-east-1a"
```

Add another node in the zone `ap-northeast-1a` of region `ap-northeast-1`.

```sh
$ ./bin/yb-ctl add_node --placement_info "aws.ap-northeast-1.ap-northeast-1a"
```

At this point, these 2 new nodes are added into the cluster but are not taking any read or write IO. This is because  YB Master's initial placement policy of storing data across the zones in `us-west-2` region still applies.

![Add node in a new region](/images/ce/online-reconfig-add-regions-no-load.png)

### Update placement policy

Let us now update the placement policy, instructing the YB-Master to place data in the new regions.

```sh
$ ./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    modify_placement_info aws.us-west-2.us-west-2a,aws.us-east-1.us-east-1a,aws.ap-northeast-1.ap-northeast-1a 3
```

You should see that the data as well as the IO gradually moves from the nodes in `us-west-2b` and `us-west-2c` to the newly added nodes. The [tablet servers page](http://localhost:7000/tablet-servers) should soon look something like the screenshot below.

![Multi region workload](/images/ce/online-reconfig-multi-region-load.png)

## 4. Retire old nodes

### Start new masters
Next we need to move the YB-Master from the old nodes to the new nodes. In order to do so, first start a new masters on the new nodes.

```sh
$ ./bin/yb-ctl add_node --master --placement_info "aws.us-east-1.us-east-1a"
```

```sh
$ ./bin/yb-ctl add_node --master --placement_info "aws.ap-northeast-1.ap-northeast-1a"
```


![Add master](/images/ce/online-reconfig-add-masters.png)

### Remove old masters
Remove the old masters from the masters Raft group. Assuming nodes with IPs 127.0.0.2 and 127.0.0.3 were the two old nodes, run the following commands.

```sh
$ ./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100,127.0.0.4:7100,127.0.0.5:7100 change_master_config REMOVE_SERVER 127.0.0.2 7100
```

```sh
$ ./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.3:7100,127.0.0.4:7100,127.0.0.5:7100 change_master_config REMOVE_SERVER 127.0.0.3 7100
```


![Add master](/images/ce/online-reconfig-remove-masters.png)

### Remove old nodes

Now it's safe to remove the old nodes.

```sh
$ ./bin/yb-ctl remove_node 2
```

```sh
$ ./bin/yb-ctl remove_node 3
```

![Add master](/images/ce/online-reconfig-remove-nodes.png)

## 5. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./bin/yb-ctl destroy
```

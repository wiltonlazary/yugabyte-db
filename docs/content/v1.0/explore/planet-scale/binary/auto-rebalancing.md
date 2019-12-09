## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following:

```sh
$ ./bin/yb-ctl destroy
```

Start a new local cluster - by default, this will create a 3 node universe with a replication factor of 3.
We set the number of [shards](../../architecture/concepts/sharding/) per tserver to 8 so we can better observe the load balancing during scaling.
Considering there are 3 tservers and replication factor 3, there will be 72 total shards per table.

```sh
$ ./bin/yb-ctl --num_shards_per_tserver 8 create
```


## 2. Run sample key-value app

Run the Cassandra sample key-value app against the local universe by typing the following command.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload CassandraKeyValue \
                                    --nodes 127.0.0.1:9042 \
                                    --num_threads_write 1 \
                                    --num_threads_read 4 \
                                    --value_size 4096
```

## 3. Observe data sizes per node

You can check a lot of the per-node stats by browsing to the <a href='http://127.0.0.1:7000/tablet-servers' target="_blank">tablet-servers</a> page. It should look like this. The total data size per node as well as the total memory used per node are highlighted in the screenshot below. Note that both of those metrics are roughly the same across all the nodes indicating uniform usage across the nodes.

![Data and memory sizes with 3 nodes](/images/ce/auto-rebalancing-3-nodes.png)


## 4. Add a node and observe data rebalancing

Add a node to the universe.

```sh
$ ./bin/yb-ctl add_node
```

Now we should have 4 nodes. Refresh the <a href='http://127.0.0.1:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update. As you refresh, you should see the new node getting more and more tablets, which would cause it to get more data as well as increase its memory footprint. Finally, all the 4 nodes should end up with a similar data distribution and memory usage.

![Data and memory sizes with 4 nodes](/images/ce/auto-rebalancing-4-nodes.png)


## 5. Add another node and observe linear scale out

Add yet another node to the universe.

```sh
$ ./bin/yb-ctl add_node
```

Now we should have 5 nodes. Refresh the <a href='http://127.0.0.1:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update, and as before you should see all the nodes end up with similar data sizes and memory footprints.

![Data and memory sizes with 5 nodes](/images/ce/auto-rebalancing-5-nodes.png)

YugabyteDB automatically balances the tablet leaders and followers of a universe by moving them in a rate-limited manner into the newly added nodes. This automatic balancing of the data is completely transparent to the application logic.

## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./bin/yb-ctl destroy
```

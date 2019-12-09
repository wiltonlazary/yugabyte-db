## 1. Create universe

If you have a previously running local universe, destroy it using the following.

```sh
$ kubectl delete -f yugabyte-statefulset.yaml
```

Start a new local cluster — by default, this will create a 3-node cluster with a replication factor of 3.

```sh
$ kubectl apply -f yugabyte-statefulset.yaml
```

Check the Kubernetes dashboard to see the 3 YB-Master and 3 YB-TServer pods representing the 3 nodes of the cluster.

```sh
$ minikube dashboard
```

![Kubernetes Dashboard](/images/ce/kubernetes-dashboard.png)

## 2. Check cluster status with Admin UI

In order to do this, we would need to access the UI on port 7000 exposed by any of the pods in the YB-Master (`yb-master`) service (one of `yb-master-0`, `yb-master-1` or `yb-master-2`). In order to do so, we find the URL for the yb-master-ui LoadBalancer service.

```sh
$ minikube service  yb-master-ui --url
```

```
http://192.168.99.100:31283
```

Now, you can view the [yb-master-0 Admin UI](../../reference/configuration/yb-master/#admin-ui) is available at the above URL.

## 3. Add node and observe linear scale-out

Add a node to the universe.

```sh
$ kubectl scale statefulset yb-tserver --replicas=4
```

Now we should have 4 nodes. Refresh the <a href='http://localhost:7000/tablet-servers' target="_blank">tablet-servers</a> page to see the stats update. YugabyteDB automatically updates application clients to use the newly added node for serving queries. This scaling out of client queries is completely transparent to the application logic, allowing the application to scale linearly for both reads and writes. 

You can also observe the newly added node using the following command.

```sh
$ kubectl get pods
```

```
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          5m
yb-master-1    1/1       Running   0          5m
yb-master-2    1/1       Running   0          5m
yb-tserver-0   1/1       Running   1          5m
yb-tserver-1   1/1       Running   1          5m
yb-tserver-2   1/1       Running   0          5m
yb-tserver-3   1/1       Running   0          4m
```

## 4. Scale back down to 3 nodes

The cluster can now be scaled back to only 3 nodes.

```sh
$ kubectl scale statefulset yb-tserver --replicas=3
```

```sh
$ kubectl get pods
```

```
NAME           READY     STATUS        RESTARTS   AGE
yb-master-0    1/1       Running       0          6m
yb-master-1    1/1       Running       0          6m
yb-master-2    1/1       Running       0          6m
yb-tserver-0   1/1       Running       1          6m
yb-tserver-1   1/1       Running       1          6m
yb-tserver-2   1/1       Running       0          6m
yb-tserver-3   1/1       Terminating   0          5m
```

## 5. Clean up (optional)

Optionally, you can shut down the local cluster created in Step 1.

```sh
$ kubectl delete -f yugabyte-statefulset.yaml
```

Further, to destroy the persistent volume claims (**you will lose all the data if you do this**), run:

```sh
kubectl delete pvc -l app=yb-master
kubectl delete pvc -l app=yb-tserver
```

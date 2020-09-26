---
title: Google Kubernetes Engine (GKE)
linkTitle: Google Kubernetes Engine (GKE)
description: Google Kubernetes Engine (GKE)
block_indexing: true
menu:
  v2.0:
    parent: deploy-kubernetes
    name: Google Kubernetes Engine
    identifier: k8s-gke-1
    weight: 623
type: page
isTocNested: true
showAsideToc: true
---


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/deploy/kubernetes/gke/helm-chart" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Helm chart
    </a>
  </li>
  <li >
    <a href="/latest/deploy/kubernetes/gke/statefulset-yaml" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      StatefulSet YAML
    </a>
  </li>
   <li >
    <a href="/latest/deploy/kubernetes/gke/statefulset-yaml-local-ssd" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      StatefulSet YAML with Local SSD
    </a>
  </li>
</ul>

## Prerequisites

- Download and install the [Google Cloud SDK](https://cloud.google.com/sdk/downloads/).

**NOTE:** If you install gcloud using a package manager (as opposed to downloading and installing it manually), it does not support some of the commands below.

- Configure defaults for gcloud

Set the project ID as `yugabyte`. You can change this as per your need.

```sh
$ gcloud config set project yugabyte
```

Set the default compute zone as `us-west1-b`. You can change this as per your need.

```sh
$ gcloud config set compute/zone us-west1-b
```

- Install `kubectl`

After installing Cloud SDK, install the `kubectl` command line tool by running the following command. 

```sh
$ gcloud components install kubectl
```

Note that GKE is usually 2 or 3 major releases behind the upstream/OSS Kubernetes release. This means you have to make sure that you have the latest kubectl version that is compatible across different Kubernetes distributions if that's what you intend to.

- Ensure `helm` is installed

First, check to see if Helm is installed by using the Helm version command.

```sh
$ helm version
```

For Helm 2, you should see something similar to the following output.
```
Client: &version.Version{SemVer:"v2.14.1", GitCommit:"5270352a09c7e8b6e8c9593002a73535276507c0", GitTreeState:"clean"}
Error: could not find tiller
```

If you run into issues associated with `tiller` with Helm 2, you can initialize helm with the upgrade option.
```sh
$ helm init --upgrade --wait
```

Tiller, the server-side component for Helm 2, will then be installed into your Kubernetes cluster. By default, Tiller is deployed with an insecure 'allow unauthenticated users' policy. To prevent this, run `helm init` with the `--tiller-tls-verify` flag.For more information on securing your installation see [here](https://docs.helm.sh/using_helm/#securing-your-helm-installation).

For Helm 3, you should see something similar to the following output. Note that the `tiller` server side component has been removed in Helm 3.
```
version.BuildInfo{Version:"v3.0.3", GitCommit:"ac925eb7279f4a6955df663a0128044a8a6b7593", GitTreeState:"clean", GoVersion:"go1.13.6"}
```

## 1. Create a GKE cluster

Create a Kubernetes cluster, if you have not already done so, by running the following command.

```sh
$ gcloud container clusters create yugabyte
```

## 2. Create a YugabyteDB cluster

### Create service account (Helm 2 only)

Before you can create the cluster, you need to have a service account that has been granted the `cluster-admin` role. Use the following command to create a `yugabyte-helm` service account granted with the ClusterRole of `cluster-admin`.

```sh
$ kubectl create -f https://raw.githubusercontent.com/yugabyte/charts/master/stable/yugabyte/yugabyte-rbac.yaml
```

```sh
serviceaccount/yugabyte-helm created
clusterrolebinding.rbac.authorization.k8s.io/yugabyte-helm created
```

### Initialize Helm (Helm 2 only)

Initialize `helm` with the service account, but use the `--upgrade` option to ensure that you can upgrade any previous initializations you may have made.

```sh
$ helm init --service-account yugabyte-helm --upgrade --wait
```
```
$HELM_HOME has been configured at `/Users/<user>/.helm`.

Tiller (the Helm server-side component) has been upgraded to the current version.
Happy Helming!
```

### Add charts repository

To add the YugabyteDB charts repository, run the following command.

```sh
$ helm repo add yugabytedb https://charts.yugabyte.com
```

### Fetch updates from the repository

Make sure that you have the latest updates to the repository by running the following command.

```sh
$ helm repo update
```

### Validate the chart version

**For Helm 2:**

```sh
$ helm search yugabytedb/yugabyte
```

**For Helm 3:**

```sh
$ helm search repo yugabytedb/yugabyte
```

**Output:**

```sh
NAME                CHART VERSION APP VERSION   DESCRIPTION                                       
yugabytedb/yugabyte 2.0.12        2.0.12.0-b10  YugabyteDB is the high-performance distr...
```

### Install YugabyteDB

Install YugabyteDB in the Kubernetes cluster using the commands below. 

**For Helm 2:**

```sh
$ helm install yugabytedb/yugabyte --namespace yb-demo --name yb-demo --wait
```

**For Helm 3:**

For Helm 3, you have to first create a namespace.

```sh
$ kubectl create namespace yb-demo
$ helm install yb-demo yugabytedb/yugabyte --namespace yb-demo --wait
```

## Check the cluster status

You can check the status of the cluster using various commands noted below.

**For Helm 2:**

```sh
$ helm status yb-demo
```

**For Helm 3:**

```sh
$ helm status yb-demo -n yb-demo
```

**Output**:

```sh
NAME: yb-demo
LAST DEPLOYED: Thu Feb 13 13:29:13 2020
NAMESPACE: yb-demo
STATUS: deployed
REVISION: 1
TEST SUITE: None
NOTES:
1. Get YugabyteDB Pods by running this command:
  kubectl --namespace yb-demo get pods

2. Get list of YugabyteDB services that are running:
  kubectl --namespace yb-demo get services

3. Get information about the load balancer services:
  kubectl get svc --namespace yb-demo

4. Connect to one of the tablet server:
  kubectl exec --namespace yb-demo -it yb-tserver-0 bash

5. Run YSQL shell from inside of a tablet server:
  kubectl exec --namespace yb-demo -it yb-tserver-0 -- /home/yugabyte/bin/ysqlsh -h yb-tserver-0.yb-tservers.yb-demo

6. Cleanup YugabyteDB Pods
  helm delete yb-demo --purge
  NOTE: You need to manually delete the persistent volume
  kubectl delete pvc --namespace yb-demo -l app=yb-master
  kubectl delete pvc --namespace yb-demo -l app=yb-tserver
```

Check the pods.

```sh
$ kubectl get pods --namespace yb-demo
```

```
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          4m
yb-master-1    1/1       Running   0          4m
yb-master-2    1/1       Running   0          4m
yb-tserver-0   1/1       Running   0          4m
yb-tserver-1   1/1       Running   0          4m
yb-tserver-2   1/1       Running   0          4m
```

Check the services.

```sh
$ kubectl get services --namespace yb-demo
```

```
NAME                 TYPE           CLUSTER-IP      EXTERNAL-IP    PORT(S)                                        AGE
yb-master-ui         LoadBalancer   10.109.39.242   35.225.153.213 7000:31920/TCP                                 10s
yb-masters           ClusterIP      None            <none>         7100/TCP,7000/TCP                              10s
yb-tserver-service   LoadBalancer   10.98.36.163    35.225.153.214 6379:30929/TCP,9042:30975/TCP,5433:30048/TCP   10s
yb-tservers          ClusterIP      None            <none>         7100/TCP,9000/TCP,6379/TCP,9042/TCP,5433/TCP   10s
```

You can even check the history of the `yb-demo` deployment.

**For Helm 2:**

```sh
$ helm history yb-demo
```

**For Helm 3**:

```sh
$ helm history yb-demo -n yb-demo
```

**Output:**

```sh
REVISION  UPDATED                   STATUS    CHART           APP VERSION   DESCRIPTION     
1         Thu Feb 13 13:29:13 2020  deployed  yugabyte-2.0.12 2.0.12.0-b10  Install complete
```

## Connect using YugabyteDB Shells

To connect and use the YSQL Shell `ysqlsh`, run the following command.

```sh
$ kubectl exec -n yb-demo -it yb-tserver-0 -- /home/yugabyte/bin/ysqlsh -h yb-tserver-0.yb-tservers.yb-demo
```

To connect and use the YCQL Shell `cqlsh`, run the following command.

```sh
$ kubectl exec -n yb-demo -it yb-tserver-0 /home/yugabyte/bin/cqlsh yb-tserver-0.yb-tservers.yb-demo
```

## Connect using external clients

To connect an external program, get the load balancer `EXTERNAL-IP` IP address of the `yb-tserver-service` service and connect to the 5433 / 9042 ports for YSQL / YCQL services respectively.

```sh
$ kubectl get services --namespace yb-demo
```
```
NAME                 TYPE           CLUSTER-IP      EXTERNAL-IP   PORT(S)                                        AGE
...
yb-tserver-service   LoadBalancer   10.98.36.163    35.225.153.214     6379:30929/TCP,9042:30975/TCP,5433:30048/TCP   10s
...
```

## Configure cluster

You can configure the cluster using the same commands/options as [Open Source Kubernetes](../../oss/helm-chart/#configure-cluster)

### Independent LoadBalancers

By default, the YugabyteDB Helm chart will expose the client API endpoints as well as master UI endpoint using 2 LoadBalancers. If you want to expose the client APIs using independent LoadBalancers, you can do the following.

**For Helm 2**:

```sh
helm install yugabytedb/yugabyte -f https://raw.githubusercontent.com/yugabyte/charts/master/stable/yugabyte/expose-all.yaml --namespace yb-demo --name yb-demo --wait
```

**For Helm 3:**

```sh
helm install yb-demo yugabytedb/yugabyte -f https://raw.githubusercontent.com/yugabyte/charts/master/stable/yugabyte/expose-all.yaml --namespace yb-demo --wait
```

You can also bring up an internal LoadBalancer (for either YB-Master or YB-TServer services), if required. Just specify the [annotation](https://kubernetes.io/docs/concepts/services-networking/service/#internal-load-balancer) required for your cloud provider. The following command brings up an internal LoadBalancer for the YB-TServer service in Google Cloud Platform.

```sh
$ helm install yugabyte -f https://raw.githubusercontent.com/yugabyte/charts/master/stable/yugabyte/expose-all.yaml --namespace yb-demo --name yb-demo \
  --set annotations.tserver.loadbalancer."cloud\.google\.com/load-balancer-type"=Internal --wait
```

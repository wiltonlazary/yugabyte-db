---
title: Install Yugabyte Platform on Kubernetes
headerTitle: Install Yugabyte Platform
linkTitle: 2. Install Yugabyte Platform
description: Install Yugabyte Platform (aka YugaWare) on Kubernetes
aliases:
  - /latest/deploy/enterprise-edition/install-admin-console/kubernetes
menu:
  latest:
    identifier: install-yp-3-kubernetes
    parent: deploy-yugabyte-platform
    weight: 670
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/yugabyte-platform/deploy/install-admin-console/default" class="nav-link">
      <i class="fas fa-cloud"></i>
      Default
    </a>
  </li>
  <li >
    <a href="/latest/yugabyte-platform/deploy/install-admin-console/airgapped" class="nav-link">
      <i class="fas fa-unlink"></i>
      Airgapped
    </a>
  </li>
  <li>
    <a href="/latest/yugabyte-platform/deploy/install-admin-console/kubernetes" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

## Prerequisites

The Yugabyte Platform (YugaWare) Helm chart was tested using the following software versions:

- Kubernetes 1.10 or later.
- Helm 3.0 or later.
- Yugabyte Platform (`yugaware`) Docker image 1.1.0 or later.

Before installing the YugbyteDB Admin Console, verify you have the following:

- A Kubernetes cluster configured with [Helm](https://helm.sh/).
- A Kubernetes node with minimum 4 CPU core and 15 GB RAM can be allocated to Yugabyte Platform.
- A Kubernetes secret obtained from Yugabyte Support.

To confirm that `helm` is configured correctly, run the following command:

```sh
$ helm version
```

The output should be similar to the following:

```
version.BuildInfo{Version:"v3.2.1", GitCommit:"fe51cd1e31e6a202cba7dead9552a6d418ded79a", GitTreeState:"clean", GoVersion:"go1.13.10"}
```

## Install Yugabyte Platform

1. [Optional] Create namespace (if not installing in default namespace).

```sh
$ kubectl create namespace yw-test
```

```
namespace/yw-test created
```

2. To apply the Kubernetes secret (obtained from Yugabyte Support), run the following `kubectl apply` command:

```sh
$ kubectl apply -f ~/Desktop/K8s/yugabyte-k8s-secret.yml -n yw-test
```

You should see a message saying that the secret was created.

```
secret/yugabyte-k8s-pull-secret created
```

3. Run the following `helm repo add` command to clone the [YugabyteDB charts repository](https://charts.yugabyte.com/).

    ```sh
    $ helm repo add yugabytedb https://charts.yugabyte.com
    ```

    A message should appear, similar to this:

    ```
    "yugabytedb" has been added to your repositories
    ```

If you have previously cloned the YugabyteDB charts repository, you can update it instead by running the following command:

```sh
$ helm repo update
```

To search for the available chart version, run this command:

```sh
$ helm search repo yugabytedb/yugabyte
```

The latest Helm Chart version and App version will be displayed.

```
NAME               	CHART VERSION	APP VERSION	DESRIPTION                                       
yugabytedb/yugabyte	2.3.0        	2.3.1.0	YugabyteDB is the high-performance distributed ..
```

4. Run the following `helm install` command to install Yugabyte Platform (YugaWare).

```sh
$ helm install yw-test yugabytedb/yugaware --version 2.3.0 -n yw-test --wait
```

A message should appear showing that the deployment succeeded.

```
NAME: yw-test
LAST DEPLOYED: Tue Jun  16 02:57:59 2020
NAMESPACE: yw-test
STATUS: deployed
REVISION: 1
TEST SUITE: None
```

To check all resources, run the following command:

```sh
$ kubectl get all -n yw-test
```

Details about all of your resources displays.

```
NAME                         READY   STATUS   RESTARTS   AGE
pod/yw-test-yugaware-0   6/6     Running   0                   5m51s

NAME                                     TYPE                CLUSTER-IP   EXTERNAL-IP    PORT(S)                                       AGE
service/yw-test-yugaware-ui   LoadBalancer   10.112.6.190   35.199.146.194   80:32446/TCP,9090:32018/TCP   5m51s

NAME                                           READY   AGE
statefulset.apps/yw-test-yugaware   1/1     5m52s
```

## Upgrade Yugabyte Platform (YugaWare)

To upgrade your installed Yugabyte Platform, run the following `helm upgrade` command.

```sh
$ helm upgrade yw-test yugabytedb/yugaware --version 2.3.0 --set image.tag=2.3.1.0-b15 -n yw-test
```

```
yw-test --timeout 900s --wait
Release "yw-test" has been upgraded. Happy Helming!
NAME: yw-test
LAST DEPLOYED: Tue Jun  30 03:56:38 2020
NAMESPACE: yw-test
STATUS: deployed
REVISION: 2
TEST SUITE: None
```

## Delete the Yugabyte Platform (YugaWare)

To remove the Yugabyte Platform, run the `helm delete` command:

```sh
$ helm del yw-test -n yw-test
```

A message displays that the Yugabyte Platform release and the namespace is deleted.

```
release "yw-test" uninstalled
```

Run the following command to remove the namespace:

```sh
$ kubectl delete namespace yw-test
```

You should see the following message:

```
namespace "yw-test" deleted
```

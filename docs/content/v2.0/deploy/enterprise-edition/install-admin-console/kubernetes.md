## Prerequisites

You must have a Kubernetes cluster that has [Helm](https://helm.sh/) configured. If you have not installed Helm client and server (aka Tiller) yet, follow the instructions [here](https://docs.helm.sh/using_helm/#installing-helm).

The YugaWare Helm chart documented here has been tested with the following software versions:

- Kubernetes 1.10+
- Helm 2.8.0+
- YugaWare Docker Images 1.1.0+
- Kubernetes node with minimum 4 CPU core and 15 GB RAM can be allocated to YugaWare.

Confirm that your `helm` is configured correctly.

```sh
$ helm version
```

```
Client: &version.Version{SemVer:"v2.10.0", GitCommit:"...", GitTreeState:"clean"}
Server: &version.Version{SemVer:"v2.10.0", GitCommit:"...", GitTreeState:"clean"}
```

## Create a cluster

### Create a service account with cluster-admin role privileges

For deploying a YugaWare Helm (v2.x) chart, you need to have a service account with `cluster-admin` role privileges — if the user in context already has that access, you can skip this step.

```sh
$ kubectl apply -f https://raw.githubusercontent.com/yugabyte/charts/master/stable/yugabyte/yugabyte-rbac.yaml
```

```sh
serviceaccount/yugabyte-helm created
clusterrolebinding.rbac.authorization.k8s.io/yugabyte-helm created
```

### Initialize Helm

Initialize `helm` with the service account but use the `--upgrade` flag to ensure that you can upgrade any previous initializations you may have made.

```sh
$ helm init --service-account yugabyte-helm --upgrade --wait
```

```sh
$HELM_HOME has been configured at /Users/<user>/.helm.

Tiller (the Helm server-side component) has been upgraded to the current version.
Happy Helming!
```

### Download YugaWare Helm Chart

You can do this as shown below.

```sh
$ wget https://downloads.yugabyte.com/kubernetes/yugaware-1.0.0.tgz
```

### Install YugaWare

Install YugaWare in the Kubernetes cluster using the command below.

```sh
$ helm install yugaware-1.0.0.tgz --name yb --set=image.tag=1.1.10.0-b3 --wait
```

### Check the cluster status

You can check the status of the cluster using various commands noted below.

```sh
$ helm status yb
```

```sh
LAST DEPLOYED: Wed Jan  2 14:12:27 2019
NAMESPACE: default
STATUS: DEPLOYED

RESOURCES:
==> v1/ConfigMap
NAME                           AGE
yb-yugaware-global-config      14d
yb-yugaware-app-config         14d
yb-yugaware-nginx-config       14d
yb-yugaware-prometheus-config  14d

==> v1/PersistentVolumeClaim
yb-yugaware-storage  14d

==> v1/ServiceAccount
yugaware  14d

==> v1/ClusterRole
yugaware  14d

==> v1/ClusterRoleBinding
yugaware  14d

==> v1/Service
yb-yugaware-ui  14d

==> v1/StatefulSet
yb-yugaware  14d

==> v1/Pod(related)

NAME           READY  STATUS   RESTARTS  AGE
yb-yugaware-0  5/5    Running  0         14d
```

Get service details.

```sh
$ kubectl get svc -lapp=yb-yugaware
```

```sh
NAME             TYPE           CLUSTER-IP    EXTERNAL-IP      PORT(S)                       AGE
yb-yugaware-ui   LoadBalancer   10.102.9.91   10.200.300.400   80:32495/TCP,9090:30087/TCP   15d
```

You can even check the history of the `yb` helm chart.

```sh
$ helm history yb
```

```sh
REVISION	UPDATED                 	STATUS  	CHART         	DESCRIPTION
1       	Wed Jan  2 14:12:27 2019	DEPLOYED	yugaware-1.0.0	Install complete
```

### Upgrade YugaWare

```sh
$ helm upgrade yb yugaware-1.0.0.tgz --set=image.tag=<new-tag> --wait
```

### Delete YugaWare

```sh
$ helm delete yb --purge
```

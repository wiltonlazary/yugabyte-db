---
title: Back up and restore
linkTitle: Back up and restore
description: Back up and restore
menu:
  latest:
    identifier: manage-ee-backup-restore
    parent: manage-enterprise-edition
    weight: 747
isTocNested: true
showAsideToc: true
---

This section will describe how to back up and restore tables using the YugabyteDB Admin Console.

## Create universe

First, create a universe similar to steps shown in [Create universe](../create-universe-multi-zone).
For the purposes of this demo we create a 1 node cluster that looks something like this. 

![Create universe 1 Node](/images/ee/br-create-universe.png)

Wait for the universe to become ready.

## Setting storage for backup

### Local storage

In this example, we create a local directory on the tserver to backup to. Select the
`Connect` modal in the `Nodes` tab of the universe and select the server from Admin Host.

![Connect Modal](/images/ee/br-connect-modal.png)

Once you are sshed in, create a directory `/backup` and change the owner to yugabyte.

```sh
$ sudo mkdir /backup; sudo chown yugabyte /backup
```

Note that when there are more than 1 nodes, an nfs mounted on each server is recommended, and
creating a local backup folder on each server will not work.

### AWS cloud storage

One can also backup to Amazon cloud using the `amazon S3` tab in Backup configuration.

![AWS Backup](/images/ee/br-aws-s3.png)

The Access Key & Secret text can be added for the IAM of the user. The destination S3 Bucket where backups are
stored can be entered in the format shown in the sample above.

## Backup

Now, select `Configuration` on the left panel, select the `Backup` tab on top, click `NFS` and enter
`/backup` as the NFS Storage Path before selecting `Save`. 

![Cloud Provider Configuration](/images/ee/cloud-provider-configuration.png)

Now, go to the `Backups` tab. There, click on `Create Backup`. A modal should appear where you can 
enter the table (this demo uses the default redis table) and NFS Storage option. If S3 was selected
as the storage, the "S3 Storage" dropdown option can be chosen during this backup creation.

![Backup Modal](/images/ee/create-backup-modal.png)

Select `OK`. If you refresh the page, you'll eventually see a completed task.

## Restore

On that same completed task, click on the `Actions` dropdown and click on `Restore Backup`. 
You will see a modal where you can select the universe, keyspace, and table you want to restore to. Enter in
values like this (making sure to change the table name you restore to) and click `OK`.

![Restore Modal](/images/ee/restore-backup-modal.png)

If you now go to the `Tasks` tab, you will eventually see a completed `Restore Backup` task. To
confirm this worked, go to the `Tables` tab to see both the original table and the table you
restored to.

![Tables View](/images/ee/tables-view.png)

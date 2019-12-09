---
title: Backing up data
linkTitle: Backing up data
description: Backing up data
image: /images/section_icons/manage/enterprise.png
headcontent: Backing up data in YugabyteDB.
aliases:
  - /manage/backup-restore/backing-up-data
menu:
  latest:
    identifier: manage-backup-restore-backing-up-data
    parent: manage-backup-restore
    weight: 703
---

This page covers backups for YugabyteDB. Here are some points to keep in mind.

- Export-based backups 
  - Single row ACID backups
  - Backup the schema and data separately
  - Multi-threaded parallelism

- Distributed backups 
  - Single table ACID backups
  - Integrated backup solution, integrated with object stores such as AWS S3
  - Massively parallel, efficient for very large data sets

Dump the keyspace schema (optional) as well as the data in the tables to create a backup of the data in YugabyteDB. Here are some points to bear in mind.

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#cassandra" class="nav-link active" id="cassandra-tab" data-toggle="tab" role="tab" aria-controls="cassandra" aria-selected="true">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="cassandra" class="tab-pane fade show active" role="tabpanel" aria-labelledby="cassandra-tab">
    {{% includeMarkdown "ycql/backing-up-data.md" /%}}
  </div>
</div>

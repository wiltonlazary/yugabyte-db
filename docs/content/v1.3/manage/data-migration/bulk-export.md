---
title: Bulk export
linkTitle: Bulk export
description: Bulk export
image: /images/section_icons/manage/enterprise.png
headcontent: Bulk export data from YugabyteDB.
block_indexing: true
menu:
  v1.3:
    identifier: manage-bulk-export
    parent: manage-bulk-import-export
    weight: 705
---

This page documents the options for export data out of YugabyteDB.

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#ycql" class="nav-link active" id="ycql-tab" data-toggle="tab" role="tab" aria-controls="ycql" aria-selected="true">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="ycql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="ycql-tab">
    {{% includeMarkdown "ycql/bulk-export.md" /%}}
  </div>
</div>

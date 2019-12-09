---
title: Time series
linkTitle: Time series
description: Time series
aliases:
  - /develop/learn/timeseries-data/
---

<ul class="nav nav-tabs nav-tabs-yb">
  <li class="active">
    <a href="#cassandra" class="nav-link active" id="cassandra-tab" data-toggle="tab" role="tab" aria-controls="cassandra" aria-selected="true">
      <i class="icon-java-bold" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
  <li>
    <a href="#redis" class="nav-link" id="redis-tab" data-toggle="tab" role="tab" aria-controls="redis" aria-selected="false">
      <i class="icon-java-bold" aria-hidden="true"></i>
      YEDIS
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="cassandra" class="tab-pane fade show active" role="tabpanel" aria-labelledby="cassandra-tab">
    {{% includeMarkdown "ycql/timeseries-data.md" /%}}
  </div>
  <div id="redis" class="tab-pane fade" role="tabpanel" aria-labelledby="redis-tab">
    {{% includeMarkdown "yedis/timeseries-data.md" /%}}
  </div>
</div>

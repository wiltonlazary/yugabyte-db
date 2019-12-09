---
title: C++
linkTitle: C++
description: Build a C++ application
aliases:
  - /develop/client-drivers/cpp/
  - /latest/develop/client-drivers/cpp/
menu:
  latest:
    identifier: build-apps-cpp
    parent: build-apps
    weight: 556
---

<ul class="nav nav-tabs nav-tabs-yb">
   <li>
    <a href="#ysql" class="nav-link active" id="ysql-tab" data-toggle="tab" role="tab" aria-controls="ysql" aria-selected="false">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li>
    <a href="#ycql" class="nav-link" id="ycql-tab" data-toggle="tab" role="tab" aria-controls="ycql" aria-selected="true">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="ysql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="ysql-tab">
    {{% includeMarkdown "ysql/cpp.md" /%}}
  </div>
  <div id="ycql" class="tab-pane fade" role="tabpanel" aria-labelledby="ycql-tab">
    {{% includeMarkdown "ycql/cpp.md" /%}}
  </div>
</div>

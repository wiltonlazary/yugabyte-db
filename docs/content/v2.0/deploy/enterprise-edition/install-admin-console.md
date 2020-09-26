---
title: Install Admin Console
linkTitle: 2. Install Admin Console
description: Install Admin Console
block_indexing: true
menu:
  v2.0:
    identifier: install-admin-console
    parent: deploy-enterprise-edition
    weight: 670
---
<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#default" class="nav-link active" id="default-tab" data-toggle="tab" role="tab" aria-controls="default" aria-selected="true">
      <i class="fas fa-cloud"></i>
      Default
    </a>
  </li>
  <li>
    <a href="#airgapped" class="nav-link" id="airgapped-tab" data-toggle="tab" role="tab" aria-controls="airgapped" aria-selected="true">
      <i class="fas fa-unlink"></i>
      Air-gapped
    </a>
  </li>
  <li>
    <a href="#kubernetes" class="nav-link" id="kubernetes-tab" data-toggle="tab" role="tab" aria-controls="kubernetes" aria-selected="false">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="default" class="tab-pane fade show active" role="tabpanel" aria-labelledby="default-tab">
    {{% includeMarkdown "install-admin-console/default.md" /%}}
  </div>
  <div id="airgapped" class="tab-pane fade" role="tabpanel" aria-labelledby="airgapped-tab">
    {{% includeMarkdown "install-admin-console/airgapped.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "install-admin-console/kubernetes.md" /%}}
  </div>
</div>

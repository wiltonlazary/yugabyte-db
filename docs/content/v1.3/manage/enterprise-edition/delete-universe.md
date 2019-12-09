---
title: Delete universe
linkTitle: Delete universe
description: Delete universe
menu:
  v1.3:
    identifier: manage-delete-universe
    parent: manage-enterprise-edition
    weight: 760
isTocNested: true
showAsideToc: true
---

To delete unwanted universes, select **Delete Universe** from the **More** drop-down on the **Universe Detail** page. For public clouds, such as AWS and Google Cloud Platform, the underlying compute instances are terminated after the database has been installed from those nodes. For On-Premises Datacenter, the underlying compute instances are no longer marked as `In Use` which then opens those instances up to be used in new universes.

![Delete Universe Dropdown](/images/ee/delete-univ-1.png)

![Delete Universe Confirmation](/images/ee/delete-univ-2.png)

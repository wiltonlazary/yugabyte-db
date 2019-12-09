---
title: Health checking and alerts
linkTitle: Health checking and alerts
description: Health checking and alerts
aliases:
  - /manage/enterprise-edition/cluster-health/
  - /manage/cluster-health/
menu:
  latest:
    identifier: cluster-health
    parent: manage-enterprise-edition
    weight: 740
isTocNested: true
showAsideToc: true
---

For staying aware of potential issues with your deployment, YugaWare has the capability to check on each individual universe for several types of issues and proactively send out email alerts when problems arise.

## How to enable and tweak

To configure health checking, visit your profile page in YugaWare by clicking the top-right dropdown featuring your account email and then clicking on the `Profile` entry.

![Profile Dropdown](/images/ee/health/profile-button.png)

You should see something like the following:

![Alerting Controls](/images/ee/health/alerting-controls.png)

Under the `Alerting Controls` section, there are two fields that you can edit:

- A text input for a CSV of custom email addresses to send alerts to.
- A toggle to switch on/off sending the same alerts back to the Yugabyte support team.

Either setting at least one email address or enabling sending to Yugabyte will turn the feature on and subsequently begin to track the health of your universes. Currently, this has two modes of operation:

- Every 5 minutes, the background checker will run over every universe and perform a set of checks for each individual node. If any of the checks fails, the Health tab of the universe will highlight the errors and an email will be sent out to all the configured email addresses.
- Every 12 hours, whether or not there are errors, a status email is sent out to ensure that the checking is actually taking place and you are not just getting a false sense of security!

Both of the timers are currently fixed, but will soon be user-configurable knobs.

## How to view the health of a universe

Finally, here is a sample of how to actually see the health of your universe, by navigating to any of your universes and clicking the `Health` tab:

![Universe Health](/images/ee/health/universe-health.png)

As you can see, the checks run every 5 minutes, across every node. Currently we explicitly check for the following:

- Uptime of both the `yb-master` and `yb-tserver` processes, which could indicate a node or process restart.
- Disk utilization on the various partitions configured on your nodes to ensure the database does not run out of storage.
- Presence of any internal google logging `FATAL` files, indicating some previous serious failure of Yugabyte.
- Presence of any core files, indicating some previous serious failure of Yugabyte.
- Total number of open file descriptors, which if too great, might end up causing problems in normal operation.
- Connectivity with either `cqlsh` or `redis-cli`, which could indicate either network connectivity issues in your deployment or server-side issues processing requests.

This list is not exhaustive, and we are actively working on expanding this! Furthermore, we are also working on more tightly integrating with the underlying `Prometheus` server that we bundle with YugaWare to have significantly more granular and user-configurable metrics-based alerting enabled in our product!

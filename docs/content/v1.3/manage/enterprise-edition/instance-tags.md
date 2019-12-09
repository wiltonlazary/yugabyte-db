---
title: Create and edit instance tags
linkTitle: Create and edit instance tags
description: Create and edit instance tags
menu:
  v1.3:
    identifier: manage-instance-tags
    parent: manage-enterprise-edition
    weight: 742
isTocNested: true
showAsideToc: true
---

The instances created on a cloud provider can be assigned special metadata to help manage, bill or audit the resources. On AWS they referred to as
[instance tags](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/Using_Tags.html). We provide a way to create and edit these from the
YugaWare universe dashboard.

{{< note title="Note" >}}
We provide this option to set instance tags for AWS cloud provider only currently.
{{< /note >}}

## 1. Create instance tags

During the universe creation step, with an AWS cloud provider, YugaWare will provide the option to input the instance tags as a map of keys/values as shown below.

![Create instance tags](/images/ee/inst-tags-1.png)

User can provide these key/values in any order.

### Templated tags

`Name` is the only key that can have `templated` tags, so it can be made of different parts filled in at run-time to determine node and instance names.

- The parts of the template should be enclosed between `${` and `}`.
- The reserved keywords that can be specified: `universe`, `instance-id`, `zone`, and `region`.
- Templated tag value needs to have `instance-id` at the minimum.
- Order of the parts of template do not matter.

*Check Cloud Provider instances*

Browse to the cloud provider's instances page. In this example, since we are using AWS as the cloud provider, browse to `EC2` -> `Running Instances`
in the correct availability zone and search for instances that have `test-tags` in their name. You should see something as follows in the
`Tags` tab of those instances.

![Instances with tags](/images/ee/inst-tags-aws-1.png)

{{< note title="Note" >}}
`yb-server-type` and `launched-by` are internally reserved key names.
{{< /note >}}

## 2. Edit instance tags

The map of instance tags can be changed using the edit universe operation. One can modify, insert or delete existing instance tags as shown below:

![Edit instance tags](/images/ee/inst-tags-2.png)

These are changes, compared to the input during the create universe:

- `Billing` was modified.
- `MyInfo` was deleted.
- `NewInfo` was added.
- `Department` was not changed in any way.
Note that `Name` key field cannot be edited in any form.

As before, one can confirm on the cloud provider list of instances that the tags have been updated correctly:
![Edited instances with tags](/images/ee/inst-tags-aws-2.png)

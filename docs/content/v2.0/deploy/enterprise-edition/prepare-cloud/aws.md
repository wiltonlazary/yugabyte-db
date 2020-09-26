
## 1. [Optional] Create a new security group

In order to access YugaWare from outside the AWS environment, you would need to enable access by assigning an appropriate security group to the YugaWare machine. You will at minimum need to:

- Access the YugaWare instance over ssh (port tcp:22)
- Check, manage and upgrade YugaWare (port tcp:8800)
- View the YugaWare console ui (port tcp:80)

Let us create a security group enabling all of that!

Go to `EC2` -> `Security Groups`, click on `Create Security Group` and add the following values:

- Enter `yugaware-sg` as the name (you can change the name if you want).
- Add a description (eg: `Security group for YugaWare access`).
- Add the appropriate ip addresses to the `Source IP ranges` field. To allow access from any machine, add `0.0.0.0/0` but note that this is not very secure.
- Add the ports `22`, `8800`, `80` to the `Port Range` field. The `Protocol` must be `TCP`.

You should see something like the screenshot below, click `Create` next.

![Create security group](/images/ee/aws-setup/yugaware-aws-create-sg.png)

## 2. [Optional] Create a new IAM role

In order for YugaWare to manage YugabyteDB nodes, it will require some limited access to your AWS infrastructure. This can be accomplished through directly providing a set of credentials, when configuring the AWS provider, which you can read more later on [here](../configure-cloud-providers/). Alternatively, the EC2 instance where YugaWare will be running can be brought up with an IAM role with enough permissions to take all the actions required by YugaWare. Below we provide a sample of such a role:

```sh
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Sid": "VisualEditor0",
            "Effect": "Allow",
            "Action": [
                "ec2:AttachVolume",
                "ec2:AuthorizeSecurityGroupIngress",
                "ec2:ImportVolume",
                "ec2:ModifyVolumeAttribute",
                "ec2:DescribeInstances",
                "ec2:DescribeInstanceAttribute",
                "ec2:CreateKeyPair",
                "ec2:DescribeVolumesModifications",
                "ec2:DeleteVolume",
                "ec2:DescribeVolumeStatus",
                "ec2:StartInstances",
                "ec2:DescribeAvailabilityZones",
                "ec2:CreateSecurityGroup",
                "ec2:DescribeVolumes",
                "ec2:ModifyInstanceAttribute",
                "ec2:DescribeKeyPairs",
                "ec2:DescribeInstanceStatus",
                "ec2:DetachVolume",
                "ec2:ModifyVolume",
                "ec2:TerminateInstances",
                "ec2:AssignIpv6Addresses",
                "ec2:ImportKeyPair",
                "ec2:DescribeTags",
                "ec2:CreateTags",
                "ec2:RunInstances",
                "ec2:AssignPrivateIpAddresses",
                "ec2:StopInstances",
                "ec2:AllocateAddress",
                "ec2:DescribeVolumeAttribute",
                "ec2:DescribeSecurityGroups",
                "ec2:CreateVolume",
                "ec2:EnableVolumeIO",
                "ec2:DescribeImages",
                "ec2:DescribeVpcs",
                "ec2:DeleteSecurityGroup",
                "ec2:DescribeSubnets",
                "ec2:DeleteKeyPair"
            ],
            "Resource": "*"
        }
    ]
}
```

## 3. Provision instance for YugaWare

Create an instance to run YugaWare. In order to do so, go to `EC2` -> `Instances` and click on `Launch Instance`. Fill in the following values.

- Change the boot disk image to `Ubuntu 16.04` and continue to the next step.
![Pick OS Image](/images/ee/aws-setup/yugaware-create-instance-os.png)

- Choose `c5.xlarge` (4 vCPUs are recommended for production) as the machine type. Continue to the next step.

- Choose the VPC, subnet and other settings as appropriate. Make sure to enable the `Auto-assign Public IP` setting, otherwise this machine would not be accessible from outside AWS. If you created an IAM role above, or already had one that you would like to use, provide that under `IAM role`. Continue to the next step.

- Increase the root storage volume size to at least `100GiB`. Continue to the next step.

- Add a tag to name the machine. You can set key to `Name` and value to `yugaware-1`. Continue to the next step.

- Select the `yugaware-sg` security group we created in the previous step (or the custom name you chose when setting up the security groups). Launch the instance.

- Pick an existing key pair (or create a new one) in order to access the machine. Make sure you have the ssh access key. This is important to enable `ssh` access to this machine. In this example, we will assume the key pair is `~/.ssh/yugaware.pem`.

Finally, click `Launch` to launch the YugaWare server. You should see a machine being created as shown in the image below.

![Pick OS Image](/images/ee/aws-setup/yugaware-machine-creation.png)

# Prerequisites
* Download and Install [gcloud](https://cloud.google.com/sdk/docs/) command line tool. 
* Clone git repo from [here](https://github.com/yugabyte/gcp-deployment-manager.git)

# Usage
[![Open in Cloud Shell](https://gstatic.com/cloudssh/images/open-btn.svg)](https://console.cloud.google.com/cloudshell/editor?cloudshell_git_repo=https%3A%2F%2Fgithub.com%2FYugaByte%2Fgcp-deployment-manager.git)

* Change current directory to cloned git repo directory
* Use gcloud command to create deployment-manager deployment <br/> 
    ```
    $ gcloud deployment-manager deployments create <your-deployment-name> --config=yugabyte-deployment.yaml
    ```
* Wait for 5-10 minutes after the creation of all resources is complete by the above command.
* Once the deployment creation is complete, you can describe it as shown below. <br/> 
    ```
    $ gcloud deployment-manager deployments describe <your-deployment-name>
    ```
    In the output, you will get the YugabyteDB admin URL, JDBC URL, YSQL, YCQL and YEDIS connection string. You can use YugaByte admin URL to access admin portal. 

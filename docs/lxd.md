# Deploying APIServer2 on LXD containers

APIServer2 was designed to be run behind a load balancer that serves the HTTPS traffic to the clients, APIServer2 only supports plain HTTP 1.1 keep-alive, the most simple and recommended setup is in a single VM using HAProxy as the load balancer and LXD native Linux containers, this is for high-performance and low management complexity. It can also be run as a container in Docker, Kubernetes or any container service on the Cloud.

```
+------------------------------------------------------------------+
|                          Ubuntu 24.04 VM                         |
|                                                                  |
|   Incoming HTTP Request                                          |
|          |                                                       |
|          v                                                       |
|   +--------------+                                               |
|   |   HAProxy    |                                               |
|   +------+-------+                                               |
|          |                                                       |
|          +------------[ Load balances HTTP traffic to ]-----------+
|          |                                                       |
|    +-----+--------------------+                                  |
|    |                          |                                  |
|    v                          v                                  |
| +---------------------+    +---------------------+               |
| |   LXD Container 1   |    |   LXD Container 2   |               |
| |       (node1)       |    |       (node2)       |               |
| |    [APIServer2]     |    |    [APIServer2]     |               |
| +---------------------+    +---------------------+               |
|                                                                  |
+------------------------------------------------------------------+
```

We provide an installation script `setup.sh` that runs on a clean Ubuntu 24.04 VM and installs and configures all the required software, including HAProxy, LXD, two containers running APIServer as a SystemD service, even the DNS configuration to provide name resolution for LXD containers is performed by this script. It will install a pre-compiled APIServer2 binary with all the examples shown above. You can edit `test.sh` to provide your own configuration for database connections, secrets, etc.

If you are using Multipass we suggest creating a VM like this:
```
multipass launch -n ha3 -c 4 -m 4G -d 8G
```
You will need at least 8GB of disk space for your VM, if you want to use more than 2 containers please note that each LXD container can take about 1.5G of disk space.

### **Step 1 - download script**
```
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/lxd/setup.sh && chmod +x setup.sh
```

### **Step 2 - configure script if necessary**
```
nano setup.sh
```
You may want to edit this section in particular:
```
# create run.sh script for apiserver2
tee "run.sh" > /dev/null <<'EOF'
#!/bin/bash

# server configuration
export PORT=8080
export POOL_SIZE=24
export IO_THREADS=4
export QUEUE_CAPACITY=500

# database configuration
export DB1="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=demodb;UID=sa;PWD=Basica2024;APP=apiserver;Encryption=off;ClientCharset=UTF-8"
export LOGINDB="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=testdb;UID=sa;PWD=Basica2024;APP=apiserver-login;Encryption=off;ClientCharset=UTF-8"

# cors configuration
export CORS_ORIGINS="null,file://"

# blobs storage configuration
export BLOB_PATH="/uploads"

# json web token configuration
export JWT_SECRET="B@asica2025*uuid0998554j93m722pQ"
export JWT_TIMEOUT_SECONDS=300

# api key for diagnostics
export API_KEY="6976f434-d9c1-11f0-93b8-5254000f64af"

# remote api configuration
export REMOTE_API_URL="http://localhost:8080"
export REMOTE_API_USER="mcordova"
export REMOTE_API_PASS="basica"

# executable
./apiserver
EOF
```

### **Step 3 - execute**
```
./setup.sh
```
It may take about 5 minutes for the process to finish, reasons that can make it fail:
* the VM is not brand-new and it already has HAProxy or LXD installed
* the VM has no connection to the internet thus it cannot download packages

We are using plain http with HAProxy on port 80, but it is easy to configure HTTPS for HAProxy, for simplicity's sake with this deployment script we are not using HTTPS.

### **Step 4 - test it**

Open a terminal on the same VM an execute several times to hit the different containers:
```
curl cpp14.mshome.net:8080/version -H "x-api-key: 6976f434-d9c1-11f0-93b8-5254000f64af" -s | jq
```
```
curl cpp14.mshome.net:8080/metrics -H "x-api-key: 6976f434-d9c1-11f0-93b8-5254000f64af" -s | jq
```
You should see output like this:
for `/version`: 
```
{
  "pod_name": "node1",
  "version": "1.1.1"
}
```
for `/metrics`: 
```
{
  "pod_name": "node2",
  "start_time": "2025-08-26T12:37:41",
  "total_requests": 35,
  "average_processing_time_seconds": 0.003105,
  "current_connections": 1,
  "current_active_threads": 0,
  "pending_tasks": 0,
  "thread_pool_size": 24,
  "total_ram_kb": 4008636,
  "memory_usage_kb": 16512,
  "memory_usage_percentage": 0.41
}
```
This `setup.sh` script can be taylored for more real-life production setups, with encrypted environment values, a private key and HTTPS/certificate setup for HAProxy.

### **Additional notes**

If you already have `setup.sh` pre-configured for your deployment environment, you can run a command that will download, execute and then delete the installation script, like this:
```
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/lxd/setup.sh && chmod +x setup.sh && ./setup.sh && rm setup.sh
```
You would be using your own local server to provide the files of course, so the URL in the command above should change.

You can configure this script to download encrypted environment values as .enc files and the corresponding private key `private.pem` required to decrypt, as well as a server certificate `PEM` file for HAProxy, this server certificate should contain all the necessary parts: the private key, the certificate, and the intermediate certificate, referred as the certificate chain by your certificate provider, they shound be stored in a single PEM file in that order.
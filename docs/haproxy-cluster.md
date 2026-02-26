# Install a resource-saving cluster with HAProxy and MicroK8s

High-availability in Kubernetes terms means at least 3 MicroK8s nodes, and it is a best practice not to use the 3 control-plane nodes for deploying pods, so you would need at least about 5-6 VMs to deploy your K8s cluster and mantain high availability if any of the nodes fails. Too many resources for some small-to-medium companies, and that's why it is better to use a serverless Kubernetes on the cloud if you are going really big, but for many companies, including small to medium Banks and institutions that want to use OnPrem Kubernetes this is not the only way. 

It is possible to deploy a high-availability cluster with little resources, combining HAProxy load balancer with a two independent single-node MicroK8s clusters, it is not high-availability in the sense of traditional kubernetes, which may be overkill for OnPrem, but this is very pragmatic and can easily manage the load for small-medium Banks and other organizations/institutions, running the middleware API for external mobile Apps or serving inter-organization APIs.

```mermaid
flowchart LR
    %% Define the entry point
    Client(["External Apps"])

    %% Define the HAProxy Node with Hardware Specs and Security Annotations
    LB{{"**HAProxy VM**<br>(Load Balancer)<br>_2-cores, 4GB RAM_<br>-----------------------<br>🔒 JA4 fingerprint blacklist<br>📉 Rate-limits<br>🛡️ API Protection"}}

    %% Backend Pool Subgraph
    subgraph BackendPool ["Backend VMs"]
        direction TB
        
        %% VM 1 with Specs
        subgraph VM1 ["microk8s VM2 (5-cores, 4GB RAM)"]
            direction LR
            Ingress1[/"**Ingress Controller**<br>-----------------------<br>🔀 Path rules"\]
            API1["**APIServer2 Pod**<br>-----------------------<br>🔑 JWT security"]
            
            %% Internal flow
            Ingress1 --> API1
        end

        %% VM 2 with Specs
        subgraph VM2 ["microk8s VM1 (5-cores, 4GB RAM)"]
            direction LR
            Ingress2[/"**Ingress Controller**<br>-----------------------<br>🔀 Path rules"\]
            API2["**APIServer2 Pod**<br>-----------------------<br>🔑 JWT security"]
            
            %% Internal flow
            Ingress2 --> API2
        end
    end

    %% Define External connections
    Client ==>|"TLS"| LB

    %% Traffic distribution to the Ingress inside the VMs
    LB -.->|"Distribute"| Ingress1
    LB -.->|"Distribute"| Ingress2

    %% Styling
    style LB fill:#ffecb3,stroke:#f57f17,stroke-width:2px,color:#000
    
    %% VM Styling (Container box)
    style VM1 fill:#e1f5fe,stroke:#0288d1,stroke-width:2px,color:#000
    style VM2 fill:#e1f5fe,stroke:#0288d1,stroke-width:2px,color:#000
    
    %% Pod Styling
    style Ingress1 fill:#ffffff,stroke:#333,stroke-width:1px
    style Ingress2 fill:#ffffff,stroke:#333,stroke-width:1px
    style API1 fill:#b2ebf2,stroke:#006064,stroke-width:1px
    style API2 fill:#b2ebf2,stroke:#006064,stroke-width:1px
```

There is a single point of failure, the HAProxy VM, but it can be restored in seconds, and recreated from scratch in very few minutes.
Very few resources are required for this setup, 12 cores and 12 GB of RAM distributed between 3 VMs, each MicroK8s cluster can start with 2 or 3 pods and scale up to 4 pods, providing the whole backend cluster with 8 pods if necessary, each Pod runs an APIServer2 multireactor EPOLL server, which is highly efficient for supporting thousands of concurrent connections with very few threads.

A multi-layer security approach is implemented in this setup, HAProxy provides rate-limits, JA4 fingerprints blacklist and API rules enforcement, transforming the load balancer into an API Gateway, protecting the ingress and the pods. The ingress implements path rules, nothing else, and the APIServer2 container implements the stateless JWT security model, including MFA (multi-factor authentication).

## Step 1: Provisioning the test environment

Let's assume you are running Multipass on Windows 10/11 Pro. Open a command shell and create these VMs, we will use even less resources, 10 cores and 8GB of RAM for the whole cluster.
```
multipass launch -n ha -c 2 -m 2g -d 6g
multipass launch -n node1 -c 4 -m 4g -d 8g
multipass launch -n node2 -c 4 -m 4g -d 8g
```
Take note of the IPs:
```
multipass list
```
Expected output (IPs will vary):
```
ha                      Running           172.23.240.163   Ubuntu 24.04 LTS
node1                   Running           172.27.113.14    Ubuntu 24.04 LTS
node2                   Running           172.27.121.1     Ubuntu 24.04 LTS
```

## Step 2: Install HAProxy v3
```
multipass shell ha
```
Now you are into the Linux terminal of the `ha` VM, take note of the IPs of `node1` and `node2` VMs.
Download the installation script:
```
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/install-ha.sh && chmod +x install-ha.sh
```
Edit it to point to the MicroK8s nodes IPs:
```
nano install-ha.sh
```
Edit this section, save and exit:
```
# Backend Kubernetes Nodes (Fixed IPs from provision-vms.ps1)
NODE1_IP="172.27.113.14"
NODE2_IP="172.27.121.1"
```
Run the script:
```
./install-ha.sh
```
Expected output:
```
---------------------------------------------------------------------------------
Starting HAProxy 3.2 Setup on 172.23.240.163
Target Backends: 172.27.113.14, 172.27.121.1
---------------------------------------------------------------------------------
[+] STEP 1: Updating system and installing dependencies...
Scanning processes...
Scanning linux images...
[✓] system updated and dependencies installed
[+] STEP 2: Installing HAProxy 3.2 via official PPA...
Scanning processes...
Scanning linux images...
[✓] HAProxy version 3.2.13 installed
[+] STEP 3: Generating SSL Certificate...
[✓] SSL certificate generated at /etc/ssl/private/haproxy.pem
[+] STEP 4: Configuring JA4 TLS Fingerprinting and Blacklists...
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100  5588  100  5588    0     0  25413      0 --:--:-- --:--:-- --:--:-- 25515
[✓] JA4 Lua script and Blacklist (8 entries) integrated
[+] STEP 5: Generating HAProxy configuration with Advanced Security...
[✓] HAProxy configuration applied
[+] STEP 6: Creating hainfo utility...
[✓] hainfo utility created in /bin
[+] STEP 7: Verifying local proxy response...

Broadcast message from systemd-journald@ha (Thu 2026-02-26 18:39:27 UTC):

haproxy[3195]: backend microk8s has no server available!


Broadcast message from systemd-journald@ha (Thu 2026-02-26 18:39:27 UTC):

haproxy[3195]: backend microk8s has no server available!

[✓] Load Balancer is successfully communicating with K8s backends
[✓] HAProxy installation completed in 0m 29s.
 →→ HAProxy is Active at 172.27.116.163 ←←
```
At this time there are no MicroK8s nodes active, hence the message `backend microk8s has no server available!`, it is normal, do not worry.
Execute this handy status script to monitor your haproxy service:
```
hainfo
```
Expected output:
```
PXNAME       SVNAME       STATUS   RATE_CUR   REQ_TOT    ECONN      ERESP      CHKFAIL
---------------------------------------------------------------------------------------------
microk8s     node1        DOWN     0          0          0          0          1
microk8s     node2        DOWN     0          0          1          0          1
```
That `hainfo` utility script was created by the installation script.

## Step 3: Install MicroK8s on the VMs node1 and node2

Open two command shell terminals on Windows.
On each one:
```
multipass shell node1
```
```
multipass shell node2  
```
Now you should had open two Linux terminals, let's install MicroK8s on each one, the procedure is almost the same you [already know](https://github.com/cppservergit/apiserver2/blob/main/docs/microk8s.md), but we need to edit and configure the script before running it, to make the Traefik ingress trust in HAProxy regarding the original client IP address.

On each node (node1, node2) execute:
```
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/setup.sh && chmod +x setup.sh
```
Edit the script to change the HAProxy IP:
```
nano setup.sh
```
Change this:
```
# --- SCRIPT CONFIGURATION ---
HAPROXY_IP="172.27.116.163" # external HAProxy IP
```
Now we ar ready to install MicroK8s, on each node execute:
```
./setup.sh
```
Expected output on each node:
```
----------------------------------------------------------------
 Express MicroK8s single-node setup for APIServer2
----------------------------------------------------------------
[+] STEP 1: Mounting upload directory...
[✓] mount directory ready
[+] STEP 2: Tuning sysctl...
[✓] sysctl updated
[+] STEP 3: Creating MicroK8s Launch configuration...
[✓] Launch configuration installed.
[+] STEP 4: Installing MicroK8s...
2026-02-26T17:30:22-04:00 INFO Waiting for automatic snapd restart...
microk8s (1.35/stable) v1.35.0 from Canonical✓ installed
[+] Waiting for MicroK8s to be ready...
[✓] MicroK8s base system installed.
[+] STEP 5: Installing traefik ingress...
[✓] traefik installed.
[+] STEP 6: Deploying APIserver2...
[✓] APIServer2 deployment is ready.
[+] STEP 7: Finalizing configuration...
[✓] MicroK8s/APIServer2 setup completed in 1m 5s.

[+] EXTRA step: Waiting for all Pods to be running - it may take a few minutes...
[✓] Pods are ready.

 →→ Please LOG LOG BACK IN for group changes to take effect. ←←
```
It may take 2-3 minutes for each node to be ready to serve requests via HAProxy, when you see the message `Pods are ready` is time to test it.

Step #4: Test

Go to the HAProxy terminal and execute:
```
hainfo
```
Expected output:
```
PXNAME       SVNAME       STATUS   RATE_CUR   REQ_TOT    ECONN      ERESP      CHKFAIL
---------------------------------------------------------------------------------------------
microk8s     node1        UP       0          0          0          0          1
microk8s     node2        UP       0          0          0          0          1
```
Perfect! both nodes are UP, our MicroK8s cluster is ready.

On the HAProxy terminal execute a couple of times:
```
curl https://localhost/api/version -ks -H "Authorization: Bearer 6976f434-d9c1-11f0-93b8-5254000f64af" | jq
```
Expected output:
```
{
  "pod_name": "apiserver2-5f7748d9cd-v824x",
  "version": "1.1.9"
}
```
The `pod_name` attribute will change on each request, you default APIServer2 configuration launches 2 Pods on each node, you have a stateless cluster of 4 pods.
In a secure setup, your MicroK8s VMs are not visible to the remote clients, are located in a network segment not exposed to the internet or remote clients, they can only reach the HAProxy VM, and only via HTTPS. Your HAProxy configuration contains several API/RateLimit/JA4 rules to protect your cluster, attacks will be recorded in the HAProxy VM logs and most of them will not reach the Pods.

If you HAProxy VM fails, you will experience interruption of service, but it would be very brief, it can be restored in seconds from a snapshot using your VM Hypervisor, or even reinstalled from scratch on a new VM if necessary.

The same applies to the MicroK8s VMs.

**Note**: Multipass on Windows 11 makes testing an architecture like this very easy, but it not intended for production, even for development, if the Windows host gets restarted, the Multipass VMs may change their IP address and then the whole cluster will not work anymore, but as you have just experienced, it takes only a few minutes to rebuild the whole cluster.

# Running APIServer2 with MicroK8s on Ubuntu 24.04

In this tutorial you will use APIServer2 docker image straight from DockerHub to build a single-node Kubernetes cluster with 2 Pods running the container and an integrated Load Balancer (Nginx Ingress), this is equivalent to our LXD setup, but more dynamic in nature and more Cloud-ready for serverless applications. In a few minutes you will have a complete Kubernetes system ready to roll. This APIServer2 image contains all the example APIs.

## Step 1: Create the VM
You will need a clean Ubuntu 24.04 VM, assuming you are using Multipass on Windows, this is the minimal VM acceptable for this configuration:
```
multipass launch -n mk8s -c 4 -m 4g -d 8g
```

Update the system:
```
sudo apt update && sudo apt upgrade -y
```
## Step2: Install MicroK8s
```
sudo snap install microk8s --classic
```
Test the MicroK8s service:
```
sudo microk8s status --wait-ready
```
When the installation is complete, you will see this output:
```
microk8s is running
high-availability: no
  datastore master nodes: 127.0.0.1:19001
  datastore standby nodes: none
addons:
  enabled:
    dns                  # (core) CoreDNS
    ha-cluster           # (core) Configure high availability on the current node
    helm                 # (core) Helm - the package manager for Kubernetes
    helm3                # (core) Helm 3 - the package manager for Kubernetes
  disabled:
    cert-manager         # (core) Cloud native certificate management
    cis-hardening        # (core) Apply CIS K8s hardening
    community            # (core) The community addons repository
    dashboard            # (core) The Kubernetes dashboard
    gpu                  # (core) Alias to nvidia add-on
    host-access          # (core) Allow Pods connecting to Host services smoothly
    hostpath-storage     # (core) Storage class; allocates storage from host directory
    ingress              # (core) Ingress controller for external access
    kube-ovn             # (core) An advanced network fabric for Kubernetes
    mayastor             # (core) OpenEBS MayaStor
    metallb              # (core) Loadbalancer for your Kubernetes cluster
    metrics-server       # (core) K8s Metrics Server for API access to service metrics
    minio                # (core) MinIO object storage
    nvidia               # (core) NVIDIA hardware (GPU and network) support
    observability        # (core) A lightweight observability stack for logs, traces and metrics
    prometheus           # (core) Prometheus operator for monitoring and logging
    rbac                 # (core) Role-Based Access Control for authorisation
    registry             # (core) Private image registry exposed on localhost:32000
    rook-ceph            # (core) Distributed Ceph storage using Rook
    storage              # (core) Alias to hostpath-storage add-on, deprecated
```

## Step 3: Prepare the terminal for easy MicroK8s commands

To avoid typing long commands and using `sudo` every time:

```
sudo usermod -a -G microk8s $USER && mkdir -p ~/.kube && chmod 0700 ~/.kube && \
echo "alias kubectl='microk8s kubectl'" >> ~/.bash_aliases && \
source ~/.bash_aliases
```
Exit the Linux terminal and re-enter for these changes to take effect.

Test:
```
microk8s version
```
Expected output (version may vary):
```
MicroK8s v1.32.9 revision 8511
```
Test:
```
kubectl version
```
Expected output (version may vary):
```
Client Version: v1.32.9
Kustomize Version: v5.5.0
Server Version: v1.32.9
```
The should work now without `sudo` or the long command `microk8s kubectl`.

## Step 4: Install the required extensions

```
microk8s enable hostpath-storage && \
microk8s enable ingress && \
microk8s enable metrics-server && \
microk8s enable registry
```
Test:
```
microk8s status --wait-ready
```
Expected output:
```
microk8s is running
high-availability: no
  datastore master nodes: 127.0.0.1:19001
  datastore standby nodes: none
addons:
  enabled:
    dns                  # (core) CoreDNS
    ha-cluster           # (core) Configure high availability on the current node
    helm                 # (core) Helm - the package manager for Kubernetes
    helm3                # (core) Helm 3 - the package manager for Kubernetes
    hostpath-storage     # (core) Storage class; allocates storage from host directory
    ingress              # (core) Ingress controller for external access
    metrics-server       # (core) K8s Metrics Server for API access to service metrics
    registry             # (core) Private image registry exposed on localhost:32000
    storage              # (core) Alias to hostpath-storage add-on, deprecated
    ... other messages ....
```
Apply patch to the Ingress so it will listen on the public IP of the host VM on ports 443 and 80
```
microk8s kubectl patch daemonset nginx-ingress-microk8s-controller -n ingress \
  --type='json' \
  -p='[{"op": "add", "path": "/spec/template/spec/hostNetwork", "value": true}, {"op": "add", "path": "/spec/template/spec/dnsPolicy", "value": "ClusterFirstWithHostNet"}]'
```
Test:
```
kubectl get daemonset nginx-ingress-microk8s-controller -n ingress
```
Expected output:
```
Name:           nginx-ingress-microk8s-controller
Selector:       name=nginx-ingress-microk8s
Node-Selector:  <none>
Labels:         microk8s-application=nginx-ingress-microk8s
Annotations:    deprecated.daemonset.template.generation: 2
Desired Number of Nodes Scheduled: 1
Current Number of Nodes Scheduled: 1
Number of Nodes Scheduled with Up-to-date Pods: 1
Number of Nodes Scheduled with Available Pods: 1
Number of Nodes Misscheduled: 0
Pods Status:  1 Running / 0 Waiting / 0 Succeeded / 0 Failed
Pod Template:
  Labels:           name=nginx-ingress-microk8s
  Service Account:  nginx-ingress-microk8s-serviceaccount
  Containers:
   nginx-ingress-microk8s:
    Image:       registry.k8s.io/ingress-nginx/controller:v1.11.5
    Ports:       80/TCP, 443/TCP, 10254/TCP
    Host Ports:  80/TCP, 443/TCP, 10254/TCP
    Args:
      /nginx-ingress-controller
      --configmap=$(POD_NAMESPACE)/nginx-load-balancer-microk8s-conf
      --tcp-services-configmap=$(POD_NAMESPACE)/nginx-ingress-tcp-microk8s-conf
      --udp-services-configmap=$(POD_NAMESPACE)/nginx-ingress-udp-microk8s-conf
      --ingress-class=public

      --publish-status-address=127.0.0.1
    Liveness:   http-get http://:10254/healthz delay=10s timeout=5s period=10s #success=1 #failure=3
    Readiness:  http-get http://:10254/healthz delay=0s timeout=5s period=10s #success=1 #failure=3
    Environment:
      POD_NAME:        (v1:metadata.name)
      POD_NAMESPACE:   (v1:metadata.namespace)
    Mounts:           <none>
  Volumes:            <none>
  Node-Selectors:     <none>
  Tolerations:        <none>
Events:               <none>
```

## Step 5: Deploy the APIServer2 container

To deploy a container you need a YAML file with several sections, for secrets (security-sensitive environment variables), storage requirements for uploads, container specs, etc, it may be a bit complex to do it from scratch, so we supply a reasonable default configuration for this QuickStart kubernetes deployment on a single-node cluster. MicroK8s is the ideal Kubernets implementation for this task.

Download the latest version of APIServer2 MicroK8s deployment from GitHub:
```
curl -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/deploy-apiserver.yaml 
```
Deploy the containers into MicroK8s:
```
kubectl apply -f deploy-apiserver.yaml
```
List your Pods (containers)
```
kubectl get pods -o wide
```
Expected output (Pod names and IPs will vary):
```
NAME                                    READY   STATUS    RESTARTS   AGE   IP            NODE              NOMINATED NODE   READINESS GATES
apiserver-deployment-784bcb5449-dsl9c   1/1     Running   0          12h   10.1.252.19   mk8s.mshome.net   <none>           <none>
apiserver-deployment-784bcb5449-lwn2w   1/1     Running   0          12h   10.1.252.18   mk8s.mshome.net   <none>           <none>
```
Test your Pods (several times):
```
curl https://localhost/api/metrics -k -H "x-api-key: 6976f434-d9c1-11f0-93b8-5254000f64af" -s | jq
```
Expected output:
```
{
  "pod_name": "apiserver-deployment-784bcb5449-dsl9c",
  "start_time": "2025-12-22T04:21:34",
  "total_requests": 721,
  "average_processing_time_seconds": 0.004351,
  "current_connections": 1,
  "current_active_threads": 0,
  "pending_tasks": 0,
  "thread_pool_size": 24,
  "total_ram_kb": 4008592,
  "memory_usage_kb": 28288,
  "memory_usage_percentage": 0.71
}
```
If you execute several times the command above you will see a different Pod responding.
From a remote machine you can use the VM DNS name or its IP address to connect to the API:
```
curl https://mk8s.mshome.net/api/metrics -k -H "x-api-key: 6976f434-d9c1-11f0-93b8-5254000f64af" -s | jq
```

Check the Ingress (load balancer) HTTP access logs:
```
kubectl logs -n ingress -l name=nginx-ingress-microk8s
```
Expected output, something like this:
```
172.22.0.1 - - [22/Dec/2025:15:04:19 +0000] "GET /api/metrics HTTP/1.1" 200 487 "-" "curl/8.16.0" 139 0.000 [default-apiserver-service-8080] [] 10.1.252.18:8080 487 0.000 200 3b139f00a48827441ad43b695650ea77
172.22.0.1 - - [22/Dec/2025:15:04:20 +0000] "GET /api/metrics HTTP/1.1" 200 487 "-" "curl/8.16.0" 139 0.001 [default-apiserver-service-8080] [] 10.1.252.18:8080 487 0.000 200 18fdaceca1238bb72e51ae7815a91cea
```
APIServer2 Pods logs:
```
kubectl logs -l app=apiserver2 --all-containers=true --timestamps=true
```
You may not see any logs if there are not enough log records in the buffer, after some activity you will start seeing log records, like these:
```
2025-12-22T12:40:32.863063835-04:00 [  WARN  ] [Thread: 131130846078656] [eef5aba8-df54-11f0-82e4-52540016bd16] SQL connection error on 'LOGINDB' (SQLSTATE: HY000). Attempting reconnect. Error: ODBC Error on 'SQLExecute': [SQLState: HY000] [Native Error: 0] [FreeTDS][SQL Server]Unknown error
2025-12-22T12:40:47.049865971-04:00 [  INFO  ] [Thread: 126022385133248] [f1ed8d9e-df54-11f0-b26f-52540016bd16] Login OK for user 'mcordova': sessionId 15c204d1-ff4b-419d-a875-3bcae33362f6 - from 172.22.4.248
```

Other variants of the `logs` command for the Ingress:
```
kubectl logs -n ingress -l name=nginx-ingress-microk8s --tail=1000
kubectl logs -n ingress -l name=nginx-ingress-microk8s --since=1h
# warnign: all logs!
kubectl logs -n ingress -l name=nginx-ingress-microk8s --tail=-1 
```
The same flags apply to the APIServer2 `logs` command.

Inspect the state of the Ingress Pod:
```
kubectl describe pod -n ingress -l name=nginx-ingress-microk8s
```

Check APIServer2 resource usage (cpu, memory):
```
kubectl top pods
```

Check all Pods resources usage:
```
kubectl top pod --all-namespaces \
  --no-headers | awk '{print $2, $1, $3}' | sort -k3 -h | column -t
```

## Step 6: Testing all APIs

APIServer2 container includes a set of sample APIs, and a bash script using CURL for unit-testing is also provided, just download it and you are ready to go.

Download from GitHub the latest version of `test.sh` script:
```
curl -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/unit-test/test.sh && chmod +x test.sh
```
Run the script, change the URL to your VM address if necessary. The /api prefix is required, otherwise the request is rejected by the Ingress, this is to protect the Pods against common HTTP attacks. Please note that `test.sh` requires the program `uuid`, if not present the script will install it before running the tests.

```
./test.sh https://mk8s.mshome.net /api
```
Expected output:
```
GET /api/shippers                   200    true
GET /api/products                   200    true
GET /api/metrics                    200    true
GET /api/version                    200    true
GET /api/ping                       200    true
POST /api/customer                  200    true
POST /api/customer                  200    true
POST /api/customer                  200    true
POST /api/customer                  200    true
POST /api/customer                  200    true
POST /api/customer                  200    true
POST /api/customer                  200    true
POST /api/sales                     200    true
POST /api/sales                     200    true
POST /api/sales                     200    true
POST /api/rcustomer                 200    true
```
If you run it several times you will see the logs on MicroK8s and the metrics of the Pods changing.
This is a simple but effective tool, it authenticates, then calls the secure APIs sending the resulting JWT token, it also calls diagnostic APIs using the pre-configured API Key defined in the YAML file, it is a tester that you can adapt to your own developments.

That's it, welcome to Kubernetes and high-performance light C++ containers, the easy way.

# Running APIServer2 with MicroK8s on Ubuntu 24.04

In this tutorial you will use APIServer2 docker image from DockerHub to build a single-node Kubernetes v1.35 cluster with 2 Pods running the container and an integrated Load Balancer (Traefik Ingress) that will listen on ports 80/443 of the host’s public IP. This setup is scalable up to 3 pods using a very modest VM (4 cores, 4GB RAM), with all the self-healing, auto-administration and scalability of MicroK8s Kubernets implementation, also Cloud-ready for serverless applications. In a few minutes you will have a complete Kubernetes system ready to roll. This APIServer2 image contains all the example APIs, requires the same [DemoDB SQL Server container](https://github.com/cppservergit/apiserver2/blob/main/docs/sqlserver.md) as the bare-metal tutorial, it assumes demodb.mshome.net as its default location, you can edit the APIServer2 YAML file to change the secrets and point to another database host. We recommend MicroK8s to use APIServer2 for OnPrem production, single-node or in high-availability (3+ VMs), depending on your resources.

```mermaid
flowchart TD
    User((Internet Traffic))

    subgraph UbuntuVM [Ubuntu 24.04 VM]
        style UbuntuVM fill:#f9f9f9,stroke:#333,stroke-width:2px
        
        HostIP["Host Network Interface<br/>(Public IP)"]
        style HostIP fill:#fff,stroke:#333
        
        subgraph MicroK8s [MicroK8s Cluster]
            style MicroK8s fill:#e1f5fe,stroke:#0277bd,stroke-width:2px,stroke-dasharray: 5 5
            
            Ingress["<b>Ingress Controller Pod</b><br/>(Bound to Host Ports 80/443)"]
            style Ingress fill:#b3e5fc,stroke:#0277bd
            
            Service["<b>Service</b><br/>(ClusterIP / LoadBalancer)"]
            style Service fill:#fff9c4,stroke:#fbc02d
            
            Pod1["<b>APIServer2</b><br/>Pod Replica 1"]
            style Pod1 fill:#c8e6c9,stroke:#388e3c
            
            Pod2["<b>APIServer2</b><br/>Pod Replica 2"]
            style Pod2 fill:#c8e6c9,stroke:#388e3c
        end
    end

    User -->|HTTPS :443 / HTTP :80| HostIP
    HostIP -->|Traffic Forwarding| Ingress
    Ingress -->|Routing Rules| Service
    Service -->|Load Balance| Pod1
    Service -->|Load Balance| Pod2
```

## Step 1: Create the VM
You will need a clean Ubuntu 24.04 VM, assuming you are using Multipass on Windows 10/11, this is the minimal VM for this configuration:
```
multipass launch -n mk8s -c 4 -m 4g -d 8g
```
**Note**: for this tutorial to run you need another VM named demodb.mshome.net running the SQLServer 2019 demo database used by APIServer2 example APIs, please check this [tutorial](https://github.com/cppservergit/apiserver2/blob/main/docs/sqlserver.md) to quickly create that VM and install the database, it takes less than 10 minutes. If you installed the database in another VM you will have to edit the deploy-apiserver.yaml file and change the Kubernet secrets referring to the databases.


Enter you Linux VM shell:
```
multipass shell mk8s
```

## Step 2: Install MicroK8s and deploy APIServer2 cluster
Download and run the installation script, this will take about 2 minutes if you have a fast connection to the internet:
```
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/setup.sh && chmod +x setup.sh && ./setup.sh
```
This script updates de operating system, installs MicroK8s and the required extensions (ingress, host storage, metrics server, etc), configures the load balancer, tests the http ports and deploys the APIServer containers.

When the script ends you will see these messages at the end of the output:
```
[+] Updating the operating system, please wait...
Scanning processes...
Scanning candidates...
Scanning linux images...
[+] Installing MicroK8s via snap...
[+] Waiting for MicroK8s to be ready...
[✓] MicroK8s is ready.
Client Version: v1.32.9
Kustomize Version: v5.5.0
Server Version: v1.32.9
[+] Installing add-ons: hostpath-storage
Infer repository core for addon hostpath-storage
[+] Installing add-ons: ingress
Infer repository core for addon ingress
[+] Installing add-ons: metrics-server
Infer repository core for addon metrics-server
[+] MicroK8s base system installed.
[+] Patching ingress controller to redirect HTTP to HTTPS...
configmap/nginx-load-balancer-microk8s-conf patched
[+] Patching ingress controller to mount host /etc/localtime...
daemonset.apps/nginx-ingress-microk8s-controller patched
[+] Waiting for the ingress controller pod to be ready - this may take 1-2 minutes...
[✓] Ingress deployed.
[+] Testing HTTP connectivity...
[✓] Ingress is serving HTTP traffic at port 80
[+] Testing HTTPS connectivity...
[✓] Ingress is serving HTTPS traffic at 443
[+] Retrieving APIserver2 deployment manifest...
[+] Deploying APIserver2...
secret/apiserver2-secrets created
persistentvolume/apiserver2-pv created
persistentvolumeclaim/apiserver2-pvc created
deployment.apps/apiserver2 created
service/apiserver2-service created
ingress.networking.k8s.io/apiserver2-ingress created
horizontalpodautoscaler.autoscaling/apiserver2-hpa created
[+] Waiting for APIServer2 Pods to be Ready...
[✓] APIServer2 deployment is ready.
[+] Testing APIServer2 connectivity...
[✓] APIServer2 is ready to accept requests at port 443
[+] APIServer2 version: 1.1.6
[+] Waiting for all the Kubernetes pods to be ready...
[✓] Pods are ready.
[+] Adding current user to microk8s group...
[+] Setting up kubectl alias...
[✓] MicroK8s/APIServer2 setup completed.

 →→ Please LOG OUT AND LOG BACK IN for group changes to take effect. ←←
```
Exit the shell and enter again.

## Step 3: Test the installation

List APIServer2 pods
```
kubectl get pods -o wide
```
Expected output:
```
NAME                                    READY   STATUS    RESTARTS   AGE     IP             NODE   NOMINATED NODE   READINESS GATES
apiserver2-784bcb5449-6cs97   1/1     Running   0          5m12s   10.1.215.200   mk8s   <none>           <none>
apiserver2-784bcb5449-mmctd   1/1     Running   0          5m12s   10.1.215.201   mk8s   <none>           <none>
```

Check the Ingress pods (load balancer):
```
kubectl get pods -n ingress
```
Expected output (names may vary):
```
NAME                                      READY   STATUS    RESTARTS   AGE
nginx-ingress-microk8s-controller-4l548   1/1     Running   0          74s
```

Test APIServer2 diagnostics API, do it several times to see the different Pods responding:
```
curl https://localhost/api/metrics -ks -H "Authorization: Bearer 6976f434-d9c1-11f0-93b8-5254000f64af" | jq
```
Expected output:
```
{
  "pod_name": "apiserver2-5dcd6864ff-xnvs7",
  "start_time": "2026-01-04T21:13:16",
  "total_requests": 0,
  "average_processing_time_seconds": 0.000000,
  "current_connections": 1,
  "current_active_threads": 0,
  "pending_tasks": 0,
  "thread_pool_size": 4,
  "total_ram_kb": 4008584,
  "memory_usage_kb": 13052,
  "memory_usage_percentage": 0.33
}
```
If you execute several times the command above you will see a different Pod responding.
From a remote machine you can use the VM DNS name or its IP address to connect to the API:
```
curl https://mk8s.mshome.net/api/metrics -ks -H "Authorization: Bearer 6976f434-d9c1-11f0-93b8-5254000f64af" | jq
```

Get APIServer2 version:
```
curl https://localhost/api/version -ks -H "Authorization: Bearer 6976f434-d9c1-11f0-93b8-5254000f64af" | jq
```
Expected output:
```
{
  "version": "1.1.6"
}
```

Invoke the `login` API to force a test of the connection to the database:
```
curl --json '{"username":"mcordova", "password":"basica"}' https://localhost/api/login -ks | jq
```
Expected output (token will vary):
```
{
  "displayname": "Martín Córdova",
  "id_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6Im1hcnRpbi5jb3Jkb3ZhQGdtYWlsLmNvbSIsImV4cCI6IjE3NjczMjIyNDMiLCJpYXQiOiIxNzY3MzIxOTQzIiwicm9sZXMiOiJzeXNhZG1pbiwgY2FuX2RlbGV0ZSwgY2FuX3VwZGF0ZSIsInNlc3Npb25JZCI6ImQ1MzYyNzk0LTI5NWEtNDQyNS1iMDUzLTA1YzgyZThhODhhNCIsInVzZXIiOiJtY29yZG92YSJ9.ewQIim-PBxwoG7sED4l0i1NuzBuMr5Uwg1D_oYifvW0",
  "token_type": "bearer"
}
```

A 2-liner to test a secure API with database access:
```
TOKEN=$(curl --json '{"username":"mcordova", "password":"basica"}' "https://localhost/api/login" -ks | jq -r '.id_token')
curl "https://localhost/api/customer" -ks --json '{"id":"ANATR"}' -H "Authorization: Bearer $TOKEN" | jq
```
You should see the JSON response of the `/api/customer` endpoint.

Check the Ingress (load balancer) HTTP access logs:
```
microk8s kubectl logs -n ingress daemonset/traefik
```
Expected output, something like this:
```
172.22.0.1 - - [22/Dec/2025:15:04:19 +0000] "GET /api/metrics HTTP/1.1" 200 487 "-" "curl/8.16.0" 139 0.000 [default-apiserver-service-8080] [] 10.1.252.18:8080 487 0.000 200 3b139f00a48827441ad43b695650ea77
172.22.0.1 - - [22/Dec/2025:15:04:20 +0000] "GET /api/metrics HTTP/1.1" 200 487 "-" "curl/8.16.0" 139 0.001 [default-apiserver-service-8080] [] 10.1.252.18:8080 487 0.000 200 18fdaceca1238bb72e51ae7815a91cea
```
APIServer2 Pods logs:
```
kubectl logs -l app=apiserver2 -c=apiserver2 -n cppserver --timestamps=true
```
Expected output:
```
2026-01-04T21:13:16.474527564-04:00 [  INFO  ] [Thread: 140638524725888] [--------] Application starting...
2026-01-04T21:13:16.478812677-04:00 [  INFO  ] [Thread: 140638524725888] [--------] CORS enabled for 2 origin(s).
2026-01-04T21:13:16.478819076-04:00 [  INFO  ] [Thread: 140638524725888] [--------] APIServer2 version 1.1.5 starting on port 8080 with 1 I/O threads and 4 total worker threads.
2026-01-04T21:13:16.478820876-04:00 [  INFO  ] [Thread: 140638524725888] [--------] Assigning 4 worker threads per I/O worker.
2026-01-04T21:25:56.326279372-04:00 [  INFO  ] [Thread: 140638435669696] [7bc3e1aa-e9d5-11f0-869f-5254008ff37c] Login OK for user 'mcordova': sessionId 7c7f67f5-61f6-4795-bb23-9b1e2d0ec26c - from 172.25.65.150
```
The command above consolidates the last log entries from all the APIServer2 Pods. 
For more control you can execute this to get all the available logs:
```
for pod in $(kubectl get pods -l app=apiserver2 -o name); do
  echo "=== Logs from $pod ==="
  kubectl logs $pod -c apiserver2 -n cppserver --timestamps=true
done
```

Check APIServer2 resource usage (cpu, memory):
```
kubectl top pods -n cppserver
```

## Step 4: Testing all APIs

APIServer2 container includes a set of sample APIs, and a bash script using CURL for unit-testing is also provided, just download it and you are ready to go.

Download from GitHub the latest version of `test.sh` script:
```
curl -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/unit-test/test.sh && chmod +x test.sh
```
Run the script, change the URL to your VM address if necessary. The /api prefix is required, otherwise the request is rejected by the Ingress, this is to protect the Pods against common HTTP attacks.

```
./test.sh https://localhost /api
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
This script is a simple but effective tool, it authenticates, then calls the secure APIs sending the resulting JWT token, it also calls diagnostic APIs using the pre-configured API Key defined in the YAML file, it is a tester that you can adapt to your own developments.

That's it, welcome to Kubernetes and high-performance light C++ containers, the easy way.

## Restarting APIServer

If you want to reconfigure APIServer you change the YAML file, then run:
```
kubectl apply -f apiserver2.yaml
```
Restart the containers:
```
kubectl rollout restart deployment
```
Expected output:
```
deployment.apps/apiserver2 restarted
```
After a few seconds the Pods will be renewed, the rules defined in the YAML file establish that service must not be interrupted, so while the new Pods get ready at least one of the old Pods keeps running until the new ones are ready to handle the load. Kubernetes takes care of this life-cycle issues, but enough resources must exist (CPU mostly) for this to happen, otherwise you will see some Pods in pending status, never starting. If the newewal of Pods went OK you will see fresh Pods running since a few seconds ago:
```
kubectl get pods
```
Expected output:
```
NAME                                    READY   STATUS    RESTARTS   AGE
apiserver2-6678566c86-52t9q   1/1     Running   0          9s
apiserver2-6678566c86-wzvhp   1/1     Running   0          12s
```

## Auto-scaling APIServer

The YAML file includes at the end a section to configure horizontal scaling, to run more Pods if the CPU load reaches some level:
```
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: apiserver2-hpa
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: apiserver2
  
  minReplicas: 2
  maxReplicas: 3
  
  metrics:
  - type: Resource
    resource:
      name: cpu
      target:
        type: Utilization
        averageUtilization: 80
```
In this case, if the CPU reaches 80% of utilization, a new Pod will be started running an APIServer2 container, when the CPU load goes down, the container will be removed.

You can monitor the HPA activity with this command:
```
kubectl get hpa
```
Expected output:
```
NAME            REFERENCE                         TARGETS       MINPODS   MAXPODS   REPLICAS   AGE
apiserver-hpa   Deployment/apiserver2             cpu: 3%/80%   2         3         2          21
```
If the CPU reaches the target 80% replicas will increase to 3, according to `maxReplicas` value. If you were using MicroK8s in a multi-node cluster (multiple VMs) the Pod may be created on any node, depending on the resources available.

## Checking the state of the whole MicroK8s cluster
```
kubectl get all --all-namespaces
```
Expected output:
```
NAMESPACE            NAME                                           READY   STATUS    RESTARTS   AGE
container-registry   pod/registry-579865c76c-gsx8z                  1/1     Running   0          9h
default              pod/apiserver2-7476ff954c-56bzh                1/1     Running   0          6h39m
default              pod/apiserver2-7476ff954c-qkj4t                1/1     Running   0          6h39m
ingress              pod/nginx-ingress-microk8s-controller-8vtr5    1/1     Running   0          9h
kube-system          pod/calico-kube-controllers-5947598c79-gw9nh   1/1     Running   0          9h
kube-system          pod/calico-node-gxj7p                          1/1     Running   0          9h
kube-system          pod/coredns-79b94494c7-lnx9g                   1/1     Running   0          9h
kube-system          pod/hostpath-provisioner-c778b7559-cjwbp       1/1     Running   0          9h
kube-system          pod/metrics-server-7dbd8b5cc9-wxvtf            1/1     Running   0          9h

NAMESPACE            NAME                         TYPE        CLUSTER-IP       EXTERNAL-IP   PORT(S)                  AGE
container-registry   service/registry             NodePort    10.152.183.231   <none>        5000:32000/TCP           9h
default              service/apiserver2-service   ClusterIP   10.152.183.92    <none>        8080/TCP                 9h
default              service/kubernetes           ClusterIP   10.152.183.1     <none>        443/TCP                  9h
kube-system          service/kube-dns             ClusterIP   10.152.183.10    <none>        53/UDP,53/TCP,9153/TCP   9h
kube-system          service/metrics-server       ClusterIP   10.152.183.230   <none>        443/TCP                  9h

NAMESPACE     NAME                                               DESIRED   CURRENT   READY   UP-TO-DATE   AVAILABLE   NODE SELECTOR            AGE
ingress       daemonset.apps/nginx-ingress-microk8s-controller   1         1         1       1            1           <none>                   9h
kube-system   daemonset.apps/calico-node                         1         1         1       1            1           kubernetes.io/os=linux   9h

NAMESPACE            NAME                                      READY   UP-TO-DATE   AVAILABLE   AGE
container-registry   deployment.apps/registry                  1/1     1            1           9h
default              deployment.apps/apiserver2                2/2     2            2           9h
kube-system          deployment.apps/calico-kube-controllers   1/1     1            1           9h
kube-system          deployment.apps/coredns                   1/1     1            1           9h
kube-system          deployment.apps/hostpath-provisioner      1/1     1            1           9h
kube-system          deployment.apps/metrics-server            1/1     1            1           9h

NAMESPACE            NAME                                                 DESIRED   CURRENT   READY   AGE
container-registry   replicaset.apps/registry-579865c76c                  1         1         1       9h
default              replicaset.apps/apiserver2-5dcd6864ff                0         0         0       9h
default              replicaset.apps/apiserver2-7476ff954c                2         2         2       6h39m
default              replicaset.apps/apiserver2-779f796c5                 0         0         0       7h30m
kube-system          replicaset.apps/calico-kube-controllers-5947598c79   1         1         1       9h
kube-system          replicaset.apps/coredns-79b94494c7                   1         1         1       9h
kube-system          replicaset.apps/hostpath-provisioner-c778b7559       1         1         1       9h
kube-system          replicaset.apps/metrics-server-7dbd8b5cc9            1         1         1       9h

NAMESPACE   NAME                                                 REFERENCE               TARGETS       MINPODS   MAXPODS   REPLICAS   AGE
default     horizontalpodautoscaler.autoscaling/apiserver2-hpa   Deployment/apiserver2   cpu: 0%/80%   2         3         2          9h
```

## Additional notes about this installation script

This installation script goes an extra-mile to save you manual configuration:

* Configures redirect from HTTP to HTTPS, this must be enabled in the Ingress ConfigMap and then configured in the APIServer2 ingress as a rule.
* Sets the timezone for the Ingress logs to the timezone of the host machine, the timezone for APIServer2 can be set in apiserver2.yaml, it is an environment variable `TZ`.
* It creates a local directory on the host VM `/mnt/apiserver-data`, in the most simple configuration (single-node) the uploaded blobs will be stored here, but this directory can be configured as a `mount point` using OS drivers to redirect I/O to other shared storage systems like NFS or S3, without changing `apiserver2.yaml` or APIServer2 code.
* It does install only the minimal set of MicroK8s add-ons: host-storage, ingress and metrics-server.
* You can enable add-ons for tasks like automating observability using Grafana Stack, but keep in mind of the extra CPU load these add-ons may demand. APIServer2 exposes the endpoints `/metrics` for JSON consumers and `/metricsp` for Prometheus, you can recollect metrics via HTTPS using an API-Key (configured as a secret in apiserver2.yaml) without installing an additional module in the MicroK8s cluster.


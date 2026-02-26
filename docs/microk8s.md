# Running APIServer2 with MicroK8s on Ubuntu 24.04

In this tutorial you will use APIServer2 docker image from DockerHub to build a single-node Kubernetes v1.35 cluster with 2 Pods running the container and an integrated Load Balancer (Traefik Ingress) that will listen on ports 80/443 of the host’s public IP. This setup is scalable up to 3 pods using a very modest VM (4 cores, 4GB RAM), with all the self-healing, auto-administration and scalability of MicroK8s Kubernets implementation, also Cloud-ready for serverless applications. In a few minutes you will have a complete Kubernetes system ready to roll. This APIServer2 image contains all the example APIs, requires the same [DemoDB SQL Server container](https://github.com/cppservergit/apiserver2/blob/main/docs/sqlserver.md) as the bare-metal tutorial, it assumes demodb.mshome.net as its default location, you can edit the APIServer2 YAML file to change the secrets and point to another database host. We recommend MicroK8s to deploy APIServer2 for OnPrem production, single-node or in high-availability (3+ VMs), depending on your resources.

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

**ADDED VALUE**: This script installs MicroK8s v1.35 and the required extensions (ingress, host storage, dns, etc) and deploys the APIServer2 containers using a higly optimized image based on Ubuntu 24.04 that it is as compact as images based on Alpine, without sacrificing performance or security. It does install Traefik Ingress via Helm charts, this way the latest stable version of Traefik will be installed, unlike when using the MicroK8s ingress addon. The installation takes about 1 minute.

This script can be configured to make Traefik trust the external Load Balancer IP (if there is any) in order to preserve the original client's IP:
```
# --- SCRIPT CONFIGURATION ---
HAPROXY_IP="172.22.127.161" # external HAProxy IP
```
If your MicroK8s nodes run behind an HAProxy load balancer you should set the variable shown above before running the script.

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

When the script ends you will see these messages at the end of the output:
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
microk8s (1.35/stable) v1.35.0 from Canonical✓ installed
[+] Waiting for MicroK8s to be ready...
[✓] MicroK8s base system installed.
[+] STEP 5: Installing traefik ingress...
[✓] traefik installed.
[+] STEP 6: Deploying APIserver2...
[✓] APIServer2 deployment is ready.
[+] STEP 7: Finalizing configuration...
[✓] MicroK8s/APIServer2 setup completed in 0m 54s.

[+] EXTRA step: Waiting for all Pods to be running - it may take a few minutes...
[✓] Pods are ready.

 →→ Please LOG OUT AND LOG BACK IN for group changes to take effect. ←←
```
Exit the shell and enter again.

## Step 3: Test the installation

List APIServer2 pods
```
kubectl get pods -n cppserver
```
Expected output:
```
NAME                          READY   STATUS    RESTARTS   AGE
apiserver2-577858686f-44l2j   1/1     Running   0          2m30s
apiserver2-577858686f-wqt4s   1/1     Running   0          2m30s
```

Check the Ingress (load balancer):
```
kubectl get all -n ingress
```
Expected output (pod, service and daemonset should be listed):
```
NAME                READY   STATUS    RESTARTS   AGE
pod/traefik-hbpdt   1/1     Running   0          9h

NAME              TYPE        CLUSTER-IP      EXTERNAL-IP   PORT(S)          AGE
service/traefik   ClusterIP   10.152.183.23   <none>        80/TCP,443/TCP   9h

NAME                     DESIRED   CURRENT   READY   UP-TO-DATE   AVAILABLE   NODE SELECTOR   AGE
daemonset.apps/traefik   1         1         1       1            1           <none>          9h
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

Get APIServer2 version:
```
curl https://localhost/api/version -ks -H "Authorization: Bearer 6976f434-d9c1-11f0-93b8-5254000f64af" | jq
```
Expected output (version may vary):
```
{
  "version": "1.1.9"
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

Test the upload API:
```
TOKEN=$(curl --json '{"username":"mcordova", "password":"basica"}' "https://localhost/api/login" -ks | jq -r '.id_token')
curl https://localhost/api/upload -F "file1=@apiserver2.yaml" -F "title=My little YAML K8s deployment" -ks -H "Authorization: Bearer $TOKEN" | jq
```
Expected output (token will vary):
```
{
  "originalFilename": "apiserver2.yaml",
  "savedFilename": "4dde3f25-7d5a-4eb8-8279-d5e3d14ee74c.yaml",
  "size": "8849",
  "title": "My little YAML K8s deployment"
}
```
To list the uploaded files:
```
ls /mnt/apiserver-data
```

Check the Traefik Ingress (load balancer) logs:
```
microk8s kubectl logs -n ingress daemonset/traefik
```
By default Traefik ingress does not log HTTP requests, the external Load Balancer at the edge (HAProxy) will keep those logs.

APIServer2 Pods logs with server-generated timestamps:
```

```
Expected output:
```
2026-02-25T11:57:07.496262324-04:00 {"level": "INFO", "thread": "131551659783488", "req_id": "--------", "msg": "Application starting..."}
2026-02-25T11:57:07.500023678-04:00 {"level": "INFO", "thread": "131551659783488", "req_id": "--------", "msg": "CORS enabled for 2 origin(s)."}
2026-02-25T11:57:07.500031478-04:00 {"level": "INFO", "thread": "131551659783488", "req_id": "--------", "msg": "APIServer2 version 1.1.9 starting on port 8080 with 1 I/O threads and 4 total worker threads."}
2026-02-25T11:57:07.500033678-04:00 {"level": "INFO", "thread": "131551659783488", "req_id": "--------", "msg": "Assigning 4 worker threads per I/O worker."}
2026-02-25T11:57:00.122417250-04:00 {"level": "INFO", "thread": "126575868577088", "req_id": "--------", "msg": "Application starting..."}
2026-02-25T11:57:00.127389422-04:00 {"level": "INFO", "thread": "126575868577088", "req_id": "--------", "msg": "CORS enabled for 2 origin(s)."}
2026-02-25T11:57:00.127398322-04:00 {"level": "INFO", "thread": "126575868577088", "req_id": "--------", "msg": "APIServer2 version 1.1.9 starting on port 8080 with 1 I/O threads and 4 total worker threads."}
2026-02-25T11:57:00.127400622-04:00 {"level": "INFO", "thread": "126575868577088", "req_id": "--------", "msg": "Assigning 4 worker threads per I/O worker."}
```
APIServer2 logs are in JSON format for cloud-native observability stacks, like Grafana Loki.
The command above consolidates the last log entries from all the APIServer2 Pods. 

For more control you can execute this to get all the available logs:
```
for pod in $(kubectl get pods -l app=apiserver2 -n cppserver -o name); do
  echo "=== Logs from $pod ==="
  kubectl logs $pod -c apiserver2 -n cppserver --timestamps=true
done
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
This script is a simple but effective tool, it authenticates, then calls the secure APIs sending the resulting JWT token, it also calls diagnostic APIs using the pre-configured API Key defined as a secret in the apiserver2 YAML file, it is a tester that you can adapt to your own developments.

That's it, welcome to Kubernetes and high-performance light C++ containers, the easy way.

## Restarting APIServer

If you want to reconfigure APIServer you change the YAML file, then run:
```
kubectl apply -f apiserver2.yaml
```
Restart the containers:
```
kubectl rollout restart deployment -n cppserver
```
Expected output:
```
deployment.apps/apiserver2 restarted
```
After a few seconds the Pods will be renewed, the rules defined in the YAML file establish that service must not be interrupted, so while the new Pods get ready at least one of the old Pods keeps running until the new ones are ready to handle the load. Kubernetes takes care of this life-cycle issues, but enough resources must exist (CPU mostly) for this to happen, otherwise you will see some Pods in pending status, never starting. If the newewal of Pods went OK you will see fresh Pods running since a few seconds ago:
```
kubectl get pods -n cppserver
```
Expected output:
```
NAME                                    READY   STATUS    RESTARTS   AGE
apiserver2-6678566c86-52t9q   1/1     Running   0          9s
apiserver2-6678566c86-wzvhp   1/1     Running   0          12s
```

## Scaling APIServer

Your 4-cores VM can run comfortably up to 3 pods, by default it starts up with 2 pods, enough power, but if you need more you can scale it:
```
kubectl scale deployment apiserver2 --replicas=3 -n cppserver
```
Expected output:
```
deployment.apps/apiserver2 scaled
```
Try again:
```
kubectl get pods -n cppserver
```
You should see 3 pods now in running state.


## Checking the state of the whole MicroK8s cluster
```
kubectl get all -A
```
All pods should be running The states `Current` and `Desired` should match.
Expected output:
```
NAMESPACE     NAME                                           READY   STATUS    RESTARTS   AGE
cppserver     pod/apiserver2-87c46d5c-mwgj5                  1/1     Running   0          17m
cppserver     pod/apiserver2-87c46d5c-swfzx                  1/1     Running   0          7m32s
cppserver     pod/apiserver2-87c46d5c-sx8bq                  1/1     Running   0          17m
ingress       pod/traefik-hbpdt                              1/1     Running   0          11h
kube-system   pod/calico-kube-controllers-676854dc5d-dkggx   1/1     Running   0          11h
kube-system   pod/calico-node-54pjg                          1/1     Running   0          11h
kube-system   pod/coredns-78894c95f4-t42fz                   1/1     Running   0          11h
kube-system   pod/hostpath-provisioner-7f665d64f6-tpn9d      1/1     Running   0          11h

NAMESPACE     NAME                         TYPE        CLUSTER-IP       EXTERNAL-IP   PORT(S)                  AGE
cppserver     service/apiserver2-service   ClusterIP   10.152.183.197   <none>        8080/TCP                 11h
default       service/kubernetes           ClusterIP   10.152.183.1     <none>        443/TCP                  11h
ingress       service/traefik              ClusterIP   10.152.183.23    <none>        80/TCP,443/TCP           11h
kube-system   service/kube-dns             ClusterIP   10.152.183.10    <none>        53/UDP,53/TCP,9153/TCP   11h

NAMESPACE     NAME                         DESIRED   CURRENT   READY   UP-TO-DATE   AVAILABLE   NODE SELECTOR            AGE
ingress       daemonset.apps/traefik       1         1         1       1            1           <none>                   11h
kube-system   daemonset.apps/calico-node   1         1         1       1            1           kubernetes.io/os=linux   11h

NAMESPACE     NAME                                      READY   UP-TO-DATE   AVAILABLE   AGE
cppserver     deployment.apps/apiserver2                3/3     3            3           11h
kube-system   deployment.apps/calico-kube-controllers   1/1     1            1           11h
kube-system   deployment.apps/coredns                   1/1     1            1           11h
kube-system   deployment.apps/hostpath-provisioner      1/1     1            1           11h

NAMESPACE     NAME                                                 DESIRED   CURRENT   READY   AGE
cppserver     replicaset.apps/apiserver2-5f7748d9cd                0         0         0       11h
cppserver     replicaset.apps/apiserver2-87c46d5c                  3         3         3       17m
kube-system   replicaset.apps/calico-kube-controllers-676854dc5d   1         1         1       11h
kube-system   replicaset.apps/coredns-78894c95f4                   1         1         1       11h
kube-system   replicaset.apps/hostpath-provisioner-7f665d64f6      1         1         1       11h
```

## Additional notes about this installation script

This installation script goes an extra-mile to save you manual configuration:

* Installs `--channel=1.35/stable` which includes the new ingress Traefik in its latest stable version. The old Nginx ingress will be discontinued in March 2026.
* MicroK8s protects secrets with encryption, also complies with most CIS hardening rules.
* APIServer2 deployment passes Trivy tests (security scanner) with 1 false possitive warning (docker hub not trusted).
* APIServer2 production-grade OCI image based on Ubuntu 24.04 chisel, size-optimized and passes Trivy tests without warnings, no vulnerabilities.
* Configures redirect from HTTP to HTTPS.
* Ingress blocks requests to APIServer2 that do not start with `/api/` protecting APIServer2 against common HTTP attacks.
* The timezone for APIServer2 can be set in apiserver2.yaml, it is an environment variable `TZ`.
* It creates a local directory on the host VM `/mnt/apiserver-data`, in the most simple configuration (single-node) the uploaded blobs will be stored here, but this directory can be configured as a `mount point` using OS drivers to redirect I/O to other shared storage systems like NFS or MinIO S3, without changing `apiserver2.yaml` or APIServer2 code.
* It does install only the minimal set of MicroK8s add-ons: host-storage, dns and rbac, very fast installation from scratch.
* You can enable add-ons for tasks like automating observability using Grafana Stack, but keep in mind of the extra CPU load these add-ons may demand. APIServer2 exposes the endpoints `/metrics` for JSON consumers and `/metricsp` for Prometheus, you can recollect metrics via HTTPS using an API-Key (configured as a secret in apiserver2.yaml) without installing an additional module in the MicroK8s cluster.

## Production cluster with HAProxy on the TLS edge

It is possible to deploy a high-availability cluster with little resources, combining HAProxy load balancer with a two independent single-node MicroK8s clusters, it is not high-availability in the sense of traditional kubernetes, which may be overkill for OnPrem (with Microk8s you need 3 VMs for High-Availability of the control plane), but this is very pragmatic and can easily manage the load for small-medium Banks and other organizations/institutions, running the middleware API for external mobile Apps or serving inter-organization APIs.

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

A multi-layer security approach is implemented in this setup, HAProxy provides rate-limits, JA4 fingerprints blacklist and API rules enforcement, transforming the load balancer into an API Gateway, protecting the ingress and the pods. The ingress implements path rules, nothing else, and the APIServer2 container implements the stateless JWT security model.

Please check our [recipe](https://github.com/cppservergit/apiserver2/blob/main/docs/haproxy-cluster.md) for installing such a cluster.

## Upgrading Traefik
Installing Traefil via helm charts will usually install the latest stable version most of the time, but it may happen that the maintainers of the Traefik Helm chart haven't yet released a new Chart version that points to the very latest stable App version as the default.

If you detect such a case in Traefik ingress logs, that there is a new version available, a message like this: `2026-02-25T04:53:30Z WRN A new release of Traefik has been found: 3.6.9. Please consider updating.` and you decide to update it, this is the procedure, please note that there will be a brief interruption of the Ingress service in the node.

How to check your current Traefik version:
```
kubectl exec -n ingress daemonset/traefik -- traefik version
```
Expected output:
```
Version:      3.6.8
Codename:     ramequin
Go version:   go1.25.7
Built:        2026-02-11T16:35:39Z
OS/Arch:      linux/amd64
```
Update the traefik daemonset:
```
kubectl set image daemonset/traefik traefik=traefik:v3.6.9 -n ingress
kubectl delete pod -n ingress -l app.kubernetes.io/name=traefik --field-selector=status.phase=Running
kubectl rollout status daemonset/traefik -n ingress
```
It may take a minute while the new ingress pod is in `running` state.

Check the Traefik version again:
```
kubectl exec -n ingress daemonset/traefik -- traefik version
```
Expected output:
```
Version:      3.6.9
Codename:     ramequin
Go version:   go1.25.7
Built:        2026-02-23T17:12:19Z
OS/Arch:      linux/amd64
```

## Uninstalling MicroK8s

If your MicroK8s becomes unstable and for some reason it is hard to diagnose and fix it, do not waste time, uninstall and run again `setup.sh`, it takes about 5 minutes or less to complete.
```
sudo snap remove --purge microk8s
```
During development and testing when using Multipass on Windows 10/11, after a reboot of the host PC the Multipass VM may change its IP and then MicroK8s will start failing, this is such a case that justifies uninstalling and running `setup.sh` again, it will be faster that trying to fix MicroK8s configuration.

## Tuning container runtime

MicroK8s uses containerd/runc to manage and run your containers, you can replace runc and use CRun, an OCI container runtime written in C which is smaller and faster, it will optimize the container launching phase, please check our [recipe](https://github.com/cppservergit/apiserver2/blob/main/docs/crun.md) for CRun with MicroK8s and APIServer2.
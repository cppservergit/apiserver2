# Running APIServer2 with MicroK8s on Ubuntu 24.04

In this tutorial you will use APIServer2 docker image straight from DockerHub to build a single-node Kubernetes cluster with 2 Pods running the container and an integrated Load Balancer (Nginx Ingress), this is equivalent to our LXD setup, but more dynamic in nature and more Cloud-ready for serverless applications. In a few minutes you will have a complete Kubernetes system ready to roll. This APIServer2 image contains all the example APIs.

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
Retrieving APIserver2 deployment manifest...
Deploying APIserver2...
secret/apiserver-secrets created
persistentvolumeclaim/apiserver-pvc created
deployment.apps/apiserver-deployment created
service/apiserver-service created
ingress.networking.k8s.io/apiserver-ingress created
horizontalpodautoscaler.autoscaling/apiserver-hpa created
[+] Waiting for ingress controller pod...
pod/nginx-ingress-microk8s-controller-h9h6r condition met
[+] Testing HTTP connectivity...
[✓] Ingress is serving HTTP traffic at http://172.22.15.155/
[+] Testing HTTPS connectivity...
[✓] Ingress is serving HTTPS traffic at https://172.22.15.155/
Adding current user to microk8s group...
Setting up kubectl alias...
MicroK8s setup completed. Please log out and log back in for group changes to take effect.
```
Exit the shell and enter again.

## Step 3: Test the installation
```
microk8s status
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
  disabled:
    cert-manager         # (core) Cloud native certificate management
    cis-hardening        # (core) Apply CIS K8s hardening
    community            # (core) The community addons repository
    dashboard            # (core) The Kubernetes dashboard
    gpu                  # (core) Alias to nvidia add-on
    host-access          # (core) Allow Pods connecting to Host services smoothly
    kube-ovn             # (core) An advanced network fabric for Kubernetes
    mayastor             # (core) OpenEBS MayaStor
    metallb              # (core) Loadbalancer for your Kubernetes cluster
    minio                # (core) MinIO object storage
    nvidia               # (core) NVIDIA hardware (GPU and network) support
    observability        # (core) A lightweight observability stack for logs, traces and metrics
    prometheus           # (core) Prometheus operator for monitoring and logging
    rbac                 # (core) Role-Based Access Control for authorisation
    rook-ceph            # (core) Distributed Ceph storage using Rook
```

Get the Kubernetes version
```
kubectl version
```
Expected output (versions may vary):
```
Client Version: v1.32.9
Kustomize Version: v5.5.0
Server Version: v1.32.9
```

List APIServer2 pods
```
kubectl get pods -o wide
```
Expected output:
```
NAME                                    READY   STATUS    RESTARTS   AGE     IP             NODE   NOMINATED NODE   READINESS GATES
apiserver-deployment-784bcb5449-6cs97   1/1     Running   0          5m12s   10.1.215.200   mk8s   <none>           <none>
apiserver-deployment-784bcb5449-mmctd   1/1     Running   0          5m12s   10.1.215.201   mk8s   <none>           <none>
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

Invoke the `login` API to force a test of the connection to the database:
```
curl --json '{"username":"mcordova", "password":"basica"}' https://localhost/api/login -s -k | jq
```
Expected output (token will vary):
```
{
  "displayname": "Martín Córdova",
  "id_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6Im1hcnRpbi5jb3Jkb3ZhQGdtYWlsLmNvbSIsImV4cCI6IjE3NjczMjIyNDMiLCJpYXQiOiIxNzY3MzIxOTQzIiwicm9sZXMiOiJzeXNhZG1pbiwgY2FuX2RlbGV0ZSwgY2FuX3VwZGF0ZSIsInNlc3Npb25JZCI6ImQ1MzYyNzk0LTI5NWEtNDQyNS1iMDUzLTA1YzgyZThhODhhNCIsInVzZXIiOiJtY29yZG92YSJ9.ewQIim-PBxwoG7sED4l0i1NuzBuMr5Uwg1D_oYifvW0",
  "token_type": "bearer"
}
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

Check APIServer2 resource usage (cpu, memory):
```
kubectl top pods
```

## Step 4: Testing all APIs

APIServer2 container includes a set of sample APIs, and a bash script using CURL for unit-testing is also provided, just download it and you are ready to go.

Download from GitHub the latest version of `test.sh` script:
```
curl -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/unit-test/test.sh && chmod +x test.sh
```
Run the script, change the URL to your VM address if necessary. The /api prefix is required, otherwise the request is rejected by the Ingress, this is to protect the Pods against common HTTP attacks.

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
This script is a simple but effective tool, it authenticates, then calls the secure APIs sending the resulting JWT token, it also calls diagnostic APIs using the pre-configured API Key defined in the YAML file, it is a tester that you can adapt to your own developments.

That's it, welcome to Kubernetes and high-performance light C++ containers, the easy way.

## Restarting APIServer

If you want to reconfigure APIServer you change the YAML file, then run:
```
kubectl apply -f deploy-apiserver.yaml
```
But that is not enough, to restart the container and read the new values from the environment you must restart-rollout the Pods:
```
kubectl rollout restart deployment
```
Expected output:
```
deployment.apps/apiserver-deployment restarted
```
After a few seconds the Pods will be renewed, the rules defined in the YAML file establish that service must not be interrupted, so while the new Pods get ready at least one of the old Pods keeps running until the new ones are ready to handle the load. Kubernetes takes care of this life-cycle issues, but enough resources must exist (CPU mostly) for this to happen, otherwise you will see some Pods in pending status, never starting. If the newewal of Pods went OK you will see fresh Pods running since a few seconds ago:
```
kubectl get pods
```
Expected output:
```
NAME                                    READY   STATUS    RESTARTS   AGE
apiserver-deployment-6678566c86-52t9q   1/1     Running   0          9s
apiserver-deployment-6678566c86-wzvhp   1/1     Running   0          12s
```

## Auto-scaling APIServer

The YAML file includes at the end a section to configure horizontal scaling, to run more Pods if the CPU load reaches some level:
```
apiVersion: autoscaling/v2
kind: HorizontalPodAutoscaler
metadata:
  name: apiserver-hpa
spec:
  scaleTargetRef:
    apiVersion: apps/v1
    kind: Deployment
    name: apiserver-deployment
  
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
apiserver-hpa   Deployment/apiserver-deployment   cpu: 3%/80%   2         3         2          21
```
If the CPU reaches the target 80% replicas will increase to 3, according to `maxReplicas` value. If you were using MicroK8s in a multi-node cluster (multiple VMs) the Pod may be created on any node, depending on the resources available.

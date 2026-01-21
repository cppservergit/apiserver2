# Running APIServer2 with K3s on Ubuntu 24.04

You can deploy APIServer2 with equivalent features as in MicroK8s, using [K3S](https://k3s.io/), another lightweight Kubernetes implementation.

Create a minimal VM with Multipass:
```
multipass launch -n k3s -c 4 -m 4G -d 6g
```
Enter the Linux terminal:
```
multipass shell k3s
```
Update OS and create host directory for blobs:
```
sudo mkdir -p /mnt/apiserver-data
sudo chown 10001:10001 /mnt/apiserver-data
sudo apt update
sudo apt upgrade -y
```

Install K3S on Ubuntu 24.04
```
curl -sfL https://get.k3s.io | K3S_KUBECONFIG_MODE="644" sh -
```
This will take about 2-5 minutes to complete.
Expected output:
```
[INFO]  Finding release for channel stable
[INFO]  Using v1.34.3+k3s1 as release
[INFO]  Downloading hash https://github.com/k3s-io/k3s/releases/download/v1.34.3+k3s1/sha256sum-amd64.txt
[INFO]  Downloading binary https://github.com/k3s-io/k3s/releases/download/v1.34.3+k3s1/k3s
[INFO]  Verifying binary download
[INFO]  Installing k3s to /usr/local/bin/k3s
[INFO]  Skipping installation of SELinux RPM
[INFO]  Creating /usr/local/bin/kubectl symlink to k3s
[INFO]  Creating /usr/local/bin/crictl symlink to k3s
[INFO]  Creating /usr/local/bin/ctr symlink to k3s
[INFO]  Creating killall script /usr/local/bin/k3s-killall.sh
[INFO]  Creating uninstall script /usr/local/bin/k3s-uninstall.sh
[INFO]  env: Creating environment file /etc/systemd/system/k3s.service.env
[INFO]  systemd: Creating service file /etc/systemd/system/k3s.service
[INFO]  systemd: Enabling k3s unit
Created symlink /etc/systemd/system/multi-user.target.wants/k3s.service → /etc/systemd/system/k3s.service.
[INFO]  systemd: Starting k3s
```

Test K3S installation:
```
kubectl version
```
Expected output (versions may vary)
```
Client Version: v1.34.3+k3s1
Kustomize Version: v5.7.1
Server Version: v1.34.3+k3s1
```
SHow node details:
```
kubectl get node -o wide
```
Expected output (versions may vary)
```
NAME   STATUS   ROLES           AGE     VERSION        INTERNAL-IP      EXTERNAL-IP   OS-IMAGE             KERNEL-VERSION     CONTAINER-RUNTIME
k3s    Ready    control-plane   2m40s   v1.34.3+k3s1   172.30.138.131   <none>        Ubuntu 24.04.3 LTS   6.8.0-90-generic   containerd://2.1.5-k3s1
```
Test Ingress (load balancer):
```
kubectl rollout status deployment/traefik -n kube-system 
```
Expected output:
```
deployment "traefik" successfully rolled out
```

Download APIServer2 YAML:
```
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/apiserver2.yaml
```

Deploy APIServer2:
```
kubectl create namespace cppserver
kubectl label --overwrite ns cppserver pod-security.kubernetes.io/enforce=restricted
kubectl apply -f apiserver2.yaml
```
Expected output:
```
namespace/cppserver created
namespace/cppserver labeled
secret/apiserver2-secrets created
persistentvolume/apiserver2-pv created
persistentvolumeclaim/apiserver2-pvc created
deployment.apps/apiserver2 created
service/apiserver2-service created
middleware.traefik.io/apiserver2-strip created
middleware.traefik.io/apiserver2-limits created
middleware.traefik.io/apiserver2-redirect created
ingressroute.traefik.io/apiserver2-ingress-http created
ingressroute.traefik.io/apiserver2-ingress created
horizontalpodautoscaler.autoscaling/apiserver2-hpa created
networkpolicy.networking.k8s.io/default-deny-ingress created
networkpolicy.networking.k8s.io/allow-apiserver-ingress created
```

Wait until APIServer2 gets fully deployed on K3S:
```
kubectl rollout status deployment/apiserver2 -n cppserver --timeout=300s
```
Expectd output:
```
deployment "apiserver2" successfully rolled out
```

List the state of APIServer2 deployment:
```
kubectl get all -n cppserver
```
Expected output:
```
NAME                              READY   STATUS    RESTARTS   AGE
pod/apiserver2-6864f6497b-6xzkd   1/1     Running   0          95s
pod/apiserver2-6864f6497b-flmxj   1/1     Running   0          95s

NAME                         TYPE        CLUSTER-IP      EXTERNAL-IP   PORT(S)    AGE
service/apiserver2-service   ClusterIP   10.43.125.143   <none>        8080/TCP   95s

NAME                         READY   UP-TO-DATE   AVAILABLE   AGE
deployment.apps/apiserver2   2/2     2            2           95s

NAME                                    DESIRED   CURRENT   READY   AGE
replicaset.apps/apiserver2-6864f6497b   2         2         2       95s

NAME                                                 REFERENCE               TARGETS       MINPODS   MAXPODS   REPLICAS   AGE
horizontalpodautoscaler.autoscaling/apiserver2-hpa   Deployment/apiserver2   cpu: 0%/80%   2         3         2          95s
```

Test APIServer2:
```
curl https://localhost/api/ping -ks | jq
```
Expected output:
```
{
  "status": "OK"
}
```
Test `/login` API with database access:
```
curl --json '{"username":"mcordova", "password":"basica"}' https://localhost/api/login -ks | jq
```
Expected output:
```
{
  "displayname": "Martín Córdova",
  "id_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6Im1hcnRpbi5jb3Jkb3ZhQGdtYWlsLmNvbSIsImV4cCI6IjE3Njg5NTk3MDQiLCJpYXQiOiIxNzY4OTU5NDA0Iiwicm9sZXMiOiJzeXNhZG1pbiwgY2FuX2RlbGV0ZSwgY2FuX3VwZGF0ZSIsInNlc3Npb25JZCI6IjBmNjljMDNkLTk5N2MtNGFiMi1hNzYzLTFjZDFiYzFlZDBiZSIsInVzZXIiOiJtY29yZG92YSJ9.t2GJhBkiZXpR3NNE9jGXujEyISfan3moy-LJiJ571b8",
  "token_type": "bearer"
}
```

That's it, now you have another lightweight option to deploy APIServer2 container on Kubernetes.

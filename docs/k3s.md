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
sudo chown 1000:1000 /mnt/apiserver-data
sudo apt update
```

Install K3S on Ubuntu 24.04
```
curl -sfL https://get.k3s.io | K3S_KUBECONFIG_MODE="644" sh -
```
This will take about 1-2 minutes to complete.

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

Download APIServer2 YAML for K3S:
```
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/k3s/apiserver2.yaml
```

Deploy APIServer2:
```
kubectl apply -f apiserver2.yaml
```

Wait until APIServer2 gets fully deployed on K3S:
```
kubectl rollout status deployment/apiserver2 --timeout=300s
```

Test APIServer2:
```
curl https://localhost/api/metrics -ks -H "x-api-key: 6976f434-d9c1-11f0-93b8-5254000f64af" | jq
```
```
curl https://localhost/api/version -ks -H "x-api-key: 6976f434-d9c1-11f0-93b8-5254000f64af" | jq
```
```
curl --json '{"username":"mcordova", "password":"basica"}' https://localhost/api/login -ks | jq
```
```
TOKEN=$(curl --json '{"username":"mcordova", "password":"basica"}' "https://localhost/api/login" -ks | jq -r '.id_token')
curl "https://localhost/api/customer" -ks --json '{"id":"ANATR"}' -H "Authorization: Bearer $TOKEN" | jq
```

That's it, now you have another option to deploy APIServer2 container on Kubernetes.

# Using CRun as the container runtime with MicroK8s

`CRun` is a replacement for the default MicroK8s OCI container runtime `Runc`, which is written in Go, `CRun` is written in C, it is much smaller and faster than Runc, in case you want to optimize the operation of your MicroK8s node regarding to the launching of containers, which is the specific task CRun/Runc manage, these are the steps, it is assumed that a working MicroK8s installation is in working condition.

The [CRun Project on Github](https://github.com/containers/crun)

## Step 1: Install CRun

```
# Create the binary directory
sudo mkdir -p /var/snap/microk8s/common/bin

# Download a stable static crun release
wget https://github.com/containers/crun/releases/download/1.26/crun-1.26-linux-amd64 -O crun-static
sudo chmod +x crun-static
sudo mv crun-static /var/snap/microk8s/common/bin/crun
```

## Step 2: Edit MicroK8s containerd configuration
```
sudo nano /var/snap/microk8s/current/args/containerd-template.toml
```

Add this block at the end of the `[plugins."io.containerd.grpc.v1.cri".containerd.runtimes]` section:
```
   [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.crun]
      runtime_type = "io.containerd.runc.v2"
      [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.crun.options]
         BinaryName = "/var/snap/microk8s/common/bin/crun"
```

The end of your `containerd.runtimes` block should look like this:
```
   [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.kata]
      runtime_type = "io.containerd.kata.v2"
      [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.kata.options]
        BinaryName = "kata-runtime"

   [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.crun]
      runtime_type = "io.containerd.runc.v2"
      [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.crun.options]
         BinaryName = "/var/snap/microk8s/common/bin/crun"
```
With the `CRun` runtime plugin defined right after the last one, the `kata` plugin.

## Step 3: Define the new runtime class in MicroK8s
```
cat <<EOF > runtime.yaml
apiVersion: node.k8s.io/v1
kind: RuntimeClass
metadata:
  name: crun
handler: crun
EOF
```
Execute:
```
kubectl apply -f runtime.yaml
```

## Step 4: Update APIServer2 deployment
Let's assume you have in your home directory the file `apiserver2.yaml` that was used to deploy APIServer2 on MicroK8s, as usual when using our installation [procedure](https://github.com/cppservergit/apiserver2/blob/main/docs/microk8s.md).

```
nano apiserver2.yaml
```

Search for the deployment section:
```
# ==============================================================================
# 3. DEPLOYMENT
# ==============================================================================
apiVersion: apps/v1
kind: Deployment
```

Search for `template` section:
```
  template:
    metadata:
      labels:
        app: apiserver2
      annotations:
        # --- PROMETHEUS DISCOVERY ---
        prometheus.io/scrape: "true"
        prometheus.io/port: "8080"
        prometheus.io/path: "/metricsp"
    spec:
```
Right under `spec:` add this element, it should look like this:
```
    spec:
      runtimeClassName: crun
```
Save and exit.

## Step 5: Restart MicroK8s and redeploy APIServer2
```
microk8s stop
microk8s start
```
Redeploy:
```
kubectl apply -f apiserver2.yaml
kubectl rollout restart deployment -n cppserver
```
That's it, from now on `containerd` will use `crun` to launch APIServer2 containers, instead of `runc`.

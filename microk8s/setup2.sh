#!/bin/bash
clear

# --- SCRIPT CONFIGURATION ---
HAPROXY_IP="172.22.127.161" # external HAProxy IP
SECRETKEY=$(openssl rand -base64 32) # for encrypted secrets

echo "----------------------------------------------------------------"
echo " Express MicroK8s single-node setup for APIServer2"
echo "----------------------------------------------------------------"

# Start timer
START_TIME=$(date +%s)

# --- COLORS ---
BLUE='\033[94m'
RESET='\033[0m'
YELLOW='\033[33m'
BLINK='\033[5m'

echo "[+] STEP 1: Mounting upload directory..."
sudo mkdir -p /mnt/apiserver-data
sudo chown 10001:10001 /mnt/apiserver-data
echo -e "${BLUE}[✓] mount directory ready${RESET}"

echo "[+] STEP 2: Tuning sysctl..."
cat <<EOF | sudo tee /etc/sysctl.d/99-microk8s-highperf.conf > /dev/null
net.ipv4.ip_local_port_range = 10240 65535
net.ipv4.tcp_tw_reuse = 1
net.ipv4.tcp_fin_timeout = 15
vm.panic_on_oom=0
vm.overcommit_memory=1
kernel.panic=10
kernel.panic_on_oops=1
kernel.keys.root_maxkeys=1000000
kernel.keys.root_maxbytes=25000000
EOF
sudo sysctl --system >/dev/null
echo -e "${BLUE}[✓] sysctl updated${RESET}"

echo "[+] STEP 3: Creating MicroK8s Launch configuration..."
cat <<EOF > microk8s-config.yaml
---
version: 0.1.0
addons:
  - name: dns
  - name: hostpath-storage

extraKubeAPIServerArgs:
  --encryption-provider-config: "\$SNAP_DATA/args/encryption-config.yaml"

extraConfigFiles:
  encryption-config.yaml: |
    apiVersion: apiserver.config.k8s.io/v1
    kind: EncryptionConfiguration
    resources:
      - resources: ["secrets", "configmaps"]
        providers:
          - aescbc:
              keys:
                - name: key1
                  secret: $SECRETKEY
          - identity: {}
EOF

sudo mkdir -p /var/snap/microk8s/common/
sudo cp microk8s-config.yaml /var/snap/microk8s/common/.microk8s.yaml
sudo rm microk8s-config.yaml
echo -e "${BLUE}[✓] Launch configuration installed.${RESET}"

echo "[+] STEP 4: Installing MicroK8s..."
sudo snap install microk8s --classic --channel=1.35/stable
echo "[+] Waiting for MicroK8s to be ready..."
sudo microk8s status --wait-ready >/dev/null
echo -e "${BLUE}[✓] MicroK8s base system installed.${RESET}"

echo "[+] STEP 5: Installing traefik ingress..."
cat <<EOF > traefik-values.yaml
deployment:
  kind: DaemonSet
ports:
  web:
    port: 8000
    hostPort: 80
  websecure:
    port: 8443
    hostPort: 443
service:
  type: ClusterIP
additionalArguments:
  - "--entryPoints.web.forwardedHeaders.trustedIPs=$HAPROXY_IP"
  - "--entryPoints.websecure.forwardedHeaders.trustedIPs=$HAPROXY_IP"
providers:
  kubernetesCRD:
    allowEmptyServices: true
  kubernetesIngress:
    allowEmptyServices: true
EOF

sudo microk8s helm repo add traefik https://traefik.github.io/charts >/dev/null
sudo microk8s helm repo update >/dev/null
sudo microk8s kubectl create namespace ingress >/dev/null
sudo microk8s helm install traefik traefik/traefik --namespace ingress --values traefik-values.yaml >/dev/null
rm traefik-values.yaml
echo -e "${BLUE}[✓] traefik installed.${RESET}"

echo "[+] STEP 6: Deploying APIserver2..."
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/apiserver2.yaml
sudo microk8s kubectl create namespace cppserver > /dev/null
sudo microk8s kubectl label --overwrite ns cppserver pod-security.kubernetes.io/enforce=restricted > /dev/null
sudo microk8s kubectl apply -f apiserver2.yaml > /dev/null
echo -e "${BLUE}[✓] APIServer2 deployment is ready.${RESET}"

echo "[+] STEP 7: Finalizing configuration..."
sudo usermod -a -G microk8s $USER && mkdir -p ~/.kube && chmod 0700 ~/.kube
echo "alias kubectl='microk8s kubectl'" >> ~/.bash_aliases

# Calculate duration
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))
MINUTES=$((DURATION / 60))
SECONDS=$((DURATION % 60))

echo -e "${BLUE}[✓] MicroK8s/APIServer2 setup completed in ${MINUTES}m ${SECONDS}s.${RESET}"

echo ""
echo "[+] STEP EXTRA: Waiting for all Pods to be running - it may take a few minutes..."
sudo microk8s kubectl wait --for=condition=Ready pods --all --all-namespaces --timeout=600s >/dev/null
echo "[✓] Pods are ready."
echo  ""
sudo microk8s kubectl get all -A
echo -e "${YELLOW}${BLINK} →→ Please LOG OUT AND LOG BACK IN for group changes to take effect. ←←${RESET}"

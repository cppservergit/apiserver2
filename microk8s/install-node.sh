#!/bin/bash
clear

# --- COLORS ---
BLUE='\033[94m'
RESET='\033[0m'
YELLOW='\033[33m'
BLINK='\033[5m'

# --- SCRIPT CONFIGURATION ---
# Dynamically extract the IP from eth1 (the bridged interface)
NODE_IP=$(ip -4 addr show eth1 | grep -oP '(?<=inet\s)\d+(\.\d+){3}')

# If extraction fails, exit to prevent a broken installation
if [ -z "$NODE_IP" ]; then
    echo -e "${YELLOW}[!] ERROR: Could not find an IP on eth1. Check your bridge.${RESET}"
    exit 1
fi

HAPROXY_IP="192.168.0.200"        # Target fixed IP for the load balancer
SKEY=$(openssl rand -base64 32)    # Encryption key for secrets

echo "---------------------------------------------------------------------------------"
echo -e "Starting Immortal Single-Node Setup on ${BLUE}$NODE_IP${RESET}"
echo "---------------------------------------------------------------------------------"

echo "[+] STEP 1: Mounting upload directory..."
sudo mkdir -p /mnt/apiserver-data
sudo chown 10001:10001 /mnt/apiserver-data
echo -e "${BLUE}[✓] mount directory ready${RESET}"

echo "[+] STEP 2: Updating the operating system, please wait..."
sudo DEBIAN_FRONTEND=noninteractive apt-get update >/dev/null
echo -e "${BLUE}[✓] system updated${RESET}"

echo "[+] STEP 3: Tuning sysctl..."
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

echo "[+] STEP 4: Creating MicroK8s Fixed-IP Launch configuration..."
cat <<EOF > microk8s-config.yaml
---
version: 0.1.0
addons:
  - name: dns
  - name: rbac
  - name: hostpath-storage
  - name: metrics-server

extraKubeAPIServerArgs:
  --advertise-address: "$NODE_IP"
  --encryption-provider-config: "\$SNAP_DATA/args/encryption-config.yaml"
  --authorization-mode: "RBAC,Node"
  --audit-policy-file: "\$SNAP_DATA/args/audit-policy.yaml"
  --audit-log-path: "\$SNAP_DATA/var/log/kube-apiserver-audit.log"

extraKubeletArgs:
  --node-ip: "$NODE_IP"

extraConfigFiles:
  encryption-config.yaml: |
    apiVersion: apiserver.config.k8s.io/v1
    kind: EncryptionConfiguration
    resources:
      - resources:
          - secrets
          - configmaps
        providers:
          - aescbc:
              keys:
                - name: key1
                  secret: $SKEY
          - identity: {}

  audit-policy.yaml: |
    apiVersion: audit.k8s.io/v1
    kind: Policy
    rules:
      - level: Metadata
        resources:
        - group: ""
          resources: ["secrets", "configmaps"]
      - level: RequestResponse
        verbs: ["create", "update", "patch", "delete"]
        resources:
        - group: ""
          resources: ["pods", "services"]
EOF

sudo mkdir -p /var/snap/microk8s/common/
sudo cp microk8s-config.yaml /var/snap/microk8s/common/.microk8s.yaml
sudo rm microk8s-config.yaml
echo -e "${BLUE}[✓] Launch configuration installed (Fixed IP: $NODE_IP)${RESET}"

echo "[+] STEP 5: Side-loading APIServer2 container image..."
curl -s -O demodb:8080/apiserver2.tar
sudo mkdir -p /var/snap/microk8s/common/sideload
sudo mv apiserver2.tar /var/snap/microk8s/common/sideload
echo -e "${BLUE}[✓] APIServer2 container image pre-installed.${RESET}"

echo "[+] STEP 6: Downloading snaps from local network repository..."
curl -s demodb:8080/microk8s_8612.snap -O
curl -s demodb:8080/microk8s_8612.assert -O
echo -e "${BLUE}[✓] Snaps ready.${RESET}"

echo "[+] STEP 7: Installing MicroK8s via local snap packages..."
sudo snap ack microk8s_8612.assert > /dev/null
sudo snap install microk8s_8612.snap --classic
echo "[+] Waiting for MicroK8s to be ready..."
sudo microk8s status --wait-ready >/dev/null
echo -e "${BLUE}[✓] MicroK8s base system installed on fixed IP interface.${RESET}"
sudo rm microk8s_8612.assert microk8s_8612.snap

echo "[+] STEP 8: Installing traefik ingress via helm chart..."
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
  type: LoadBalancer
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

echo "[+] STEP 9: Waiting for the Ingress pod to be ready..."
sudo microk8s kubectl rollout status daemonset/traefik -n ingress --timeout=120s >/dev/null
echo -e "${BLUE}[✓] Ingress pod deployed.${RESET}"

echo "[+] STEP 11: Deploying APIserver2..."
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/apiserver2-ha.yaml
sudo microk8s kubectl create namespace cppserver > /dev/null
sudo microk8s kubectl label --overwrite ns cppserver pod-security.kubernetes.io/enforce=restricted > /dev/null
sudo microk8s kubectl apply -f apiserver2-ha.yaml > /dev/null

echo "[+] STEP 12: Waiting for APIServer2 Pods to be Ready..."
sudo microk8s kubectl rollout status deployment/apiserver2 -n cppserver --timeout=300s >/dev/null
echo -e "${BLUE}[✓] APIServer2 deployment is ready.${RESET}"

# --- RESTORED STEP 13 ---
echo "[+] STEP 13: Testing APIServer2 connectivity..."
if curl -sk --max-time 5 https://localhost/api/ping >/dev/null; then
  echo -e "${BLUE}[✓] APIServer2 is ready to accept requests at port 443${RESET}"
fi

echo "[+] STEP 14: Waiting for all pods to be ready..."
sudo microk8s kubectl wait --for=condition=Ready pods --all --all-namespaces --timeout=600s >/dev/null
echo -e "${BLUE}[✓] Pods are ready.${RESET}"

echo "[+] STEP 15: Finalizing configuration..."
sudo usermod -a -G microk8s $USER && mkdir -p ~/.kube && chmod 0700 ~/.kube
echo "alias kubectl='microk8s kubectl'" >> ~/.bash_aliases

echo -e "${BLUE}[✓] MicroK8s/APIServer2 setup completed.${RESET}"
echo -e "${YELLOW}${BLINK} →→ Please LOG OUT AND LOG BACK IN for group changes to take effect. ←←${RESET}"
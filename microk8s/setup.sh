clear
echo "---------------------------------------------------------------------------------"
echo "This script will install MicroK8s and deploy APIServer2 on your system."
echo "It may take between 5 and 10 minutes depending on your system and bandwidth."
echo "---------------------------------------------------------------------------------"
sudo mkdir -p /mnt/apiserver-data
sudo chown 10001:10001 /mnt/apiserver-data
echo "[+] Updating the operating system, please wait..."
sudo apt-get -qq update >/dev/null 
sudo apt-get -qq upgrade -y >/dev/null

echo "[+] Tuning sysctl for http clients..."
cat <<EOF | sudo tee /etc/sysctl.d/99-microk8s-highperf.conf
# --- Networking Tuning (High Performance API) ---
net.ipv4.ip_local_port_range = 10240 65535
net.ipv4.tcp_tw_reuse = 1
net.ipv4.tcp_fin_timeout = 15

# --- MicroK8s / CIS Hardening Requirements ---
vm.panic_on_oom=0
vm.overcommit_memory=1
kernel.panic=10
kernel.panic_on_oops=1
kernel.keys.root_maxkeys=1000000
kernel.keys.root_maxbytes=25000000
EOF
sudo sysctl --system >/dev/null
echo "[✓] sysctl updated"

echo "[+] Creating MicroK8s launch configuration..."
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/launch.sh && chmod +x launch.sh
./launch.sh
sudo mkdir -p /var/snap/microk8s/common/
sudo cp microk8s-config.yaml /var/snap/microk8s/common/.microk8s.yaml

echo "[+] Pre-loading APIServer2 container image into the MicroK8s container runtime..."
curl -s -O https://cppserver.com/res/apiserver2.tar
sudo mkdir -p /var/snap/microk8s/common/sideload
sudo cp apiserver2.tar /var/snap/microk8s/common/sideload

echo "[✓] Launch configuration installed."

echo "[+] Installing MicroK8s via snap..."
sudo snap install microk8s --classic >/dev/null
echo "[+] Waiting for MicroK8s to be ready..."
sudo microk8s status --wait-ready >/dev/null
echo "[✓] MicroK8s is ready."
sudo microk8s kubectl version
echo "[✓] MicroK8s base system installed."

echo "[+] Patching ingress controller to redirect HTTP to HTTPS..."
sudo microk8s kubectl patch configmap nginx-load-balancer-microk8s-conf -n ingress \
  --type merge -p '{"data":{"ssl-redirect":"true","force-ssl-redirect":"true"}}'

echo "[+] Patching ingress controller to mount host /etc/localtime..."
sudo microk8s kubectl patch daemonset nginx-ingress-microk8s-controller -n ingress --patch '
spec:
  template:
    spec:
      containers:
      - name: nginx-ingress-microk8s
        volumeMounts:
        - mountPath: /etc/localtime
          name: localtime
          readOnly: true
      volumes:
      - name: localtime
        hostPath:
          path: /etc/localtime
          type: File
'

# --- Wait for ingress controller pods to be Ready --- 
echo "[+] Waiting for the ingress controller pod to be ready - this may take 1-2 minutes..." 
sudo microk8s kubectl rollout status daemonset/nginx-ingress-microk8s-controller -n ingress --timeout=120s >/dev/null
echo "[✓] Ingress deployed."

# --- Verify connectivity on port 80 ---
echo "[+] Testing HTTP connectivity..."
if curl -s --max-time 5 http://localhost/ >/dev/null; then
   echo "[✓] Ingress is serving HTTP traffic at port 80"
fi

# --- Verify connectivity on port 443 (optional, requires TLS configured) ---
echo "[+] Testing HTTPS connectivity..."
if curl -sk --max-time 5 https://localhost/ >/dev/null; then
  echo "[✓] Ingress is serving HTTPS traffic at 443"
fi

# --- Deploy APIServer2 container ---
echo "[+] Retrieving APIserver2 deployment manifest..."
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/apiserver2.yaml 
echo "[+] Deploying APIserver2..."
sudo microk8s kubectl create namespace cppserver
sudo microk8s kubectl label --overwrite ns cppserver pod-security.kubernetes.io/enforce=restricted
sudo microk8s kubectl apply -f apiserver2.yaml

# --- Verify connectivity on port 443 for APIServer2 ---
echo "[+] Waiting for APIServer2 Pods to be Ready..."
sudo microk8s kubectl rollout status deployment/apiserver2 -n cppserver --timeout=300s >/dev/null
echo "[✓] APIServer2 deployment is ready."
echo "[+] Testing APIServer2 connectivity..."
if curl -sk --max-time 5 https://localhost/api/ping >/dev/null; then
  echo "[✓] APIServer2 is ready to accept requests at port 443"
fi

echo "[+] Waiting for all the Kubernetes pods to be ready..."
sudo microk8s kubectl wait --for=condition=Ready pods --all --all-namespaces --timeout=600s >/dev/null
echo "[✓] Pods are ready."

echo "[+] Adding current user to microk8s group..."
sudo usermod -a -G microk8s $USER && mkdir -p ~/.kube && chmod 0700 ~/.kube
echo "[+] Setting up kubectl alias..."
echo "alias kubectl='microk8s kubectl'" >> ~/.bash_aliases
source ~/.bash_aliases
echo "[✓] MicroK8s/APIServer2 setup completed."
echo ""
echo -e "\033[5;33m →→ Please LOG OUT AND LOG BACK IN for group changes to take effect. ←←\033[0m"

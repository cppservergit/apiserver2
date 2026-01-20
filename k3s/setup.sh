clear
echo "---------------------------------------------------------------------------------"
echo "This script will install K3s and deploy APIServer2 on your system."
echo "It may take between 5 and 10 minutes depending on your system and bandwidth."
echo "---------------------------------------------------------------------------------"
sudo mkdir -p /mnt/apiserver-data
sudo chown 10001:10001 /mnt/apiserver-data
echo "[+] Updating the operating system, please wait..."
sudo apt-get -qq update >/dev/null 
sudo apt-get -qq upgrade -y >/dev/null

echo "[+] Tuning sysctl..."
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
echo "[✓] sysctl updated"


echo "[+] Installing K3s..."
curl -sfL https://get.k3s.io | K3S_KUBECONFIG_MODE="644" sh -
sudo kubectl version
kubectl get node -o wide
echo "[✓] K3s base system installed."

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
kubectl create namespace cppserver > /dev/null
kubectl label --overwrite ns cppserver pod-security.kubernetes.io/enforce=restricted > /dev/null
kubectl apply -f apiserver2.yaml > /dev/null

# --- Verify connectivity on port 443 for APIServer2 ---
echo "[+] Waiting for APIServer2 Pods to be Ready..."
kubectl rollout status deployment/apiserver2 -n cppserver --timeout=300s >/dev/null
echo "[✓] APIServer2 deployment is ready."
echo "[+] Testing APIServer2 connectivity..."
if curl -sk --max-time 5 https://localhost/api/ping >/dev/null; then
  echo "[✓] APIServer2 is ready to accept requests at port 443"
fi

echo "[+] Waiting for all the Kubernetes pods to be ready..."
kubectl wait --for=condition=Ready pods --all --all-namespaces --timeout=600s >/dev/null
echo "[✓] Pods are ready."

echo ""

echo "K3s node installation with APIServer2 completed."
echo "To test APIServer2, you can use the following command:"
echo "curl https://localhost/api/ping -ks | jq"

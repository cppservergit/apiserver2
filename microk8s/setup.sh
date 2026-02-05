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

echo "[+] Creating MicroK8s launch configuration..."
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/launch.sh && chmod +x launch.sh
./launch.sh
sudo mkdir -p /var/snap/microk8s/common/
sudo cp microk8s-config.yaml /var/snap/microk8s/common/.microk8s.yaml
sudo rm microk8s-config.yaml
sudo rm launch.sh

# OPTIONAL: preload your container image from a trusted source into MicroK8s, you MUST set [imagePullPolicy: Never] in apiserver2.yaml to make it work
#echo "[+] Pre-loading APIServer2 container image into the MicroK8s container runtime..."
#curl -s -O https://cppserver.com/res/apiserver2.tar
#sudo mkdir -p /var/snap/microk8s/common/sideload
#sudo cp apiserver2.tar /var/snap/microk8s/common/sideload

echo "[✓] Launch configuration installed."

echo "[+] Installing MicroK8s via snap..."
sudo snap install microk8s --classic --channel=1.35/stable>/dev/null
echo "[+] Waiting for MicroK8s to be ready..."
sudo microk8s status --wait-ready >/dev/null
echo "[✓] MicroK8s is ready."
sudo microk8s kubectl version
echo "[✓] MicroK8s base system installed."
# set snap refresh time window to off-peak hours
sudo snap set system refresh.timer=01:00-04:00 >/dev/null

# --- Wait for ingress to be Ready ---
echo "[+] Waiting for the Ingress daemonset to be ready - this may take 1-2 minutes..."
sudo microk8s kubectl rollout status daemonset/traefik -n ingress --timeout=120s >/dev/null
echo "[✓] Ingress daemonset deployed."
echo "[+] Waiting for the Ingress pod to be ready - this may take 1-2 minutes..."
sudo microk8s kubectl wait --namespace ingress --for=condition=ready pod  --selector=app.kubernetes.io/name=traefik --timeout=120s >/dev/null
echo "[✓] Ingress pod deployed."

# --- Verify connectivity on port 443 ---
echo "[+] Testing HTTPS connectivity..."
if curl -sk --max-time 5 https://localhost/ >/dev/null; then
  echo "[✓] Ingress is serving HTTPS traffic at 443"
fi

# --- Deploy APIServer2 container ---
echo "[+] Retrieving APIserver2 deployment manifest..."
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/apiserver2.yaml 
echo "[+] Deploying APIserver2..."
sudo microk8s kubectl create namespace cppserver > /dev/null
sudo microk8s kubectl label --overwrite ns cppserver pod-security.kubernetes.io/enforce=restricted > /dev/null
sudo microk8s kubectl apply -f apiserver2.yaml > /dev/null

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

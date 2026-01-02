sudo apt update && sudo apt upgrade -y
sudo DEBIAN_FRONTEND=noninteractive apt install -y iptables-persistent 
sudo apt install uuid -y
sudo snap install microk8s --classic
sudo microk8s status --wait-ready
sudo microk8s enable hostpath-storage
sudo microk8s enable ingress
sudo microk8s enable metrics-server
sudo microk8s enable registry
sudo microk8s status --wait-ready

sudo microk8s kubectl patch daemonset nginx-ingress-microk8s-controller -n ingress \
  --type='json' \
  -p='[{"op": "add", "path": "/spec/template/spec/hostNetwork", "value": true}, {"op": "add", "path": "/spec/template/spec/dnsPolicy", "value": "ClusterFirstWithHostNet"}]'

sudo microk8s kubectl patch configmap nginx-load-balancer-microk8s-conf -n ingress \
  --type merge -p '{"data":{"ssl-redirect":"true","force-ssl-redirect":"true"}}'

echo "Retrieving APIserver2 deployment manifest..."
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/deploy-apiserver.yaml 
echo "Deploying APIserver2..."
sudo microk8s kubectl apply -f deploy-apiserver.yaml

# --- Wait for ingress controller pods to be Ready --- 
echo "[+] Waiting for ingress controller pod - this may take a few minutes..." 
sudo microk8s kubectl wait --namespace ingress --for=condition=Ready pod -l name=nginx-ingress-microk8s --timeout=420s 

# --- Verify connectivity on port 80 ---
echo "[+] Testing HTTP connectivity..."
if curl -s --max-time 5 http://localhost/ >/dev/null; then
   echo "[✓] Ingress is serving HTTP traffic at http://$VM_IP/"
fi

# --- Verify connectivity on port 443 (optional, requires TLS configured) ---
echo "[+] Testing HTTPS connectivity..."
if curl -sk --max-time 5 https://localhost/ >/dev/null; then
  echo "[✓] Ingress is serving HTTPS traffic at https://$VM_IP/"
fi

# --- Block external access to kubelet (10250), allow localhost  ---
echo "[+] Adding firewall rules to restrict kubelet port..." 
sudo iptables -A INPUT -p tcp --dport 10250 ! -s 127.0.0.1 -j DROP 
sudo ip6tables -A INPUT -p tcp --dport 10250 ! -s ::1 -j DROP 
echo "[+] Saving iptables rules for persistence..." 
sudo netfilter-persistent save

echo "Adding current user to microk8s group..."
sudo usermod -a -G microk8s $USER && mkdir -p ~/.kube && chmod 0700 ~/.kube
echo "Setting up kubectl alias..."
echo "alias kubectl='microk8s kubectl'" >> ~/.bash_aliases
source ~/.bash_aliases
echo "MicroK8s setup completed. Please log out and log back in for group changes an kubectl alias to take effect."

VM_IP=$(ip -4 addr show scope global | grep inet | awk '{print $2}' | cut -d/ -f1 | head -n1)
sudo apt update && sudo apt upgrade -y
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
sudo microk8s kubectl apply -f deploy-apiserver2.yaml

# --- Wait for ingress controller pods to be Ready --- 
echo "[+] Waiting for ingress controller pod..." 
sudo microk8s kubectl wait --namespace ingress --for=condition=Ready pod -l name=nginx-ingress-microk8s --timeout=300s 

# --- Verify connectivity on port 80 ---
echo "[+] Testing HTTP connectivity..."
if curl -s --max-time 5 http://$VM_IP/ >/dev/null; then
   echo "[✓] Ingress is serving HTTP traffic at http://$VM_IP/"
fi

# --- Verify connectivity on port 443 (optional, requires TLS configured) ---
echo "[+] Testing HTTPS connectivity..."
if curl -sk --max-time 5 https://$VM_IP/ >/dev/null; then
  echo "[✓] Ingress is serving HTTPS traffic at https://$VM_IP/"
fi

echo "Adding current user to microk8s group..."
sudo usermod -a -G microk8s $USER && mkdir -p ~/.kube && chmod 0700 ~/.kube
echo "Setting up kubectl alias..."
echo "alias kubectl='microk8s kubectl'" >> ~/.bash_aliases
source ~/.bash_aliases
echo "MicroK8s setup completed. Please log out and log back in for group changes an kubectl alias to take effect."

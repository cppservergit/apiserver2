VM_IP=$(ip -4 addr show scope global | grep inet | awk '{print $2}' | cut -d/ -f1 | head -n1)
sudo apt update && sudo apt upgrade -y
sudo apt install uuid -y
sudo snap install microk8s --classic
sudo microk8s status --wait-ready
sudo microk8s enable hostpath-storage
sudo microk8s enable ingress
sudo microk8s enable metrics-server
sudo microk8s enable registry
sudo microk8s enable metallb:${VM_IP}-${VM_IP}
sudo microk8s status --wait-ready

# --- Wait for ingress controller pods to be Ready --- 
echo "[+] Waiting for ingress controller pods..." 
sudo microk8s kubectl wait --namespace ingress --for=condition=Ready pod -l name=nginx-ingress-microk8s --timeout=180s 
# --- Wait for ingress service to get an external IP --- 
echo "[+] Waiting for ingress service external IP..." 
for i in {1..30}; 
   do IP=$(sudo microk8s kubectl get svc nginx-ingress-microk8s-controller -n ingress -o jsonpath='{.status.loadBalancer.ingress[0].ip}') 
   if [ -n "$IP" ]; then 
     echo "[✓] Ingress service external IP assigned: $IP" 
     break 
   fi 
   sleep 10 
done
# --- Verify connectivity on port 80 ---
echo "[+] Testing HTTP connectivity..."
for i in {1..10}; do
  if curl -s --max-time 5 http://$IP/ >/dev/null; then
    echo "[✓] Ingress is serving HTTP traffic at http://$IP/"
    break
  fi
  sleep 5
done

# --- Verify connectivity on port 443 (optional, requires TLS configured) ---
echo "[+] Testing HTTPS connectivity..."
for i in {1..10}; do
  if curl -sk --max-time 5 https://$IP/ >/dev/null; then
    echo "[✓] Ingress is serving HTTPS traffic at https://$IP/"
    break
  fi
  sleep 5
done
echo "Retrieving APIserver2 deployment manifest..."
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/deploy-apiserver2.yaml 
echo "Deploying APIserver2..."
sudo microk8s kubectl apply -f deploy-apiserver2.yaml
echo "Adding current user to microk8s group..."
sudo usermod -a -G microk8s $USER && mkdir -p ~/.kube && chmod 0700 ~/.kube
echo "Setting up kubectl alias..."
echo "alias kubectl='microk8s kubectl'" >> ~/.bash_aliases
source ~/.bash_aliases
echo "MicroK8s setup completed. Please log out and log back in for group changes to take effect."

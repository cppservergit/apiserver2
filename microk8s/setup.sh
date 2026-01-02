sudo apt update && sudo apt upgrade -y
sudo apt install uuid -y
sudo snap install microk8s --classic
sudo microk8s status --wait-ready
sudo microk8s enable hostpath-storage
sudo microk8s enable ingress
sudo microk8s enable metrics-server
sudo microk8s enable registry
sudo microk8s status --wait-ready

echo "[+] Patching ingress controller to use host network..."
sudo microk8s kubectl patch daemonset nginx-ingress-microk8s-controller -n ingress \
  --type='json' \
  -p='[{"op": "add", "path": "/spec/template/spec/hostNetwork", "value": true}, {"op": "add", "path": "/spec/template/spec/dnsPolicy", "value": "ClusterFirstWithHostNet"}]'

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

echo "[+] Retrieving APIserver2 deployment manifest..."
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/deploy-apiserver.yaml 
echo "[+] Deploying APIserver2..."
sudo microk8s kubectl apply -f deploy-apiserver.yaml

# --- Wait for ingress controller pods to be Ready --- 
echo "[+] Waiting for ingress controller pod - this may take a few minutes..." 
sudo microk8s kubectl wait --namespace ingress --for=condition=Ready pod -l name=nginx-ingress-microk8s --timeout=420s 

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

echo "[+] Adding current user to microk8s group..."
sudo usermod -a -G microk8s $USER && mkdir -p ~/.kube && chmod 0700 ~/.kube
echo "[+] Setting up kubectl alias..."
echo "alias kubectl='microk8s kubectl'" >> ~/.bash_aliases
source ~/.bash_aliases
echo "[✓] MicroK8s setup completed. Please log out and log back in for group changes an kubectl alias to take effect."

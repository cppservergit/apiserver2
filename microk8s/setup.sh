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
curl -s -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/apiserver2.yaml 
echo "[+] Deploying APIserver2..."
sudo microk8s kubectl apply -f apiserver2.yaml

# --- Wait for ingress controller pods to be Ready --- 
echo "[+] Waiting for the ingress controller pod to be ready - this may take a few seconds..." 
sudo microk8s kubectl rollout status daemonset/nginx-ingress-microk8s-controller -n ingress --timeout=120s

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

# --- Verify connectivity on port 443 for APIServer2 ---
echo "[+] Waiting for APIServer2 Pods to be Ready..."
sudo microk8s kubectl rollout status deployment/apiserver-deployment --timeout=300s
echo "[+] Testing APIServer2 connectivity..."
if curl -sk --max-time 5 https://localhost/api/ping >/dev/null; then
  echo "[✓] APIServer2 is ready to accept requests at port 443"
fi

APISERVER2_VERSION=$(curl https://localhost/api/version -k -H "x-api-key: 6976f434-d9c1-11f0-93b8-5254000f64af" -s | jq -r '.version')
echo "[+] APIServer2 version: $APISERVER2_VERSION"

echo "[+] Adding current user to microk8s group..."
sudo usermod -a -G microk8s $USER && mkdir -p ~/.kube && chmod 0700 ~/.kube
echo "[+] Setting up kubectl alias..."
echo "alias kubectl='microk8s kubectl'" >> ~/.bash_aliases
source ~/.bash_aliases
echo "[✓] MicroK8s setup completed."
echo -e "\033[31mPlease LOG OUT AND LOG BACK IN for group changes to take effect.\033[0m"

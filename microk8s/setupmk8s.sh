VM_IP=$(ip -4 addr show scope global | grep inet | awk '{print $2}' | cut -d/ -f1 | head -n1)
sudo apt update && sudo apt upgrade -y
sudo snap install microk8s --classic
sudo microk8s status --wait-ready
sudo microk8s enable hostpath-storage
sudo microk8s enable ingress
sudo microk8s enable metrics-server
sudo microk8s enable registry
sudo microk8s status --wait-ready
sudo microk8s enable metallb:${VM_IP}-${VM_IP}
curl -O -L https://raw.githubusercontent.com/cppservergit/apiserver2/main/microk8s/deploy-apiserver2.yaml 
sudo microk8s kubectl apply -f deploy-apiserver2.yaml
sudo usermod -a -G microk8s $USER && mkdir -p ~/.kube && chmod 0700 ~/.kube
echo "alias kubectl='microk8s kubectl'" >> ~/.bash_aliases
source ~/.bash_aliases
echo "MicroK8s setup completed. Please log out and log back in for group changes to take effect."

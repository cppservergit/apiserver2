#!/bin/bash

# this script sets up LXD with a cluster of two nodes running APIServer2 behind HAProxy
# it is intended to be run on a fresh Ubuntu 24.04 server
# a VM with at least 12GB RAM and 2 vCPUs is recommended

# update and upgrade the system
sudo apt update && sudo apt upgrade -y

# download APIServer2
curl https://cppserver.com/res/apiserver -O && chmod +x apiserver

# setup central upload directory
sudo mkdir -p /srv/uploads
sudo chown 1000:1000 /srv/uploads
sudo chmod 775 /srv/uploads

# timezone
sudo timedatectl set-timezone America/Caracas

# create LXD configuration file ready for DNS and IPv4 only
sudo tee preseed.yaml > /dev/null <<EOF
config: {}
networks:
- config:
    dns.domain: lxd
    dns.mode: managed
    ipv4.address: 10.112.244.1/24
    ipv4.dhcp.expiry: 87600h
    ipv4.nat: "true"
    ipv6.address: none
  description: ""
  name: lxdbr0
  type: bridge
  project: default
storage_pools:
- config:
    source: /var/snap/lxd/common/lxd/storage-pools/default
  description: ""
  name: default
  driver: dir
storage_volumes: []
profiles:
- config:
    raw.idmap: |
      uid 1000 1000
      gid 1000 1000
  description: Default LXD profile
  devices:
    eth0:
      name: eth0
      network: lxdbr0
      type: nic
    root:
      path: /
      pool: default
      type: disk
    uploads:
      path: /uploads
      source: /srv/uploads
      type: disk
  name: default
projects:
- config:
    features.images: "true"
    features.networks: "true"
    features.networks.zones: "true"
    features.profiles: "true"
    features.storage.buckets: "true"
    features.storage.volumes: "true"
  description: Default LXD project
  name: default
EOF

# install LXD
sudo snap install lxd
sudo lxd init --preseed < preseed.yaml

# setup firewall on lxd bridge
UFW_STATUS=$(sudo ufw status | awk '/Status:/ {print $2}')
if [ "$UFW_STATUS" = "active" ]; then
	echo "Configuring HAProxy/LXD rules for UFW firewall..."
	sudo ufw allow in on lxdbr0
	sudo ufw route allow in on lxdbr0
	sudo ufw route allow out on lxdbr0
	sudo ufw allow 443
	sudo ufw allow 80
	sudo ufw allow 8404
else
    echo "UFW firewall is disabled. Skipping port rules."
fi

# set LXD dns
LXD_IP=$(ip -4 -br a show "lxdbr0" | awk '{print $3}' | cut -d'/' -f1)
RESOLVED_CONF="/etc/systemd/resolved.conf"
sudo cp "$RESOLVED_CONF" "${RESOLVED_CONF}.bak.$(date +%F-%T)"
sudo sed -i "s/^#*DNS=.*/DNS=$LXD_IP/" "$RESOLVED_CONF"
sudo sed -i "s/^#*Domains=.*/Domains=~lxd/" "$RESOLVED_CONF"
sudo systemctl restart systemd-resolved

# install haproxy
sudo apt install haproxy -y

# create node1 on LXD cluster
sudo lxc launch ubuntu:24.04 node1
lxc exec node1 -- bash -c "apt update && apt upgrade -y"
lxc exec node1 -- apt install -y --no-install-recommends libcurl4 libssl3 libuuid1 libjson-c5 liboath0t64 unixodbc tdsodbc
lxc exec node1 -- timedatectl set-timezone America/Caracas

# create systemd service configuration file
tee "apiserver.service" > /dev/null <<'EOF'
[Unit]
Description=APIServer2
ConditionPathExists=/home/ubuntu
After=network.target

[Service]
Type=simple
User=ubuntu
Group=ubuntu
LimitNOFILE=65000
Restart=on-failure
RestartSec=10
WorkingDirectory=/home/ubuntu
ExecStart=/home/ubuntu/run.sh
SyslogIdentifier=apiserver

[Install]
WantedBy=multi-user.target
EOF

# create run.sh script for apiserver2
tee "run.sh" > /dev/null <<'EOF'
#!/bin/bash

# server configuration
export PORT=8080
export POOL_SIZE=24
export IO_THREADS=4
export QUEUE_CAPACITY=500

# database configuration
export DB1="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=demodb;UID=sa;PWD=Basica2024;APP=apiserver;Encryption=off;ClientCharset=UTF-8"
export LOGINDB="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=testdb;UID=sa;PWD=Basica2024;APP=apiserver-login;Encryption=off;ClientCharset=UTF-8"

# cors configuration
export CORS_ORIGINS="null,file://"

# blobs storage configuration
export BLOB_PATH="/home/ubuntu/uploads"

# json web token configuration
export JWT_SECRET="B@asica2025*uuid0998554j93m722pQ"
export JWT_TIMEOUT_SECONDS=300

# api key for diagnostics
export API_KEY="6976f434-d9c1-11f0-93b8-5254000f64af"

# remote api configuration
export REMOTE_API_URL="http://localhost:8080"
export REMOTE_API_USER="mcordova"
export REMOTE_API_PASS="basica"

# executable
./apiserver
EOF

sudo chmod +x run.sh

# copy files into node1 and configure systemd service for apiserver
lxc file push run.sh node1/home/ubuntu/
lxc file push apiserver node1/home/ubuntu/
lxc file push apiserver.service node1/lib/systemd/system/
lxc exec node1 -- systemctl daemon-reload
lxc exec node1 -- systemctl enable apiserver.service
lxc exec node1 -- systemctl start apiserver

# create node2 - repeat for node3, node4, etc
lxc copy node1 node2
lxc start node2

# configure haproxy to connect to lxd backend
HA_CONFIG_FILE="/etc/haproxy/haproxy.cfg"
sudo cp "$HA_CONFIG_FILE" "${HA_CONFIG_FILE}.bak.$(date +%F-%T)"

sudo tee -a "$HA_CONFIG_FILE" > /dev/null <<'EOF'
  unique-id-format %[uuid()]
  unique-id-header X-Request-ID
  log-format "${HAPROXY_HTTP_LOG_FMT} %ID"
EOF

sudo tee -a "$HA_CONFIG_FILE" > /dev/null <<EOF

resolvers lxd_dns
  nameserver lxd $LXD_IP:53
  resolve_retries       5
  timeout resolve       5s
  timeout retry         5s
  hold valid            60s

frontend stats
  bind *:8404
  stats enable
  stats uri /
  stats refresh 10s

frontend www
  bind :80
  option forwardfor
  acl is_chunked_request hdr(transfer-encoding) -i chunked
  http-request deny deny_status 501 if is_chunked_request
  http-request deny if { method CONNECT }
  default_backend lxd_cluster

backend lxd_cluster
  http-reuse always
  balance leastconn
  option httpchk GET /ping HTTP/1.1
  server node1 node1.lxd:8080 check resolvers lxd_dns resolve-prefer ipv4 init-addr last,libc,none
  server node2 node2.lxd:8080 check resolvers lxd_dns resolve-prefer ipv4 init-addr last,libc,none
EOF

# restart system log and haproxy services to avoid permission error
sudo systemctl restart systemd-journald
sudo systemctl restart haproxy

# clean files
sudo rm run.sh
sudo rm apiserver.service
sudo rm preseed.yaml
sudo rm apiserver

#!/bin/bash
clear

# Start timer
START_TIME=$(date +%s)

# --- COLORS ---
BLUE='\033[94m'
RESET='\033[0m'
YELLOW='\033[33m'
BLINK='\033[5m'

# --- CONFIGURATION ---
# Dynamically extract this VM's IP from eth1
HA_IP=$(ip -4 addr show eth0 | grep -oP '(?<=inet\s)\d+(\.\d+){3}')

# Backend Kubernetes Nodes (Fixed IPs from provision-vms.ps1)
NODE1_IP="172.22.124.70"
NODE2_IP="172.22.123.137"

# JA4 Fingerprint Plugin URL
JA4_LUA_URL="https://raw.githubusercontent.com/O-X-L/haproxy-ja4-fingerprint/latest/ja4.lua"

# Exit if eth1 is not found
if [ -z "$HA_IP" ]; then
    echo -e "${YELLOW}[!] ERROR: Could not find an IP on eth1. Check bridge.${RESET}"
    exit 1
fi

echo "---------------------------------------------------------------------------------"
echo -e "Starting HAProxy 3.2 Advanced Security Setup on ${BLUE}$HA_IP${RESET}"
echo -e "Target Backends: ${BLUE}$NODE1_IP, $NODE2_IP${RESET}"
echo "---------------------------------------------------------------------------------"

echo "[+] STEP 1: Updating system and installing dependencies..."
sudo apt-get update >/dev/null
sudo apt-get install socat -y >/dev/null
echo -e "${BLUE}[✓] system updated and dependencies installed${RESET}"

echo "[+] STEP 2: Installing HAProxy 3.2 via official PPA..."
sudo add-apt-repository ppa:vbernat/haproxy-3.2 -y >/dev/null 2>&1
sudo apt-get update >/dev/null
sudo apt-get install haproxy -y >/dev/null
echo -e "${BLUE}[✓] HAProxy $(haproxy -v | grep -oP 'version \d+\.\d+\.\d+') installed${RESET}"

echo "[+] STEP 3: Generating SSL Certificate..."
sudo openssl req -x509 -newkey rsa:2048 \
  -keyout /etc/ssl/private/haproxy.key \
  -out /etc/ssl/certs/haproxy.crt \
  -days 365 -nodes \
  -subj "/C=US/ST=Test/L=Test/O=HAProxy/CN=localhost" >/dev/null 2>&1

sudo cat /etc/ssl/certs/haproxy.crt /etc/ssl/private/haproxy.key | sudo tee /etc/ssl/private/haproxy.pem > /dev/null
sudo chmod 600 /etc/ssl/private/haproxy.pem
echo -e "${BLUE}[✓] SSL certificate generated at /etc/ssl/private/haproxy.pem${RESET}"

echo "[+] STEP 4: Configuring JA4 TLS Fingerprinting and Blacklists..."
sudo mkdir -p /etc/haproxy/lua
sudo mkdir -p /etc/haproxy/blacklists

# Create the blacklist file with provided initial content
cat <<EOF | sudo tee /etc/haproxy/blacklists/ja4_blacklist.lst > /dev/null
# /etc/haproxy/blacklists/ja4_blacklist.lst
# Malicious JA4 Fingerprints Blacklist

# --- Malware and Trojans (C2 Callbacks) ---
# SnakeLogger / Redline Stealer
t10d070600_c50f5591e341_1a3805c3aa63
# IcedID Malware
t13d201100_2b729b4bf6f3_9e7b989ebec8
# Linux Cryptominer / Trojan
t12d5908h1_7bd0586cbef7_046e095b7c4a

# --- Offensive Security Tools & Scanners ---
# sqlmap
t13i311000_e8f1e7e78f70_d41ae481755e
# AppScan
t12i3006h2_a0f71150605f_1da50ec048a3

# --- Automated Bots & Anomalous Frameworks ---
# Undetected ChromeDriver / Python Requests
t13d1516h2_8daaf6152771_02713d6af862
# Heritrix Web Crawler
t13d491100_bd868743f55c_fa269c3d986d
EOF

# Set secure permissions
sudo chmod 600 /etc/haproxy/blacklists/ja4_blacklist.lst

if sudo curl -L "$JA4_LUA_URL" -o /etc/haproxy/lua/ja4.lua; then
    echo -e "${BLUE}[✓] JA4 Lua script and Blacklist (8 entries) integrated${RESET}"
else
    echo -e "${YELLOW}[!] WARNING: Could not download ja4.lua.${RESET}"
fi

echo "[+] STEP 5: Generating HAProxy configuration with Advanced Security..."
cat <<EOF | sudo tee /etc/haproxy/haproxy.cfg > /dev/null
global
        log /dev/log    local0
        log /dev/log    local1 notice
        chroot /var/lib/haproxy
        stats socket /run/haproxy/admin.sock mode 660 level admin
        stats timeout 30s
        user haproxy
        group haproxy
        daemon

        # Default SSL material locations
        ca-base /etc/ssl/certs
        crt-base /etc/ssl/private

        # Modern SSL configuration
        ssl-default-bind-ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384
        ssl-default-bind-ciphersuites TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256
        ssl-default-bind-options ssl-min-ver TLSv1.2 no-tls-tickets

        # Load JA4 Lua script with necessary tuning
        tune.lua.bool-sample-conversion normal
        tune.ssl.capture-buffer-size 192
        lua-load /etc/haproxy/lua/ja4.lua    

defaults
        log     global
        mode    http
        option  httplog
        option  dontlognull
        timeout connect 5000
        timeout client  50000
        timeout server  50000
        errorfile 400 /etc/haproxy/errors/400.http
        errorfile 403 /etc/haproxy/errors/403.http
        errorfile 408 /etc/haproxy/errors/408.http
        errorfile 500 /etc/haproxy/errors/500.http
        errorfile 502 /etc/haproxy/errors/502.http
        errorfile 503 /etc/haproxy/errors/503.http
        errorfile 504 /etc/haproxy/errors/504.http

frontend apiserver2
    bind *:443 ssl crt /etc/ssl/private/haproxy.pem

    # ====================================================================
    # LOGGING & REQUEST TRACKING
    # ====================================================================
    option httplog
    unique-id-format %[uuid()]
    unique-id-header X-Request-ID

    # Capture headers for log
    http-request capture req.hdr(User-Agent) len 64
    option forwardfor

    # Custom log format includes %ID (the UUID) at the beginning
    log-format "%ID %ci [%tr] %ft %b/%s %TR/%Tw/%Tc/%Tr/%Ta %ST %B %CC %CS %tsc %ac/%fc/%bc/%sc/%rc %sq/%bq %hr %hs %{+Q}r %[var(txn.log_tag)]"

    # ====================================================================
    # JA4 FINGERPRINTING & BLOCKING
    # ====================================================================
    # 1. Execute the Lua script to calculate the JA4 fingerprint
    http-request lua.fingerprint_ja4

    # 2. Store the fingerprint in a variable
    http-request set-var(txn.ja4) var(txn.fingerprint_ja4)

    # 3. Check the blacklist file
    acl is_blacklisted_ja4 var(txn.ja4) -m str -f /etc/haproxy/blacklists/ja4_blacklist.lst

    # 4. Deny if blacklisted
    http-request set-var(txn.log_tag) str("JA4_BLOCK") if is_blacklisted_ja4
    http-request deny deny_status 403 if is_blacklisted_ja4

    # 5. Append the JA4 hash to the log for visibility
    http-request capture var(txn.ja4) len 36

    # ====================================================================
    # BASE SECURITY & PROTOCOL ENFORCEMENT
    # ====================================================================
    acl is_connect method CONNECT
    http-request set-var(txn.log_tag) str("CONNECT_BLOCK") if is_connect
    http-request deny if is_connect

    acl is_chunked_request hdr(transfer-encoding) -i chunked
    http-request set-var(txn.log_tag) str("CHUNKED_BLOCK") if is_chunked_request
    http-request deny deny_status 501 if is_chunked_request

    acl starts_with_api path_beg /api/
    acl is_login path -i /api/login
    acl is_ping  path -i /api/ping

    http-request deny deny_status 404 if !starts_with_api

    # Drop /api/ping requests (Internal only)
    http-request set-var(txn.log_tag) str("PING_BLOCK") if is_ping
    http-request deny deny_status 404 if is_ping

    # ====================================================================
    # RATE LIMITING FOR /api/login
    # ====================================================================
    stick-table type ip size 100k expire 1m store http_req_rate(1m)
    http-request track-sc0 src if is_login
    acl login_abuse sc_http_req_rate(0) gt 5

    http-request set-var(txn.log_tag) str("BRUTEFORCE_BLOCK") if is_login login_abuse
    http-request deny deny_status 429 if is_login login_abuse

    # ====================================================================
    # AUTHORIZATION ENFORCEMENT
    # ====================================================================
    acl has_bearer_token req.hdr(Authorization) -i -m beg "Bearer "
    http-request set-var(txn.log_tag) str("UNAUTH_BLOCK") if !is_login !is_ping !has_bearer_token
    http-request deny deny_status 401 if !is_login !has_bearer_token

    default_backend microk8s

backend microk8s
    balance roundrobin
    option httpchk GET /api/ping
    http-check expect status 200
    
    server node1 $NODE1_IP:443 check ssl verify none
    server node2 $NODE2_IP:443 check ssl verify none
EOF

sudo systemctl restart haproxy
echo -e "${BLUE}[✓] HAProxy configuration applied${RESET}"

echo "[+] STEP 6: Creating hainfo utility..."
cat << 'EOF' | sudo tee /bin/hainfo > /dev/null
#!/bin/bash
clear && echo "show stat" | sudo socat /run/haproxy/admin.sock stdio | awk -F"," -v backend="microk8s" '
  NR==1 {
    for (i=1; i<=NF; i++) {
      gsub(/^# */, "", $i)
      name = $i
      idx[name] = i
    }
    i_px      = idx["pxname"]
    i_sv      = idx["svname"]
    i_stat    = idx["status"]
    i_rate    = idx["rate"]
    i_req     = idx["req_tot"]
    i_econ    = idx["econ"]
    i_eresp   = idx["eresp"]
    i_chkfail = idx["chkfail"]
    printf "%-12s %-12s %-8s %-10s %-10s %-10s %-10s %-10s\n", "PXNAME", "SVNAME", "STATUS", "RATE_CUR", "REQ_TOT", "ECONN", "ERESP", "CHKFAIL"
    print  "---------------------------------------------------------------------------------------------"
    next
  }
  $i_px == backend && $i_sv != "BACKEND" {
    raw = $i_stat
    status = (raw == "" || raw == "0") ? "UP" : (raw == "1" ? "DOWN" : raw)
    printf "%-12s %-12s %-8s %-10s %-10s %-10s %-10s %-10s\n", $i_px, $i_sv, status, $i_rate, $i_req, $i_econ, $i_eresp, $i_chkfail
  }'
EOF
sudo chmod +x /bin/hainfo
echo -e "${BLUE}[✓] hainfo utility created in /bin${RESET}"

echo "[+] STEP 7: Verifying local proxy response..."
if curl -sk --max-time 5 https://localhost/api/login >/dev/null; then
  echo -e "${BLUE}[✓] Load Balancer is successfully communicating with K8s backends${RESET}"
fi

# Calculate duration
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))
MINUTES=$((DURATION / 60))
SECONDS=$((DURATION % 60))

echo -e "${BLUE}[✓] HAProxy installation completed in ${MINUTES}m ${SECONDS}s.${RESET}"
echo -e "${YELLOW}${BLINK} →→ Advanced Security Proxy is Active at $HA_IP ←←${RESET}"
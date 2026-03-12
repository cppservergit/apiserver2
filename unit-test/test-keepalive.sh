#!/bin/bash

DEFAULT_BASE_URL="http://localhost:8080"
DEFAULT_API_PREFIX=""

# Accept parameters (positional arguments)
BASE_URL="${1:-$DEFAULT_BASE_URL}"
API_PREFIX="${2:-$DEFAULT_API_PREFIX}"
LOGIN_PAYLOAD='{"username":"mcordova","password":"basica"}'
API_KEY="6976f434-d9c1-11f0-93b8-5254000f64af"
TOTP_SECRET="FLVSIZNN3JF2Z3US"

amber="\e[38;5;214m"
red="\e[31m"
reset="\e[0m"

function show_result {
  local endpoint="$1"
  local status="$2"
  local success="$3"
  local body="$4"
  local color="$amber"
  [[ "$status" != "200" ]] && color="$red"

  printf "${color}%-35s %-6s %-8s${reset}\n" "$endpoint" "$status" "$success"
  [[ "$status" != "200" ]] && echo -e "${body}\n"
}

login_response=$(curl -ks -w "%{http_code}" -H "Content-Type: application/json" \
  -d "$LOGIN_PAYLOAD" "${BASE_URL}${API_PREFIX}/login")

login_body="${login_response::-3}"
login_status="${login_response: -3}"

if [[ "$login_status" != "200" ]]; then
  echo -e "${red}Login failed with status ${login_status}${reset}"
  echo "$login_body"
  exit 1
fi

TOKEN1=$(echo "$login_body" | jq -r '.id_token')
if [[ "$TOKEN1" == "null" || -z "$TOKEN1" ]]; then
  echo -e "${red}Token extraction failed${reset}"
  exit 1
fi

# UNCOMMENT BELOW TO TEST MFA and use TOKEN2 for subsequent API calls instead of TOKEN1
# requires 1) oathtool to be installed 2) MFA enabled on APIServer2 and 3) TOTP_SECRET to match the server's MFA secret for this user
# Generate TOTP code for MFA 
#TOTP=$(oathtool --base32 --totp $TOTP_SECRET)
# validate TOTP with stage-1 token and get stage-2 token
#TOKEN2=$(curl --json '{"totp":"'$TOTP'"}' "${BASE_URL}${API_PREFIX}/validate/totp" -ks -H "Authorization: Bearer $TOKEN1" | jq -r '.id_token')

if [[ -v TOKEN2 && "$TOKEN2" != "null" ]]; then TOKEN="$TOKEN2"; else TOKEN="$TOKEN1"; fi

endpoints=(
  "GET $API_PREFIX/shippers"
  "GET $API_PREFIX/products"
  "GET $API_PREFIX/metrics"
  "GET $API_PREFIX/metricsp"
  "GET $API_PREFIX/version"
  "GET $API_PREFIX/ping"
  "POST $API_PREFIX/customer {\"id\":\"anatr\"}"
  "POST $API_PREFIX/customer {\"id\":\"quick\"}"
  "POST $API_PREFIX/customer {\"id\":\"bergs\"}"
  "POST $API_PREFIX/customer {\"id\":\"ernsh\"}"
  "POST $API_PREFIX/customer {\"id\":\"fissa\"}"
  "POST $API_PREFIX/customer {\"id\":\"dracd\"}"
  "POST $API_PREFIX/customer {\"id\":\"savea\"}"
  "POST $API_PREFIX/sales {\"start_date\":\"1994-01-01\",\"end_date\":\"1994-12-31\"}"
  "POST $API_PREFIX/sales {\"start_date\":\"1995-01-01\",\"end_date\":\"1995-12-31\"}"
  "POST $API_PREFIX/sales {\"start_date\":\"1996-01-01\",\"end_date\":\"1996-12-31\"}"
  "POST $API_PREFIX/rcustomer {\"id\":\"anatr\"}"
)

# 1. Build the arguments for a single curl command
curl_args=()
req_idx=0
tmp_dir=$(mktemp -d)

for entry in "${endpoints[@]}"; do
  IFS=' ' read -r method uri rest <<< "$entry"
  
  # Add the --next separator for all requests after the first one
  if [[ $req_idx -gt 0 ]]; then
    curl_args+=("--next")
  fi

  body_file="${tmp_dir}/body_${req_idx}.txt"
  
  # Capture HTTP status to stdout, and direct the body to a specific temp file
  curl_args+=("-k" "-s" "-w" "%{http_code}\n" "-o" "$body_file")

  if [[ "$method" == "POST" ]]; then
    curl_args+=("-X" "POST" "-H" "Content-Type: application/json" "-H" "Authorization: Bearer $TOKEN" "-d" "$rest" "${BASE_URL}${uri}")
  else
    if [[ "$uri" == "$API_PREFIX/version" || "$uri" == "$API_PREFIX/metrics" || "$uri" == "$API_PREFIX/metricsp" ]]; then
      curl_args+=("-X" "GET" "-H" "Authorization: Bearer $API_KEY" "${BASE_URL}${uri}")
    else
      curl_args+=("-X" "GET" "-H" "Authorization: Bearer $TOKEN" "${BASE_URL}${uri}")
    fi
  fi
  
  ((req_idx++))
done

# 2. Execute the single curl instance to reuse the connection
# The output will be a newline-separated list of HTTP status codes
statuses=$(curl "${curl_args[@]}")

# Map the statuses into an array for easy iteration
mapfile -t status_array <<< "$statuses"

# 3. Process and print the results
req_idx=0
for entry in "${endpoints[@]}"; do
  IFS=' ' read -r method uri rest <<< "$entry"
  
  status="${status_array[$req_idx]}"
  body_file="${tmp_dir}/body_${req_idx}.txt"
  
  body=$(cat "$body_file" 2>/dev/null)
  
  ok="false"
  [[ "$status" == "200" ]] && ok="true"

  show_result "$method $uri" "$status" "$ok" "$body"
  
  ((req_idx++))
done

# Clean up the temporary directory
rm -rf "$tmp_dir"

exit 0
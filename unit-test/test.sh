#!/bin/bash

DEFAULT_BASE_URL="http://localhost:8080"
DEFAULT_API_PREFIX=""

# Accept parameters (positional arguments)
BASE_URL="${1:-$DEFAULT_BASE_URL}"
API_PREFIX="${2:-$DEFAULT_API_PREFIX}"
LOGIN_PAYLOAD='{"username":"mcordova","password":"basica"}'
API_KEY="6976f434-d9c1-11f0-93b8-5254000f64af"

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

# Check if uuid command exists 
if ! command -v uuid >/dev/null 2>&1; then 
   echo "uuid command not found, installing uuid-runtime..." 
   sudo apt update && sudo apt install -y uuid-runtime 
fi

CURL_UUID="$(echo -n $(uuid))"
login_response=$(curl -k -s -w "%{http_code}" -H "Content-Type: application/json" \
  -H "X-Request-ID: $CURL_UUID" -d "$LOGIN_PAYLOAD" "${BASE_URL}${API_PREFIX}/login")

login_body="${login_response::-3}"
login_status="${login_response: -3}"

if [[ "$login_status" != "200" ]]; then
  echo -e "${red}Login failed with status ${login_status}${reset}"
  echo "$login_body"
  exit 1
fi

TOKEN=$(echo "$login_body" | jq -r '.id_token')
if [[ "$TOKEN" == "null" || -z "$TOKEN" ]]; then
  echo -e "${red}Token extraction failed${reset}"
  exit 1
fi

endpoints=(
  "GET $API_PREFIX/shippers"
  "GET $API_PREFIX/products"
  "GET $API_PREFIX/metrics"
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

for entry in "${endpoints[@]}"; do
  IFS=' ' read -r method uri rest <<< "$entry"
  payload=""
  CURL_UUID="$(echo -n $(uuid))"

  # Initialize an array for extra curl headers
  extra_headers=()
  
  # Check if the current URI is one that requires the API key
  if [[ "$uri" == "$API_PREFIX/version" || "$uri" == "$API_PREFIX/metrics" ]]; then
    extra_headers+=("-H" "x-api-key: $API_KEY")
  fi

  if [[ "$method" == "POST" ]]; then
    payload="$rest"
    # We expand "${extra_headers[@]}" here to inject the header if it exists
    response=$(curl -k -s -w "%{http_code}" -H "Content-Type: application/json" \
      -H "Authorization: Bearer $TOKEN" -H "X-Request-ID: $CURL_UUID" \
      "${extra_headers[@]}" \
      -d "$payload" "${BASE_URL}${uri}")
  else
    response=$(curl -k -s -w "%{http_code}" -H "Authorization: Bearer $TOKEN" \
      -H "X-Request-ID: $CURL_UUID" \
      "${extra_headers[@]}" \
      "${BASE_URL}${uri}")
  fi

  body="${response::-3}"
  status="${response: -3}"
  ok="false"
  [[ "$status" == "200" ]] && ok="true"

  show_result "$method $uri" "$status" "$ok" "$body"
done
exit 0
#!/bin/bash

BASE_URL="http://localhost:8080"
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

CURL_UUID="$(echo -n $(uuid))"
login_response=$(curl -s -w "%{http_code}" -H "Content-Type: application/json" \
  -H "X-Request-ID: $CURL_UUID" -d "$LOGIN_PAYLOAD" "${BASE_URL}/login")

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
  "GET /shippers"
  "GET /products"
  "GET /metrics"
  "GET /version"
  "GET /ping"
  "POST /customer {\"id\":\"anatr\"}"
  "POST /customer {\"id\":\"quick\"}"
  "POST /customer {\"id\":\"bergs\"}"
  "POST /customer {\"id\":\"ernsh\"}"
  "POST /customer {\"id\":\"fissa\"}"
  "POST /customer {\"id\":\"dracd\"}"
  "POST /customer {\"id\":\"savea\"}"
  "POST /sales {\"start_date\":\"1994-01-01\",\"end_date\":\"1994-12-31\"}"
  "POST /sales {\"start_date\":\"1995-01-01\",\"end_date\":\"1995-12-31\"}"
  "POST /sales {\"start_date\":\"1996-01-01\",\"end_date\":\"1996-12-31\"}"
  "POST /rcustomer {\"id\":\"anatr\"}"
)

for entry in "${endpoints[@]}"; do
  IFS=' ' read -r method uri rest <<< "$entry"
  payload=""
  CURL_UUID="$(echo -n $(uuid))"

  # Initialize an array for extra curl headers
  extra_headers=()
  
  # Check if the current URI is one that requires the API key
  if [[ "$uri" == "/version" || "$uri" == "/metrics" ]]; then
    extra_headers+=("-H" "x-api-key: $API_KEY")
  fi

  if [[ "$method" == "POST" ]]; then
    payload="$rest"
    # We expand "${extra_headers[@]}" here to inject the header if it exists
    response=$(curl -s -w "%{http_code}" -H "Content-Type: application/json" \
      -H "Authorization: Bearer $TOKEN" -H "X-Request-ID: $CURL_UUID" \
      "${extra_headers[@]}" \
      -d "$payload" "${BASE_URL}${uri}")
  else
    response=$(curl -s -w "%{http_code}" -H "Authorization: Bearer $TOKEN" \
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
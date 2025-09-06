#!/bin/bash

BASE_URL="http://ha2.mshome.net:80"
LOGIN_PAYLOAD='{"username":"mcordova","password":"basica"}'

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
  "GET /ping"
  "GET /customer?id=anatr"
  "GET /customer?id=quick"
  "GET /customer?id=bergs"
  "GET /customer?id=ernsh"
  "GET /customer?id=fissa"
  "GET /customer?id=dracd"
  "GET /customer?id=savea"
  "POST /sales {\"start_date\":\"1994-01-01\",\"end_date\":\"1996-12-31\"}"
  "POST /sales {\"start_date\":\"1994-01-01\",\"end_date\":\"1994-12-31\"}"
  "POST /sales {\"start_date\":\"1995-01-01\",\"end_date\":\"1995-12-31\"}"
  "POST /sales {\"start_date\":\"1996-01-01\",\"end_date\":\"1996-12-31\"}"
)

for entry in "${endpoints[@]}"; do
  IFS=' ' read -r method uri rest <<< "$entry"
  payload=""
  CURL_UUID="$(echo -n $(uuid))"
  if [[ "$method" == "POST" ]]; then
    payload="$rest"
    response=$(curl -s -w "%{http_code}" -H "Content-Type: application/json" \
      -H "Authorization: Bearer $TOKEN" -H "X-Request-ID: $CURL_UUID" -d "$payload" "${BASE_URL}${uri}")
  else
    response=$(curl -s -w "%{http_code}" -H "Authorization: Bearer $TOKEN" -H "X-Request-ID: $CURL_UUID" "${BASE_URL}${uri}")
  fi

  body="${response::-3}"
  status="${response: -3}"
  ok="false"
  [[ "$status" == "200" ]] && ok="true"

  show_result "$method $uri" "$status" "$ok" "$body"
done

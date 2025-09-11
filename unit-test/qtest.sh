# test server
BASE_URL="http://localhost:8080"
# call login and extract token
login_response=$(curl -s -w "%{http_code}" -H "Content-Type: application/json" \
  -H "X-Request-ID: $(echo -n $(uuid))" -d '{"username":"mcordova", "password":"basica"}' "${BASE_URL}/login")
login_body="${login_response::-3}"
login_status="${login_response: -3}"
TOKEN=$(echo "$login_body" | jq -r '.id_token')
# call secure API
curl "${BASE_URL}/rcustomer?id=ANATR" -s -H "Authorization: Bearer $TOKEN" -H "X-Request-ID: $(echo -n $(uuid))" | jq

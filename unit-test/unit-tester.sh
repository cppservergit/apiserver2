#!/bin/bash

# Usage: 
# ./test.sh [HOST] [URI_PREFIX]

# Configuration
# $1 = First argument. If empty, fallback to $HOST env var. If empty, default to https://cppserver.com
HOST="${1:-${HOST:-http://localhost:8080}}"

# $2 = Second argument. If empty, fallback to $URI_PREFIX env var. If empty, default to ""
URI_PREFIX="${2:-${URI_PREFIX:-}}"

LOGIN_ENDPOINT="$URI_PREFIX/login" # Adjusted assuming /api/login is fixed, or use $URI_PREFIX/login
CUSTOMER_ENDPOINT="$URI_PREFIX/customer"
LOGIN_PAYLOAD='{"username":"mcordova", "password":"basica"}'

# 1. Authenticate and get the token
echo "Authenticating and fetching token from $HOST$LOGIN_ENDPOINT..."
TOKEN1=$(curl -s -k --json "$LOGIN_PAYLOAD" "$HOST$LOGIN_ENDPOINT" | jq -r '.id_token')

if [ -z "$TOKEN1" ] || [ "$TOKEN1" == "null" ]; then
    echo "❌ Failed to obtain token. Exiting."
    exit 1
fi

echo "✅ Token acquired successfully."
echo ""

# 2. Print the table header
echo "✅ Test results completed for $CUSTOMER_ENDPOINT"
echo
printf "%-25s | %-8s | %-8s | %-6s\n" "Test Case" "Expected" "Actual" "Result"
printf "%-25s | %-8s | %-8s | %-6s\n" "-------------------------" "--------" "--------" "------"

# Variable to hold the successful JSON response for later
SUCCESS_JSON_BODY=""

# 3. Define the test runner function
run_test() {
    local test_name="$1"
    local payload="$2"
    local token="$3"
    local expected_status="$4"

    # Handle the optional Authorization header
    local auth_args=()
    if [ -n "$token" ]; then
        auth_args=(-H "Authorization: Bearer $token")
    fi

    # Execute curl
    local response
    response=$(curl -s -k -w "\n%{http_code}" "$HOST$CUSTOMER_ENDPOINT" --json "$payload" "${auth_args[@]}")

    # Extract the status code (the last line) and the body (everything except the last line)
    local actual_status=$(echo "$response" | tail -n1)
    local body=$(echo "$response" | sed '$d')

    # Evaluate the result
    local result_mark="❌ FAIL"
    if [ "$actual_status" == "$expected_status" ]; then
        result_mark="✅ OK"
    fi

    # Print the table row
    printf "%-25s | %-8s | %-8s | %-6s\n" "$test_name" "$expected_status" "$actual_status" "$result_mark"

    # If this is the correct case and it passed, save the body to print it later
    if [ "$test_name" == "Correct Case" ] && [ "$actual_status" == "200" ]; then
        SUCCESS_JSON_BODY="$body"
    fi
}

# 4. Execute the Test Cases
run_test "Correct Case" '{"id":"ANATR"}' "$TOKEN1" "200"
run_test "Correct Case Long Reply" '{"id":"ERNSH"}' "$TOKEN1" "200"
run_test "Customer Not Found" '{"id":"SIMON"}' "$TOKEN1" "404"
run_test "Missing argument" '{"id":""}' "$TOKEN1" "400"
run_test "Invalid argument" '{"id":"Invalid123"}' "$TOKEN1" "400"
run_test "Invalid Token" '{"id":"ANATR"}' "ey_invalid_token_12345" "401"
run_test "No Token" '{"id":"ANATR"}' "" "401"

# 5. Print the payload of the successful request
echo ""
if [ -n "$SUCCESS_JSON_BODY" ]; then
    echo "--- 200 OK JSON Output ---"
    echo "$SUCCESS_JSON_BODY" | jq .
else
    echo "--- No 200 OK output captured ---"
fi


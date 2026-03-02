#!/bin/bash

# Accept parameters for the wrapper
CONCURRENCY="${1:-10}"
BASE_URL="${2:-http://localhost:8080}"
API_PREFIX="${3:-}"

TEST_SCRIPT="./test-keepalive.sh"

# Ensure the test script exists and is executable
if [[ ! -x "$TEST_SCRIPT" ]]; then
  echo "Error: $TEST_SCRIPT not found or not executable."
  exit 1
fi

echo "Starting $CONCURRENCY concurrent instances of $TEST_SCRIPT..."

for (( i=1; i<=CONCURRENCY; i++ ))
do
  # Execute in the background and discard all output
  $TEST_SCRIPT "$BASE_URL" "$API_PREFIX" > /dev/null 2>&1 &
done

# Wait for all background jobs to complete
wait

echo "Stress test complete."
#!/bin/bash

# Configuration
MOCK_PORT=18131
PROXY_LISTEN="127.0.0.1:18130"
PROXY_TARGET="127.0.0.1:$MOCK_PORT"

cleanup() {
    echo "Cleaning up..."
    kill $(jobs -p) 2>/dev/null
    sleep 1.0 # Give them time to exit gracefully and flush logs
}
trap cleanup EXIT

# Kill any lingering instances to avoid port conflicts
killall -9 radius_proxy mock_radius 2>/dev/null
sleep 0.2

echo "--- Starting Mock RADIUS Server ---"
./tests/mock_radius $MOCK_PORT &
MOCK_PID=$!
sleep 0.5

echo "--- Starting RADIUS Proxy ---"
# Check if we should enable debug
PROXY_ARGS="$PROXY_LISTEN $PROXY_TARGET"
if [ "$MODE" == "leak" ]; then
    PROXY_ARGS="-d $PROXY_ARGS"
fi

./radius_proxy $PROXY_ARGS &
PROXY_PID=$!
sleep 0.5

# Ensure they are actually running
if ! kill -0 $PROXY_PID 2>/dev/null; then
    echo "ERROR: RADIUS Proxy failed to start"
    exit 1
fi

MODE=$1
COUNT=${2:-100} # Default to 100 for fast tests

if [ -z "$MODE" ] || [ "$MODE" == "unit" ]; then
    echo "--- Running Unit Tests ---"
    ./tests/unit_test
    if [ $? -ne 0 ]; then echo "Unit tests failed"; exit 1; fi
fi

if [ "$MODE" == "stress" ] || [ "$MODE" == "leak" ]; then
    echo "--- Running Stress Test ($COUNT requests) ---"
    ./tests/stress_test 127.0.0.1 18130 $COUNT
    if [ $? -ne 0 ]; then echo "Stress test failed"; exit 1; fi
fi

if [ "$MODE" == "perf" ]; then
    echo "--- Running Performance Test ($COUNT requests) ---"
    ./tests/perf_test 127.0.0.1 18130 $COUNT
    if [ $? -ne 0 ]; then echo "Performance test failed"; exit 1; fi
fi

if [ "$MODE" == "profile" ]; then
    echo "--- Running Profiling (Callgrind, $COUNT requests) ---"
    # Restart proxy under valgrind
    kill $PROXY_PID 2>/dev/null
    wait $PROXY_PID 2>/dev/null
    
    valgrind --tool=callgrind --callgrind-out-file=callgrind.out ./radius_proxy $PROXY_LISTEN $PROXY_TARGET &
    PROXY_VALGRIND_PID=$!
    sleep 2 # Valgrind is slow to start
    
    ./tests/perf_test 127.0.0.1 18130 $COUNT
    
    echo "Dumping callgrind data for PID $PROXY_VALGRIND_PID..."
    callgrind_control -d $PROXY_VALGRIND_PID >/dev/null 2>&1
    
    echo "Waiting for proxy to finish (sending SIGTERM)..."
    kill -TERM $PROXY_VALGRIND_PID
    wait $PROXY_VALGRIND_PID 2>/dev/null
    
    echo "--- Profiling Results (Top 40 lines) ---"
    callgrind_annotate --inclusive=yes --auto=yes callgrind.out | head -n 40
fi

echo "Success!"

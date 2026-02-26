#!/bin/bash

SERVER_SOURCE_DIR=".."
SERVER_BIN="$SERVER_SOURCE_DIR/build/http_server"
DOC_ROOT="../www"
LOG_DIR="../log"
LOG_FILE="$LOG_DIR/bench_server.log"
RESULT_FILE="./results.csv"
TEST_FILE="file_10m.bin"

REQUESTS=3000 
WORKER_COUNTS=(1 2 4 8)
CONCURRENCY_LEVELS=(1 10 50 100 200 300 400 500 600 700 800 900 1000)

cleanup_server() {
    pkill -9 -f "http_server" >/dev/null 2>&1
    killall -9 http_server >/dev/null 2>&1
    sleep 0.5
}

if [ ! -f "$SERVER_BIN" ]; then
    echo "Error: Server binary not found at $SERVER_BIN"
    exit 1
fi

if [ ! -f "$DOC_ROOT/$TEST_FILE" ]; then
    echo "Test file not found. Running generator..."
    ./gen_files.sh
    
    if [ ! -f "$DOC_ROOT/$TEST_FILE" ]; then
        echo "Error: Failed to create $DOC_ROOT/$TEST_FILE"
        exit 1
    fi
fi

# CSV
echo "Workers,Concurrency,RPS,TransferRate_KBps,TimePerRequest_ms" > $RESULT_FILE

for w in "${WORKER_COUNTS[@]}"; do
    echo "Testing with WORKERS = $w"

    cleanup_server
    $SERVER_BIN --port 8081 --root "$DOC_ROOT" --workers $w --log "$LOG_FILE" &
    SERVER_PID=$!
    
    sleep 2

    # не упал ли сервер сразу
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo "Server failed to start! Check $LOG_FILE"
        exit 1
    fi

    for c in "${CONCURRENCY_LEVELS[@]}"; do
        current_req=$REQUESTS
        if [ $c -gt $current_req ]; then
            current_req=$c
        fi

        echo -n "  Running ab with -c $c ... "

        OUTPUT=$(ab -n $current_req -c $c -r -k http://127.0.0.1:8081/$TEST_FILE 2>&1)

        NON_200=$(echo "$OUTPUT" | grep "Non-2xx responses:")
        if [ ! -z "$NON_200" ]; then
            echo "WARNING: Server returned errors (404/403/500)"
        fi

        RPS=$(echo "$OUTPUT" | grep "Requests per second:" | awk '{print $4}')
        TRATE=$(echo "$OUTPUT" | grep "Transfer rate:" | awk '{print $3}')
        TPREQ=$(echo "$OUTPUT" | grep "Time per request:" | grep "(mean)" | head -n 1 | awk '{print $4}')

        if [ -z "$RPS" ]; then
            RPS="0"
            TRATE="0"
            TPREQ="0"
            echo "FAILED (ab error)"
            sleep 1
        else
            echo "Done RPS: $RPS"
        fi

        echo "$w,$c,$RPS,$TRATE,$TPREQ" >> $RESULT_FILE
        
        if [ $c -ge 500 ]; then
            sleep 1
        else
            sleep 0.5
        fi
    done

    kill $SERVER_PID
    wait $SERVER_PID 2>/dev/null
    echo
    sleep 1
done

echo "Benchmark finished"

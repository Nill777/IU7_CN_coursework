#!/bin/bash

mkdir -p build log
g++ -std=c++17 -Wall *.cpp -o build/http_server

if [ $? -eq 0 ]; then
    echo "Server on port 8080 with 4 workers"
    ./build/http_server --port 8080 --root ./www --workers 4 --log log/server.log
else
    exit 1
fi

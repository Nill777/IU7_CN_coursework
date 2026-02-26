#!/bin/bash

echo "Generating 10MB test file..."
dd if=/dev/urandom of=../www/file_10m.bin bs=1M count=10 status=progress
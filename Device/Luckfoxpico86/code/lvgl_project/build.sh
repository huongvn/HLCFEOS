#!/bin/bash
set -e
cd "$(dirname "$0")"
make clean && make
echo ""
echo "Build complete: ota/app_v1.1.0"
ls -lh ota/app_v1.1.0

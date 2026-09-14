#!/usr/bin/env bash
set -e

echo "=== CMake Configuration ==="
cmake --preset Debug

echo "=== Build ==="
cmake --build --preset Debug

echo "=== Build Complete ==="
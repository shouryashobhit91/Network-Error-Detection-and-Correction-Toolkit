#!/bin/sh
# build.sh - builds all programs on Linux / macOS (C++17).
set -e
CXX=${CXX:-g++}
mkdir -p bin results graphs
CODES="src/codes.cpp src/errors.cpp"
NET="src/codes.cpp src/errors.cpp src/sha256.cpp src/link.cpp"
FLAGS="-std=c++17 -O2 -Wall -Wextra -Isrc -pthread"
$CXX $FLAGS $CODES src/toolkit_main.cpp -o bin/edc_toolkit
for p in sender receiver channel bench; do $CXX $FLAGS $NET src/${p}_main.cpp -o bin/edc_$p; done
$CXX $FLAGS $NET tests/test_main.cpp -o bin/edc_tests
echo "Done. Programs are in ./bin"

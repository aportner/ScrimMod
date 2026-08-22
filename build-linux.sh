#!/usr/bin/env bash

set -euo pipefail

cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release

echo
file build/linux-release/scrimmod_mm_i386.so

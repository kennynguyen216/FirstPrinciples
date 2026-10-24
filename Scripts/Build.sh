#!/usr/bin/env bash
# Generates makefiles and builds. Usage: Scripts/Build.sh [debug|release]
set -e
cd "$(dirname "$0")/.."
CONFIG="${1:-debug}"
premake5 gmake
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
make -C Build config="$CONFIG" -j"$JOBS"

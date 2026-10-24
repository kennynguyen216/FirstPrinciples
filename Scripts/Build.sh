#!/usr/bin/env bash
# Generates makefiles and builds a project. Usage: Scripts/Build.sh [debug|release] [Projects/Name]
set -e
cd "$(dirname "$0")/.."
CONFIG="${1:-debug}"
PROJECT="${2:-Projects/Sample}"
premake5 --project="$PROJECT" gmake
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
make -C "$PROJECT/Build" config="$CONFIG" -j"$JOBS"

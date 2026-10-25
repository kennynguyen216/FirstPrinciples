#!/usr/bin/env bash
# Generates makefiles and builds a project. Pass --clean to discard cached build output.
# Usage: Scripts/Build.sh [debug|release] [Projects/Name] [--clean]
set -e
cd "$(dirname "$0")/.."
CONFIG="${1:-debug}"
PROJECT="${2:-Projects/Sample}"
if [[ $# -gt 3 || ( -n "${3:-}" && "${3:-}" != "--clean" ) ]]; then
    echo "Usage: Scripts/Build.sh [debug|release] [Projects/Name] [--clean]" >&2
    exit 2
fi
premake5 --project="$PROJECT" gmake
if [[ "${3:-}" == "--clean" ]]; then
    make -C "$PROJECT/Build" config="$CONFIG" clean
fi
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
make -C "$PROJECT/Build" config="$CONFIG" -j"$JOBS"

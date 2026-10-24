#!/usr/bin/env bash
# Fetches submodules and installs the macOS dependencies with Homebrew.
set -e
command -v brew >/dev/null || { echo "Install Homebrew first: https://brew.sh"; exit 1; }
git -C "$(dirname "$0")/.." submodule update --init
brew install premake sdl3 qt
echo "Done. Run Scripts/Build.sh to build."

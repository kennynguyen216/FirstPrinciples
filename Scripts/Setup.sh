#!/usr/bin/env bash
# Installs the macOS dependencies with Homebrew.
set -e
command -v brew >/dev/null || { echo "Install Homebrew first: https://brew.sh"; exit 1; }
brew install premake sdl3 qt
echo "Done. Run Scripts/Build.sh to build."

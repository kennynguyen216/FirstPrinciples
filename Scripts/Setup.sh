#!/usr/bin/env bash
# Fetches submodules and installs the macOS dependencies with Homebrew.
set -e
command -v brew >/dev/null || { echo "Install Homebrew first: https://brew.sh"; exit 1; }
git -C "$(dirname "$0")/.." submodule update --init
brew install premake sdl3 qt sevenzip
if [ "$(uname -m)" = "arm64" ]; then
	"$(dirname "$0")/GetUltralight.sh"
else
	echo "Ultralight has no Intel Mac SDK; building without HTML game UI. Audio and the Qt editor remain available."
fi
echo "Done. Run Scripts/Build.sh to build."

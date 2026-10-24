#!/usr/bin/env bash
# Generates an Xcode workspace in Build/.
set -e
cd "$(dirname "$0")/.."
premake5 xcode4
echo "Open Build/FirstPrinciples.xcworkspace"

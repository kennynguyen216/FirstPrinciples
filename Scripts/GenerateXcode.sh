#!/usr/bin/env bash
# Generates an Xcode workspace in the project's Build folder. Usage: Scripts/GenerateXcode.sh [Projects/Name]
set -e
cd "$(dirname "$0")/.."
PROJECT="${1:-Projects/Sample}"
premake5 --project="$PROJECT" xcode4
echo "Open $PROJECT/Build/$(basename "$PROJECT").xcworkspace"

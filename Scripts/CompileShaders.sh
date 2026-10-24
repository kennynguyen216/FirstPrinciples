#!/usr/bin/env bash
# Compiles the engine base shaders to SPIR-V (Vulkan) and MSL (Metal).
# Only engine developers need this. The compiled files are committed so game developers do not.
# Needs glslangValidator and spirv-cross (brew install glslang spirv-cross).
set -e
cd "$(dirname "$0")/.."
SRC=src/Engine/Shaders
OUT=Content/Engine/Shaders
mkdir -p "$OUT"
for file in "$SRC"/*.vert "$SRC"/*.frag; do
	name=$(basename "$file")
	glslangValidator -V --quiet -I"$SRC" -o "$OUT/$name.spv" "$file"
	spirv-cross "$OUT/$name.spv" --msl --msl-version 20100 --output "$OUT/$name.msl"
	echo "compiled $name"
done

#!/usr/bin/env bash
# Compiles the engine base shaders to SPIR-V (Vulkan) and MSL (Metal).
# Only engine developers need this. The compiled files are committed so game developers do not.
# Needs glslangValidator and spirv-cross (brew install glslang spirv-cross).
set -e
cd "$(dirname "$0")/.."
SRC=src/Engine/Shaders
OUT=Engine/Content/Shaders
mkdir -p "$OUT"
for file in "$SRC"/*.vert "$SRC"/*.frag "$SRC"/*.comp; do
	name=$(basename "$file")
	glslangValidator -V --quiet -I"$SRC" -o "$OUT/$name.spv" "$file"
	metalInput="$OUT/$name.spv"
	if [[ "$file" == *.comp || "$name" == Lit.frag || "$name" == Glass.frag || "$name" == Skybox.frag || "$name" == BlackHole.frag ]]; then
		metalInput=$(mktemp)
		glslangValidator -V --quiet -DFP_METAL=1 -I"$SRC" -o "$metalInput" "$file"
	fi
	spirv-cross "$metalInput" --msl --msl-version 20100 --msl-decoration-binding --output "$OUT/$name.msl"
	if [[ "$file" == *.comp || "$name" == Lit.frag || "$name" == Glass.frag || "$name" == Skybox.frag || "$name" == BlackHole.frag ]]; then
		# Normalize generated text so shader diffs contain no trailing whitespace.
		awk '{ sub(/[ \t\r]+$/, ""); lines[NR]=$0; if (length($0)) last=NR } END { for (i=1; i<=last; ++i) print lines[i] }' "$OUT/$name.msl" > "$metalInput"
		mv "$metalInput" "$OUT/$name.msl"
	fi
	echo "compiled $name"
done

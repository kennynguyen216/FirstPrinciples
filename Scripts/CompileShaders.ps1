# Compiles the engine base shaders to SPIR-V (Vulkan) and MSL (Metal).
# Only engine developers need this. The compiled files are committed so game developers do not.
# Needs glslangValidator and spirv-cross on PATH. Both ship with the Vulkan SDK.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Src = Join-Path $Root "src/Engine/Shaders"
$Out = Join-Path $Root "Engine/Content/Shaders"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Get-ChildItem -Path $Src -Include *.vert, *.frag -File -Recurse | ForEach-Object {
    $spv = Join-Path $Out ($_.Name + ".spv")
    $msl = Join-Path $Out ($_.Name + ".msl")
    & glslangValidator -V --quiet "-I$Src" -o $spv $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "glslangValidator failed on $($_.Name)" }
    & spirv-cross $spv --msl --msl-version 20100 --msl-decoration-binding --output $msl
    if ($LASTEXITCODE -ne 0) { throw "spirv-cross failed on $($_.Name)" }
    Write-Host "compiled $($_.Name)"
}

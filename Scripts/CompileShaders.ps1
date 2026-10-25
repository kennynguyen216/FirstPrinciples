# Compiles the engine base shaders to SPIR-V (Vulkan) and MSL (Metal).
# Only engine developers need this. The compiled files are committed so game developers do not.
# Needs glslangValidator and spirv-cross on PATH. Both ship with the Vulkan SDK.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Src = Join-Path $Root "src/Engine/Shaders"
$Out = Join-Path $Root "Engine/Content/Shaders"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Get-ChildItem -Path $Src -Include *.vert, *.frag, *.comp -File -Recurse | ForEach-Object {
    $spv = Join-Path $Out ($_.Name + ".spv")
    $msl = Join-Path $Out ($_.Name + ".msl")
    & glslangValidator -V --quiet "-I$Src" -o $spv $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "glslangValidator failed on $($_.Name)" }
    $metalInput = $spv
    if ($_.Extension -eq ".comp" -or $_.Name -in @("Lit.frag", "Glass.frag", "Skybox.frag")) {
        $metalInput = [System.IO.Path]::GetTempFileName()
        & glslangValidator -V --quiet -DFP_METAL=1 "-I$Src" -o $metalInput $_.FullName
        if ($LASTEXITCODE -ne 0) { throw "Metal GLSL compilation failed on $($_.Name)" }
    }
    & spirv-cross $metalInput --msl --msl-version 20100 --msl-decoration-binding --output $msl
    if ($LASTEXITCODE -ne 0) { throw "spirv-cross failed on $($_.Name)" }
    if ($metalInput -ne $spv) {
        Remove-Item $metalInput
        $normalized = ((Get-Content $msl | ForEach-Object { $_.TrimEnd() }) -join "`n").TrimEnd() + "`n"
        [System.IO.File]::WriteAllText($msl, $normalized, [System.Text.UTF8Encoding]::new($false))
    }
    Write-Host "compiled $($_.Name)"
}

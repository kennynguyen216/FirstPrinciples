# Compiles the engine base shaders to SPIR-V (Vulkan) and MSL (Metal).
# Only engine developers need this. The compiled files are committed so game developers do not.
# Needs Python 3, glslangValidator, spirv-opt and spirv-cross on PATH.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Src = Join-Path $Root "src/Engine/Shaders"
$Out = Join-Path $Root "Engine/Content/Shaders"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$RayQueryPasses = @("DIACache", "DIATrace", "DIATraceLitOne", "DIAResolve", "DIAResolveFallback", "DIAResolveFallbackLit")
Get-ChildItem -Path $Src -Include *.vert, *.frag, *.comp -File -Recurse | ForEach-Object {
    $spv = Join-Path $Out ($_.Name + ".spv")
    $msl = Join-Path $Out ($_.Name + ".msl")
    & glslangValidator -V --quiet "-I$Src" -o $spv $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "glslangValidator failed on $($_.Name)" }
    if ($_.BaseName.StartsWith("DIA")) {
        & python (Join-Path $PSScriptRoot "OptimizeDIAShader.py") $spv
        if ($LASTEXITCODE -ne 0) { throw "DIA optimization failed on $($_.Name)" }
    }
    $metalInput = $spv
    if ($_.Extension -eq ".comp" -or $_.Name -in @("Lit.frag", "Glass.frag", "Skybox.frag", "BlackHole.frag")) {
        $metalInput = [System.IO.Path]::GetTempFileName()
        & glslangValidator -V --quiet -DFP_METAL=1 "-I$Src" -o $metalInput $_.FullName
        if ($LASTEXITCODE -ne 0) { throw "Metal GLSL compilation failed on $($_.Name)" }
    }
    & spirv-cross $metalInput --msl --msl-version 20100 --msl-decoration-binding --output $msl
    if ($LASTEXITCODE -ne 0) { throw "spirv-cross failed on $($_.Name)" }
    if ($metalInput -ne $spv) {
        Remove-Item -LiteralPath $metalInput
        $normalized = ((Get-Content $msl | ForEach-Object { $_.TrimEnd() }) -join "`n").TrimEnd() + "`n"
        [System.IO.File]::WriteAllText($msl, $normalized, [System.Text.UTF8Encoding]::new($false))
    }
    $storageBase = switch ($_.BaseName) { "DIAHistory" { 11 } "DIADenoise" { 8 } "DIAResolve" { 8 } "DIAResolveFallback" { 8 } "DIAResolveFallbackLit" { 8 } default { 1 } }
    if ($storageBase -gt 1) {
        $source = [System.IO.File]::ReadAllText($msl)
        for ($binding = $storageBase; $binding -lt $storageBase + 8; ++$binding) {
            $source = $source.Replace("[[buffer($binding)]]", "[[buffer($($binding - $storageBase + 1))]]")
        }
        [System.IO.File]::WriteAllText($msl, $source)
    }
    Write-Host "compiled $($_.Name)"
    if ($RayQueryPasses -contains $_.BaseName) {
        $raySpv = Join-Path $Out ($_.BaseName + "RT.frag.spv")
        & glslangValidator -V --target-env vulkan1.2 --glsl-version 460 --quiet "-I$Src" -DDIA_HARDWARE_RT=1 -o $raySpv $_.FullName
        if ($LASTEXITCODE -ne 0) { throw "Ray query compilation failed on $($_.Name)" }
        & python (Join-Path $PSScriptRoot "OptimizeDIAShader.py") $raySpv
        if ($LASTEXITCODE -ne 0) { throw "Ray query optimization failed on $($_.Name)" }
        & spirv-val --target-env vulkan1.2 $raySpv
        if ($LASTEXITCODE -ne 0) { throw "Ray query validation failed on $($_.Name)" }
        Write-Host "compiled $($_.BaseName)RT.frag"
    }
}

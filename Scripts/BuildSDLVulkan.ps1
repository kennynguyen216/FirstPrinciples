param([switch]$Install)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Commit = '829a65d769d935c4852f8159e964312c0957260a'
$ArchiveHash = 'E7C36F1B30D8C0BE27C0497DA46F8F55D3226BF6E02570FB39131AE8BD22D7C3'
$Working = [IO.Path]::GetFullPath((Join-Path $Root 'Intermediate/HardwareRT'))
$Source = Join-Path $Working 'SDL'
$Build = Join-Path $Working 'SDL-build'
$Archive = Join-Path $Working "SDL-$Commit.zip"
New-Item -ItemType Directory -Force -Path $Working | Out-Null

if (-not (Test-Path -LiteralPath (Join-Path $Source 'CMakeLists.txt'))) {
    if (-not (Test-Path -LiteralPath $Archive)) {
        Invoke-WebRequest -Uri "https://github.com/libsdl-org/SDL/archive/$Commit.zip" -OutFile $Archive
    }
    if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $ArchiveHash) { throw 'SDL source archive checksum mismatch' }
    $Extract = Join-Path $Working 'source-unpacked'
    Expand-Archive -LiteralPath $Archive -DestinationPath $Extract -Force
    $Folders = @(Get-ChildItem -LiteralPath $Extract -Directory)
    if ($Folders.Count -ne 1) { throw 'Expected one SDL source root' }
    $From = [IO.Path]::GetFullPath($Folders[0].FullName)
    $To = [IO.Path]::GetFullPath($Source)
    if (-not $From.StartsWith($Working + [IO.Path]::DirectorySeparatorChar) -or
        -not $To.StartsWith($Working + [IO.Path]::DirectorySeparatorChar)) { throw 'SDL source path escaped its workspace' }
    Move-Item -LiteralPath $From -Destination $To
}

& python (Join-Path $PSScriptRoot 'PatchSDLVulkan.py') $Source --self-check
if ($LASTEXITCODE -ne 0) { throw 'SDL bridge patch validation failed' }
& python (Join-Path $PSScriptRoot 'PatchSDLVulkan.py') $Source
if ($LASTEXITCODE -ne 0) { throw 'SDL bridge patch failed' }
& cmake -S $Source -B $Build -A x64 -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_DOCS=OFF '-DSDL_VENDOR_INFO=FirstPrinciples ray queries' '-DCMAKE_SHARED_LINKER_FLAGS_RELEASE=/INCREMENTAL:NO'
if ($LASTEXITCODE -ne 0) { throw 'SDL configure failed' }
& cmake --build $Build --config Release --target SDL3-shared --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'SDL build failed' }

if ($Install) {
    $Destination = Join-Path $Root 'ThirdParty/SDL3/lib/x64'
    $Backup = Join-Path $Working 'StockSDL'
    New-Item -ItemType Directory -Force -Path $Backup | Out-Null
    foreach ($Name in @('SDL3.dll', 'SDL3.lib')) {
        $Current = Join-Path $Destination $Name
        $Saved = Join-Path $Backup $Name
        if (-not (Test-Path -LiteralPath $Saved)) { Copy-Item -LiteralPath $Current -Destination $Saved }
        Copy-Item -LiteralPath (Join-Path $Build "Release/$Name") -Destination $Current -Force
    }
    Write-Host 'Installed the local SDL Vulkan bridge into ThirdParty'
} else {
    Write-Host "Built $Build/Release/SDL3.dll"
}

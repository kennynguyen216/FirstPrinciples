# Downloads the free Ultralight SDK into ThirdParty\Ultralight. Game UI is drawn with it.
# The version and checksum are pinned so every machine gets the same files.
# Unpacks with the tar that ships with Windows 10 and 11, or 7-Zip when it is installed.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Version = "2.0.0-beta.2"
$Platform = "windows-x64"
$Sha = "e17b3ca2f3c2063f1d14971e7ef756f28e71f2d426a91d2e21c5236d2dc67e6d"
$Dest = Join-Path $Root "ThirdParty\Ultralight"
$Stamp = Join-Path $Dest "version.txt"

if ((Test-Path $Stamp) -and ((Get-Content $Stamp) -eq "$Version-$Platform")) {
    Write-Host "Ultralight $Version is already in $Dest"
    exit 0
}

$Work = Join-Path $env:TEMP ("ultralight-" + [Guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $Work | Out-Null
try {
    Write-Host "Downloading Ultralight $Version for $Platform..."
    $Info = Invoke-RestMethod "https://ultralig.ht/api/v1/sdk/download?platform=$Platform&version=$Version&format=json"
    $Archive = Join-Path $Work "sdk.7z"
    Invoke-WebRequest -Uri $Info.url -OutFile $Archive
    if ((Get-FileHash $Archive -Algorithm SHA256).Hash.ToLower() -ne $Sha) { throw "The download does not match the expected checksum." }

    $Out = Join-Path $Work "sdk"
    New-Item -ItemType Directory -Force -Path $Out | Out-Null
    $SevenZip = Get-Command 7z -ErrorAction SilentlyContinue
    if ($SevenZip) { & $SevenZip.Source x -y "-o$Out" $Archive | Out-Null }
    else { tar -xf $Archive -C $Out }
    if ($LASTEXITCODE -ne 0) { throw "Could not unpack the SDK. Install 7-Zip and try again." }

    # The archive may hold one top folder. Its contents go straight into ThirdParty\Ultralight.
    $Items = @(Get-ChildItem $Out)
    $Source = if ($Items.Count -eq 1 -and $Items[0].PSIsContainer) { $Items[0].FullName } else { $Out }
    if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $Dest | Out-Null
    Copy-Item (Join-Path $Source "*") $Dest -Recurse
    Set-Content $Stamp "$Version-$Platform"
    Write-Host "Ultralight $Version is in $Dest"
}
finally {
    Remove-Item $Work -Recurse -Force -ErrorAction SilentlyContinue
}

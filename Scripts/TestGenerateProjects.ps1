# Run after Setup.ps1 and installing an MSVC Qt kit. Checks the Windows generator's failure and success paths.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$OriginalQtDir = $env:QT_DIR
Push-Location $env:TEMP
try {
    $env:QT_DIR = Join-Path $Root "ThirdParty\not-a-qt-kit"
    $output = & cmd /c "`"$Root\Scripts\GenerateProjects.bat`"" 2>&1
    if ($LASTEXITCODE -eq 0 -or "$output" -notmatch "QT_DIR must point to") {
        throw "Invalid Qt kit must fail project generation with a useful error."
    }

    $env:QT_DIR = $OriginalQtDir
    $output = & cmd /c "`"$Root\Scripts\GenerateProjects.bat`"" 2>&1
    if ($LASTEXITCODE -ne 0 -or "$output" -notmatch "Using Qt:") {
        throw "Project generation failed to find an MSVC Qt kit: $output"
    }
    if ("$output" -notmatch 'Open Build\\(FirstPrinciples\.slnx?)') {
        throw "Project generation did not report its solution."
    }
    if (-not (Test-Path (Join-Path $Root "Build\$($Matches[1])"))) {
        throw "Reported solution does not exist."
    }
    Write-Host "Windows project generation checks passed."
}
finally {
    $env:QT_DIR = $OriginalQtDir
    Pop-Location
}

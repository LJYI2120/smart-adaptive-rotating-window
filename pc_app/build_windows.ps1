$ErrorActionPreference = "Stop"

$AppRoot = $PSScriptRoot
$Venv = Join-Path $AppRoot ".venv"
$Python = Join-Path $Venv "Scripts\python.exe"

if (-not (Test-Path -LiteralPath $Python)) {
    python -m venv $Venv
}

& $Python -m pip install --disable-pip-version-check -r (Join-Path $AppRoot "requirements-build.txt")

Push-Location $AppRoot
try {
    & $Python -m PyInstaller `
        --noconfirm `
        --clean `
        --windowed `
        --name "SmartWindowController" `
        --paths "$AppRoot\src" `
        --distpath "$AppRoot\dist" `
        --workpath "$AppRoot\build" `
        --specpath "$AppRoot\build" `
        --exclude-module PySide6.QtWebEngineCore `
        --exclude-module PySide6.QtWebEngineWidgets `
        --exclude-module PySide6.QtWebEngineQuick `
        --exclude-module PySide6.Qt3DCore `
        --exclude-module PySide6.QtMultimedia `
        (Join-Path $AppRoot "launcher.py")
} finally {
    Pop-Location
}

$OutputDir = Join-Path $AppRoot "dist\SmartWindowController"
$NoticeSource = Join-Path (Split-Path $AppRoot -Parent) "THIRD_PARTY_NOTICES.md"
if (Test-Path -LiteralPath $NoticeSource) {
    Copy-Item -LiteralPath $NoticeSource -Destination $OutputDir -Force
}

Write-Host "Build complete: $OutputDir\SmartWindowController.exe"

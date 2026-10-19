$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location (Join-Path $root "ui")
npm install
npm run build
Set-Location $root
cmake -S . -B build
cmake --build build --config Release
Write-Host "Готово: $root\build\Release\helios.exe или $root\build\helios.exe"

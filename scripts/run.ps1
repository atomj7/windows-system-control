#Requires -Version 5.1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = @(
    (Join-Path $root "build\Release\helios.exe"),
    (Join-Path $root "build\helios.exe")
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    Write-Host "Сначала собери: .\scripts\build.ps1"
    exit 1
}
Set-Location $root
& $exe --hardware @args

#Requires -Version 5.1
$ErrorActionPreference = "SilentlyContinue"
$dest = Join-Path $env:LOCALAPPDATA "Helios"
$programs = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Helios"
$desktop = Join-Path ([Environment]::GetFolderPath("Desktop")) "Helios.lnk"
Remove-Item $desktop -Force
Remove-Item $programs -Recurse -Force
Get-Process helios -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
Remove-Item $dest -Recurse -Force
Write-Host "Helios удалён."

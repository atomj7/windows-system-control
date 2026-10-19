#Requires -Version 5.1
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here

function Find-Payload {
    $candidates = @(
        (Join-Path $here "payload\helios.exe"),
        (Join-Path $here "helios.exe"),
        (Join-Path $root "pack\windows\helios.exe"),
        (Join-Path $root "build\Release\helios.exe"),
        (Join-Path $root "build\helios.exe")
    )
    foreach ($exe in $candidates) {
        if (Test-Path $exe) { return (Get-Item $exe) }
    }
    return $null
}

$exe = Find-Payload
if (-not $exe) {
    Write-Host "Нет helios.exe рядом со скриптом."
    Write-Host "Либо запусти Helios-Setup.exe, либо сначала .\scripts\build.ps1"
    exit 1
}

$payload = $exe.Directory.FullName
$web = Join-Path $payload "ui\dist\index.html"
if (-not (Test-Path $web)) {
    $alt = Join-Path $root "ui\dist\index.html"
    if (Test-Path $alt) {
        $webRoot = Join-Path $root "ui\dist"
    } else {
        Write-Host "Нет интерфейса ui\dist. Собери: cd ui; npm install; npm run build"
        exit 1
    }
} else {
    $webRoot = Join-Path $payload "ui\dist"
}

$dest = Join-Path $env:LOCALAPPDATA "Helios"
Write-Host "Ставлю Helios в $dest"
New-Item -ItemType Directory -Force -Path (Join-Path $dest "ui\dist") | Out-Null
Copy-Item $exe.FullName (Join-Path $dest "helios.exe") -Force
Copy-Item (Join-Path $here "Helios.cmd") (Join-Path $dest "Helios.cmd") -Force -ErrorAction SilentlyContinue
if (-not (Test-Path (Join-Path $dest "Helios.cmd"))) {
    @"
@echo off
cd /d "%~dp0"
start "" "%~dp0helios.exe" --hardware
"@ | Set-Content -Path (Join-Path $dest "Helios.cmd") -Encoding Ascii
}
Copy-Item -Path (Join-Path $webRoot "*") -Destination (Join-Path $dest "ui\dist") -Recurse -Force
Copy-Item (Join-Path $here "uninstall.ps1") (Join-Path $dest "uninstall.ps1") -Force -ErrorAction SilentlyContinue

$wsh = New-Object -ComObject WScript.Shell
$programs = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Helios"
New-Item -ItemType Directory -Force -Path $programs | Out-Null
$lnk = $wsh.CreateShortcut((Join-Path $programs "Helios.lnk"))
$lnk.TargetPath = Join-Path $dest "helios.exe"
$lnk.Arguments = "--hardware"
$lnk.WorkingDirectory = $dest
$lnk.WindowStyle = 1
$lnk.Description = "Helios — свет и воздух"
$lnk.Save()

$desktop = [Environment]::GetFolderPath("Desktop")
$desk = $wsh.CreateShortcut((Join-Path $desktop "Helios.lnk"))
$desk.TargetPath = Join-Path $dest "helios.exe"
$desk.Arguments = "--hardware"
$desk.WorkingDirectory = $dest
$desk.WindowStyle = 1
$desk.Description = "Helios — свет и воздух"
$desk.Save()

Write-Host "Готово. Ярлык: Пуск → Helios и на рабочем столе."
Write-Host "Запуск: $dest\Helios.cmd"
Write-Host "Удаление: powershell -File `"$dest\uninstall.ps1`""

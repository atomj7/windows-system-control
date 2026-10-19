#Requires -Version 5.1
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Need($name, $ok, $hint) {
    if ($ok) {
        Write-Host "[ok] $name"
        return
    }
    Write-Host "[нет] $name"
    Write-Host $hint
    exit 1
}

Write-Host "Helios: сборка Windows"
Write-Host "каталог: $root"

$node = Get-Command node -ErrorAction SilentlyContinue
Need "Node.js 20+" ($null -ne $node) "Поставь Node.js LTS: https://nodejs.org/en/download и закрой/открой PowerShell."

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
Need "CMake" ($null -ne $cmake) "Поставь CMake и отметь Add CMake to PATH: https://cmake.org/download/"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) { $vsPath = $vsPath.Trim() }
}
$mingw = Get-Command g++ -ErrorAction SilentlyContinue
Need "компилятор C++" ($vsPath -or $mingw) @"
Нужен Visual Studio 2022 с рабочей нагрузкой «Разработка классических приложений на C++»:
https://visualstudio.microsoft.com/vs/community/
Или MinGW-w64 (g++), если VS ставить не хочешь.
"@

Write-Host "интерфейс: npm install && npm run build"
Set-Location (Join-Path $root "ui")
npm install
if ($LASTEXITCODE -ne 0) { throw "npm install не прошёл" }
npm run build
if ($LASTEXITCODE -ne 0) { throw "npm run build не прошёл" }
Set-Location $root

$buildDir = Join-Path $root "build"
if ($vsPath) {
    Write-Host "C++: Visual Studio ($vsPath)"
    cmake -S $root -B $buildDir -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) {
        cmake -S $root -B $buildDir -A x64
    }
    if ($LASTEXITCODE -ne 0) { throw "cmake configure не прошёл" }
    cmake --build $buildDir --config Release --parallel
} else {
    Write-Host "C++: MinGW ($($mingw.Source))"
    cmake -S $root -B $buildDir -G "MinGW Makefiles"
    if ($LASTEXITCODE -ne 0) { throw "cmake configure не прошёл" }
    cmake --build $buildDir --parallel
}
if ($LASTEXITCODE -ne 0) { throw "сборка C++ не прошла" }

$candidates = @(
    (Join-Path $buildDir "Release\helios.exe"),
    (Join-Path $buildDir "helios.exe")
)
$exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) { throw "helios.exe не найден после сборки" }

Write-Host ""
Write-Host "Готово: $exe"
Write-Host "Запуск из корня репозитория:"
Write-Host "  `"$exe`" --hardware"
Write-Host "Стенд без железа:"
Write-Host "  `"$exe`" --simulate"
Write-Host "Интерфейс: http://127.0.0.1:47621"

#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

if [[ ! -d ui/node_modules ]]; then
  (cd ui && npm install)
fi
(cd ui && npm run build)

cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE="$root/cmake/mingw.cmake"
cmake --build build-win -j --target helios

stage="$root/installer/payload"
rm -rf "$stage"
mkdir -p "$stage/ui/dist"
cp "$root/build-win/helios.exe" "$stage/helios.exe"
cp "$root/installer/Helios.cmd" "$stage/Helios.cmd"
cp -a "$root/ui/dist/." "$stage/ui/dist/"

x86_64-w64-mingw32-objdump -p "$stage/helios.exe" | grep -i 'DLL Name' || true

(cd "$root/installer" && makensis helios.nsi)

zip_root="$root/pack"
rm -rf "$zip_root/Helios-windows"
mkdir -p "$zip_root/Helios-windows"
cp "$stage/helios.exe" "$zip_root/Helios-windows/"
cp "$root/installer/Helios.cmd" "$zip_root/Helios-windows/"
cp "$root/installer/install.cmd" "$zip_root/Helios-windows/"
cp "$root/installer/install.ps1" "$zip_root/Helios-windows/"
cp "$root/installer/uninstall.ps1" "$zip_root/Helios-windows/"
mkdir -p "$zip_root/Helios-windows/ui/dist"
cp -a "$stage/ui/dist/." "$zip_root/Helios-windows/ui/dist/"
cp "$root/installer/Helios-Setup.exe" "$zip_root/Helios-Setup.exe"
(cd "$zip_root" && rm -f Helios-windows.zip && zip -r Helios-windows.zip Helios-windows)

echo "Установщик: $root/installer/Helios-Setup.exe"
echo "Портативный zip: $root/pack/Helios-windows.zip"

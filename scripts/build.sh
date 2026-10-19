#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root/ui"
npm install
npm run build
cd "$root"
cmake -S . -B build -DCMAKE_CXX_COMPILER="${CXX:-/usr/bin/g++}"
cmake --build build -j
echo "Готово: $root/build/helios"

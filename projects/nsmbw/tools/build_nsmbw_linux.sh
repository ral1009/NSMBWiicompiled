#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
build_dir=${1:-"$root/build-linux-nsmbw"}
if (( $# )); then shift; fi

if [[ $(uname -sm) != 'Linux x86_64' ]]; then
    echo 'This build requires Linux x86_64.' >&2
    exit 1
fi
for tool in cmake ninja clang clang++; do
    command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done

cmake -S "$root/runtime" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DMKW_BUILD_MKW=OFF -DMKW_BUILD_NSMBW=ON -DNSMBW_RELEASE=ON \
    -DMKW_TRANSLATED_COMPILE_JOBS=2 "$@"
cmake --build "$build_dir" --target NSMBWCompiled --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"

#!/bin/sh
# Compiles src/storage_sd.cpp against the shim headers in shim/ and runs the
# save layer over real files in a scratch directory.
#
#   test/host_sd/run.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=${TMPDIR:-/tmp}/host_sd_test
scratch=${TMPDIR:-/tmp}/host-sd-scratch
mkdir -p "$scratch"

${CXX:-g++} -std=gnu++17 -O1 -Wall -Wno-unused-function \
  -I "$here/shim" -I "$root/src" \
  "$here/shim/impl.cpp" "$here/host_sd_test.cpp" \
  "$root/src/world.cpp" "$root/src/raycast.cpp" "$root/src/game.cpp" \
  "$root/src/storage_sd.cpp" \
  -o "$out"

HOST_SD_ROOT="$scratch" "$out"

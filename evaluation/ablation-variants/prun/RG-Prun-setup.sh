#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

echo "[Build RustGoPrun]"

export CURR="$SCRIPT_DIR"
export ENV_DIR="$REPO_ROOT/env"
pushd "$ENV_DIR"
set +u
source env.sh
set -u
popd

export RUSTGO_STUFFS="$REPO_ROOT/src/RustGo_stuffs/RustGo_one_target_version"

for required in \
  "$RUSTGO_STUFFS/include/common.h" \
  "$RUSTGO_STUFFS/include/config.h" \
  "$RUSTGO_STUFFS/include/sharedmem.h" \
  "$RUSTGO_STUFFS/instrumentation/afl-compiler-rt.o.c" \
  "$RUSTGO_STUFFS/src/afl-common.c" \
  "$RUSTGO_STUFFS/src/afl-fuzz.c" \
  "$RUSTGO_STUFFS/src/afl-sharedmem.c"; do
  if [ ! -f "$required" ]; then
    echo "Missing RustGoPrun source file: $required" >&2
    exit 1
  fi
done

: "${CARGO:?CARGO must be set by env.sh}"

cd "$SCRIPT_DIR"
rm -rf RustGoPrun
mkdir -p RustGoPrun
cd RustGoPrun
mkdir -p RuntimeLib_RustGoPrun
export RUSTGO_RUNTIME="$PWD/RuntimeLib_RustGoPrun"

echo "[Clone afl.rs for RustGoPrun]"
git clone https://github.com/rust-fuzz/afl.rs.git
cd afl.rs
git reset --hard b0b398bde12ae1943b26b26e74e348f3c812025f

echo "[Clone AFLplusplus for RustGoPrun]"
export AFL_RS="$PWD"
rm -rf AFLplusplus
git clone https://github.com/AFLplusplus/AFLplusplus.git
cd AFLplusplus
export AFLplusplus="$PWD"
git reset --hard 143c9d175e9357ba548413ee7dcee6a8de23f733

echo "[Patch AFLplusplus for RustGoPrun]"
rm -f include/common.h include/config.h include/sharedmem.h
rm -f instrumentation/afl-compiler-rt.o.c
rm -f src/afl-common.c src/afl-fuzz.c src/afl-sharedmem.c

cp "$RUSTGO_STUFFS/include/common.h" \
  "$RUSTGO_STUFFS/include/config.h" \
  "$RUSTGO_STUFFS/include/sharedmem.h" \
  "$AFLplusplus/include"
cp "$RUSTGO_STUFFS/instrumentation/afl-compiler-rt.o.c" "$AFLplusplus/instrumentation"
cp "$RUSTGO_STUFFS/src/afl-common.c" \
  "$RUSTGO_STUFFS/src/afl-fuzz.c" \
  "$RUSTGO_STUFFS/src/afl-sharedmem.c" \
  "$AFLplusplus/src"

echo "[Build RustGoPrun afl.rs]"
cd "$AFL_RS"
CARGO_BUILD_JOBS=1 CARGO_INCREMENTAL=0 "$CARGO" build

export AFL_RUNTIME_LIB="$HOME/.local/share/afl.rs/rustc-1.64.0-dev/afl.rs-0.12.9"
if [ ! -d "$AFL_RUNTIME_LIB" ]; then
  echo "Missing afl.rs runtime output: $AFL_RUNTIME_LIB" >&2
  exit 1
fi

cp -r "$AFL_RUNTIME_LIB" "$RUSTGO_RUNTIME"

echo "[Finished RustGoPrun build]"
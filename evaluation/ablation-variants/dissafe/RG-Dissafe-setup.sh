#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
ENV_DIR="$REPO_ROOT/env"

echo "[Setup RustGoDissafe]"

pushd "$ENV_DIR"
set +u
source env.sh
set -u
popd

: "${CARGO:?CARGO must be set by env.sh}"

RG_DISSAFE_DIR="$SCRIPT_DIR/RustGoDissafe"
RG_DISSAFE_RUNTIME="$RG_DISSAFE_DIR/RuntimeLib_RustGoDissafe"

rm -rf "$RG_DISSAFE_DIR"
mkdir -p "$RG_DISSAFE_RUNTIME"

cd "$RG_DISSAFE_DIR"

echo "[Clone afl.rs for RustGoDissafe]"
git clone https://github.com/rust-fuzz/afl.rs.git
cd afl.rs
git reset --hard b0b398bde12ae1943b26b26e74e348f3c812025f

echo "[Clone AFLplusplus for RustGoDissafe]"
AFL_RS="$PWD"
rm -rf AFLplusplus
git clone https://github.com/AFLplusplus/AFLplusplus.git
cd AFLplusplus
git reset --hard 143c9d175e9357ba548413ee7dcee6a8de23f733

echo "[Build RustGoDissafe afl.rs]"
cd "$AFL_RS"
CARGO_BUILD_JOBS=1 "$CARGO" build

AFL_RUNTIME_LIB="$HOME/.local/share/afl.rs/rustc-1.64.0-dev/afl.rs-0.12.9"
if [ ! -d "$AFL_RUNTIME_LIB" ]; then
  echo "Missing afl.rs runtime output: $AFL_RUNTIME_LIB" >&2
  exit 1
fi

cp -r "$AFL_RUNTIME_LIB" "$RG_DISSAFE_RUNTIME"

echo "[Finished RustGoDissafe setup]"

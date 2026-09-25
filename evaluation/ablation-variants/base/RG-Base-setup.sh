#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# set environment with s2fuzz compiler
export CURR=$SCRIPT_DIR
export ENV_DIR=$REPO_ROOT/env
pushd "$ENV_DIR"
set +u
source env.sh
set -u
popd
export RUSTGO_STUFFS=$REPO_ROOT/src/RustGo_stuffs/RustGo_mode_base

if [ ! -f "$RUSTGO_STUFFS/instrumentation/afl-compiler-rt.o.c" ]; then
  echo "Missing RustGo base instrumentation: $RUSTGO_STUFFS/instrumentation/afl-compiler-rt.o.c" >&2
  exit 1
fi

if [ ! -x "${CARGO:-}" ]; then
  echo "Missing cargo from env.sh: ${CARGO:-unset}" >&2
  exit 1
fi

# git clone of afl.rs
cd "$SCRIPT_DIR"
rm -rf RustGoBase
mkdir -p RustGoBase
cd RustGoBase
mkdir -p RuntimeLib_RustGoBase
export RUSTGO_RUNTIME=$PWD/RuntimeLib_RustGoBase

git clone https://github.com/rust-fuzz/afl.rs.git
cd afl.rs
git reset --hard b0b398bde12ae1943b26b26e74e348f3c812025f

# git clone of AFLplusplus in afl.rs
export AFL_RS=$PWD
rm -rf AFLplusplus 
git clone https://github.com/AFLplusplus/AFLplusplus.git 
cd AFLplusplus
export AFLplusplus=$PWD
git reset --hard 143c9d175e9357ba548413ee7dcee6a8de23f733

# build AFLplusplus with s2fuzz implementation
echo "[Build RustGoBase]"
rm instrumentation/afl-compiler-rt.o.c

echo "[Change instrumentation/afl-compiler-rt.o.c]"
cp $RUSTGO_STUFFS/instrumentation/afl-compiler-rt.o.c $AFLplusplus/instrumentation

echo "[Finished Build RustGoBase]"

# build afl.rs
cd $AFL_RS
cargo build

export AFL_RUNTIME_LIB=$HOME/.local/share/afl.rs/rustc-1.64.0-dev/afl.rs-0.12.9/
if [ ! -d "$AFL_RUNTIME_LIB" ]; then
  echo "Missing afl.rs runtime output: $AFL_RUNTIME_LIB" >&2
  exit 1
fi

cp -r $AFL_RUNTIME_LIB $RUSTGO_RUNTIME

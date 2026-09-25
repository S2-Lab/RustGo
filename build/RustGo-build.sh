#!/bin/bash

set -Eeuo pipefail

# set environment with s2fuzz compiler
export CURR=$PWD
export ENV_DIR=$CURR/../env
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
pushd $ENV_DIR
source env.sh
popd
export RUSTGO_STUFFS=$CURR/../src/RustGo_stuffs/RustGo
export RUSTC="$CURR/rust/build/x86_64-unknown-linux-gnu/stage1/bin/rustc"
export CARGO="$CURR/rust/build/x86_64-unknown-linux-gnu/stage0/bin/cargo"
export RUSTGO_SYSROOT="$CURR/rust/build/x86_64-unknown-linux-gnu/stage1"
export RUSTGO_TARGET_LIB="$RUSTGO_SYSROOT/lib/rustlib/x86_64-unknown-linux-gnu/lib"

if [ ! -x "$RUSTC" ]; then
    echo "[ERROR] Missing stage1 rustc: $RUSTC" >&2
    exit 1
fi

if [ ! -x "$CARGO" ]; then
    echo "[ERROR] Missing stage0 cargo: $CARGO" >&2
    exit 1
fi

if ! find "$RUSTGO_TARGET_LIB" -maxdepth 1 -type f -name 'libstd*.rlib' | grep -q .; then
    echo "[ERROR] Missing stage1 std libraries under $RUSTGO_TARGET_LIB" >&2
    echo "        Build the integrated Rust toolchain first so stage1 sysroot contains libstd/libcore." >&2
    exit 1
fi

if ! find "$RUSTGO_TARGET_LIB" -maxdepth 1 -type f -name 'libproc_macro*' | grep -q .; then
    echo "[ERROR] Missing stage1 proc_macro library under $RUSTGO_TARGET_LIB" >&2
    echo "        Build the integrated Rust toolchain first so stage1 sysroot contains libproc_macro." >&2
    exit 1
fi

# git clone of afl.rs
rm -rf RustGo
mkdir RustGo
cd RustGo
mkdir RuntimeLib_RustGo
export RUSTGO_RUNTIME=$PWD/RuntimeLib_RustGo

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
echo "[Build RustGo]"
rm include/afl-fuzz.h include/alloc-inl.h include/common.h include/config.h include/sharedmem.h 
rm instrumentation/afl-compiler-rt.o.c
rm src/afl-common.c src/afl-forkserver.c src/afl-fuzz-init.c src/afl-fuzz-queue.c src/afl-fuzz-state.c src/afl-fuzz-stats.c src/afl-fuzz.c src/afl-sharedmem.c

echo "[Change include/afl-fuzz.h include/alloc-inl.h include/common.h include/config.h include/sharedmem.h]"
cp $RUSTGO_STUFFS/include/afl-fuzz.h $RUSTGO_STUFFS/include/alloc-inl.h $RUSTGO_STUFFS/include/common.h $RUSTGO_STUFFS/include/config.h $RUSTGO_STUFFS/include/sharedmem.h $AFLplusplus/include
echo "[Change instrumentation/afl-compiler-rt.o.c]"
cp $RUSTGO_STUFFS/instrumentation/afl-compiler-rt.o.c $AFLplusplus/instrumentation
echo "[Change src/afl-common.c src/afl-forkserver.c src/afl-fuzz-init.c src/afl-fuzz-queue.c src/afl-fuzz-state.c src/afl-fuzz-stats.c src/afl-fuzz.c src/afl-sharedmem.c]"
cp $RUSTGO_STUFFS/src/afl-common.c $RUSTGO_STUFFS/src/afl-forkserver.c $RUSTGO_STUFFS/src/afl-fuzz-init.c $RUSTGO_STUFFS/src/afl-fuzz-queue.c $RUSTGO_STUFFS/src/afl-fuzz-state.c $RUSTGO_STUFFS/src/afl-fuzz-stats.c  $RUSTGO_STUFFS/src/afl-fuzz.c $RUSTGO_STUFFS/src/afl-sharedmem.c $AFLplusplus/src

echo "[Finished Build RustGo]"

# build afl.rs
cd $AFL_RS
"$CARGO" build

export AFL_RUNTIME_LIB=$HOME/.local/share/afl.rs/rustc-1.64.0-dev/afl.rs-0.12.9/
if [ ! -d "$AFL_RUNTIME_LIB" ]; then
    echo "[ERROR] Missing afl.rs runtime library: $AFL_RUNTIME_LIB" >&2
    exit 1
fi
cp -r $AFL_RUNTIME_LIB $RUSTGO_RUNTIME
echo "[Build Finished] RustGo runtime: $RUSTGO_RUNTIME"

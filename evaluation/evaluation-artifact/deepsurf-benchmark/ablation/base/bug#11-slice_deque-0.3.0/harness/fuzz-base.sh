#!/bin/bash
export RUSTGO_BASE_LD="$REPO_ROOT/evaluation/ablation-variants/base/RustGoBase/RuntimeLib_RustGoBase/afl.rs-0.12.9/afl-llvm-rt"
export RustGoBase="$REPO_ROOT/evaluation/ablation-variants/base/RustGoBase/afl.rs/AFLplusplus"

$RustGoBase/afl-fuzz -i in -o "$PWD/out" "$PWD/rustgo-base" 

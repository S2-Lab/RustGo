#!/bin/bash
export RUSTGO_OPT_LD="$REPO_ROOT/evaluation/ablation-variants/opt/RustGoOpt/RuntimeLib_RustGoOpt/afl.rs-0.12.9/afl-llvm-rt"
export RustGoOpt="$REPO_ROOT/evaluation/ablation-variants/opt/RustGoOpt/afl.rs/AFLplusplus"

"$RustGoOpt/afl-fuzz" -i in -o "$PWD/out" -- "$PWD/rustgo-opt"

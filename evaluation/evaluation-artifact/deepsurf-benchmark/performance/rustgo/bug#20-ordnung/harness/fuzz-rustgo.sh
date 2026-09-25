#!/bin/bash
set -Eeuo pipefail
export RUSTGO_LD="$REPO_ROOT/build/RustGo/RuntimeLib_RustGo/afl.rs-0.12.9/afl-llvm-rt/afl-llvm-rt.o"
export RustGo="$REPO_ROOT/build/RustGo/afl.rs/AFLplusplus"
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_SKIP_CPUFREQ=1

exec "$RustGo/afl-fuzz" -i in -o "$PWD/out" -- "$PWD/rustgo-target"

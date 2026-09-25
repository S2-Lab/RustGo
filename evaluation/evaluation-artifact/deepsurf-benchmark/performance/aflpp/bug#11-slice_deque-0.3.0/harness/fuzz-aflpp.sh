#!/bin/bash
set -Eeuo pipefail
export AFLRS="$REPO_ROOT/build/dgfs-porting/build/afl.rs/afl.rs-original/afl.rs/AFLplusplus"
export LIBRARY_DIR="$REPO_ROOT/build/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export LD_LIBRARY_PATH="$LIBRARY_DIR:${LD_LIBRARY_PATH:-}"
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_SKIP_CPUFREQ=1

"$AFLRS/afl-fuzz" -m none -i in -o out -- "$PWD/output" 

#!/bin/bash
# Setting RustGoBase Fuzzer Environment
export RustGoBase="$REPO_ROOT/evaluation/ablation-variants/base/RustGoBase/afl.rs/AFLplusplus"
export RUSTGO_BASE_LD="$REPO_ROOT/evaluation/ablation-variants/base/RustGoBase/RuntimeLib_RustGoBase/afl.rs-0.12.9/afl-llvm-rt"

#Checking Artifact Root
if [ -z "$ARTIFACT_ROOT" ]; then
    echo "[+] ARTIFACT_ROOT is not set."
    echo "[+] Please run 'source env.sh' in the top directory of this artifact."
    exit 1
fi

# Setting Out Dir
if [[ -d out ]]; then
    rm -rf out
fi

# Setting Logfile
log_file="reaching-log.txt"
start_time_ms=$(date +%s%3N)
echo "$start_time_ms" > "$log_file"

# Run RustGoBase fuzzing
LD_PRELOAD="$ARTIFACT_ROOT/scripts/eval.so" \
$RustGoBase/afl-fuzz -i in -o "$PWD/out" "$PWD/rustgo-base"
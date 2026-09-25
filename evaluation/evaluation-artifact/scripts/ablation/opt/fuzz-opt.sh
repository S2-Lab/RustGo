#!/bin/bash
# Setting RustGoOpt Fuzzer Environment
export RustGoOpt="$REPO_ROOT/evaluation/ablation-variants/opt/RustGoOpt/afl.rs/AFLplusplus"
export RUSTGO_OPT_LD="$REPO_ROOT/evaluation/ablation-variants/opt/RustGoOpt/RuntimeLib_RustGoOpt/afl.rs-0.12.9/afl-llvm-rt"

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

# Run RustGoOpt fuzzing
LD_PRELOAD="$ARTIFACT_ROOT/scripts/eval.so" \
$RustGoOpt/afl-fuzz -i in -o "$PWD/out" "$PWD/rustgo-opt"
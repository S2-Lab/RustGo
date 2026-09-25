#!/bin/bash
# Setting RustGo Fuzzer Environment
export RUSTGO_LD="$REPO_ROOT/build/RustGo/RuntimeLib_RustGo/afl.rs-0.12.9/afl-llvm-rt/afl-llvm-rt.o"
export RustGo="$REPO_ROOT/build/RustGo/afl.rs/AFLplusplus"

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

# Run RustGo fuzzing
LD_PRELOAD="$ARTIFACT_ROOT/scripts/eval.so" \
$RustGo/afl-fuzz -i in -o "$PWD/out" "$PWD/rustgo-target"
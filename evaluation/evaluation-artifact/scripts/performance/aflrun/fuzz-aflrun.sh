#!/bin/bash
# Setting AFLRUN Fuzzer Environment
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/1.69.0-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH"
export PATH="$HOME/.rustup/toolchains/1.69.0-x86_64-unknown-linux-gnu/bin:$PATH"
export AFLRUN="$REPO_ROOT/evaluation/compared-fuzzer/build/AFLRun/AFLRun"

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

# Run AFLRUN fuzzing
LD_PRELOAD="$ARTIFACT_ROOT/scripts/eval.so" \
"$AFLRUN/afl-fuzz" -i in -o out -- "$PWD/output"
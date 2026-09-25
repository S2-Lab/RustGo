#!/bin/bash
# Setting WindRanger Fuzzer Environment
export WINDRANGER="$REPO_ROOT/evaluation/compared-fuzzer/build/windranger/WindRanger"
export PATH="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/bin:$PATH"
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH"
export WR_BB_TARGETS="$PWD/temp/BBtargets.txt"
export WR_TARGETS="poc" 

#Checking Artifact Root
if [ -z "$ARTIFACT_ROOT" ]; then
    echo "[+] ARTIFACT_ROOT is not set."
    echo "[+] Please run 'source env.sh' in the top directory of this artifact."
    exit 1
fi

# Setting Logfile
log_file="reaching-log.txt"
start_time_ms=$(date +%s%3N)
echo "$start_time_ms" > "$log_file"


LD_PRELOAD="$ARTIFACT_ROOT/scripts/eval.so" \
$WINDRANGER/afl-fuzz -m none -i in -o out $PWD/output 
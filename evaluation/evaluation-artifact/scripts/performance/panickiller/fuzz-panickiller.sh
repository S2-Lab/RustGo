#!/bin/bash
# Setting PanicKiller Fuzzer Environment
export PANICKILLER_DIR="$REPO_ROOT/evaluation/compared-fuzzer/build/panickiller"
export PK_LD="$PANICKILLER_DIR/"
export PK="$PANICKILLER_DIR/afl.rs/AFLplusplus"

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

LD_PRELOAD="$ARTIFACT_ROOT/scripts/eval.so" \
$PK/afl-fuzz -i in -o "$PWD/out" "$PWD/pk-target"

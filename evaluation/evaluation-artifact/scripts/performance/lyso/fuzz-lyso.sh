#!/bin/bash
# Setting Lyso Fuzzer Environment
export LYSO="$REPO_ROOT/evaluation/compared-fuzzer/build/lyso"
export AFL_SKIP_CPUFREQ=1
export AFL_NO_AFFINITY=1
export AFL_USE_ASAN=1
export TMP_DIR=$PWD/tmp
export SHARED=$PWD/shared
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-02-11-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH"
mkdir -p "$SHARED/findings"
echo core | sudo tee /proc/sys/kernel/core_pattern

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


LD_PRELOAD="$ARTIFACT_ROOT/scripts/eval.so" $LYSO/magma/fuzzers/lyso/repo/afl-fuzz \
  -d -g "$TMP_DIR" -c "$SHARED" -m 100 \
  -i in -o "$SHARED/findings" -- "$PWD/output" 2>&1

#!/bin/bash
# Setting AFLPP Fuzzer Environment
export AFLPP_RT_DIR="$REPO_ROOT/evaluation/compared-fuzzer/build/afl.rs/afl.rs-original/RuntimeLib_Orig/afl.rs-0.12.9/afl-llvm-rt"
export AFLPP="$REPO_ROOT/evaluation/compared-fuzzer/build/afl.rs/afl.rs-original/afl.rs/AFLplusplus"
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_SKIP_CPUFREQ=1

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

# Run AFLPP fuzzing
LD_PRELOAD="$ARTIFACT_ROOT/scripts/eval.so" \
$AFLPP/afl-fuzz -i in -o "$PWD/out" "$PWD/output"
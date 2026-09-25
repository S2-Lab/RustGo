#!/usr/bin/env bash
set -Eeuo pipefail

start=$(date +%s%6N)

PROGRAM="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../../../../.." && pwd)"
AFLPP_SCRIPT="$REPO_ROOT/evaluation/evaluation-artifact/scripts/deepsurf-benchmark/performance/aflpp"
ENV_DIR="$REPO_ROOT/env"
HARNESS_DIR="$PWD"
BUG_DIR="$(cd "$HARNESS_DIR/.." && pwd)"

cd $ENV_DIR
source env.sh
cd -
: "${LOG_FILE:=$HARNESS_DIR/build-aflpp.log}"
exec > >(tee "$LOG_FILE") 2>&1

echo "[*] Logging build output to: $LOG_FILE"
echo "[*] Harness directory: $HARNESS_DIR"
echo "[*] Bug directory: $BUG_DIR"
echo "[*] Program name: $PROGRAM"

LIBRARY_DIR="$BUILD_DIR/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"
if [[ ! -d "$LIBRARY_DIR" ]]; then
    echo "[-] Missing Rust standard library directory: $LIBRARY_DIR" >&2
    exit 1
fi

# Fuzzing Runtime Library
export COMPILER_RT_DIR="$REPO_ROOT/evaluation/compared-fuzzer/build/afl.rs/afl.rs-original/RuntimeLib_Orig/afl.rs-0.12.9/afl-llvm-rt"

echo "[*] Building fuzz target with LLVM IR emission..."
RUSTFLAGS="--emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu

echo "[*] Merging LLVM IR files..."
python3 merge-aflpp.py "$PROGRAM"

echo "[*] Running AFLPP instrumentation and binary generation..."
RLIBS=$(find "$LIBRARY_DIR" -name "*.rlib" -type f)
BUILDS_DIR="$HARNESS_DIR/target/x86_64-unknown-linux-gnu/debug/build"
DLIBRARY_DIR="$HARNESS_DIR/target/x86_64-unknown-linux-gnu/debug/deps"

DRLIBS=$(find "$DLIBRARY_DIR" -name "lib*.rlib" -type f 2>/dev/null)
FRLIBS=""
OBJECTS=""

echo "[*] Collecting target program .rlib and .o files..."
while read -r lib; do
    [[ -z "$lib" ]] && continue
    echo "  - Found library: $lib"
    FRLIBS="$FRLIBS $lib"
done < <(find "$BUILDS_DIR" -name "*.rlib" -type f)

while read -r obj; do
    [[ -z "$obj" ]] && continue
    echo "  - Found object file: $obj"
    OBJECTS="$OBJECTS $obj"
done < <(find "$BUILDS_DIR" -name "*.o" -type f)

# LLVM Pass and Generate Object Code
opt -O2 total.ll -o total2.ll
opt --asan-module -S total2.ll -o total2.ll
opt -passes=sancov-module -sanitizer-coverage-level=3 -sanitizer-coverage-trace-pc-guard -sanitizer-coverage-prune-blocks=0 -S total2.ll -o sancov.ll
llvm-as sancov.ll -o total.bc
llc -filetype=obj total.bc -o total.o

# Generate Binary
clang total.o -o output -L$LIBRARY_DIR -lstd-bc021c2817d56996 -fsanitize=address \
    -lpthread -ldl -lm -L$COMPILER_RT_DIR -lafl-llvm-rt \
    -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
    -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group \

rm sancov.ll total.bc total.o total.ll total2.ll

end=$(date +%s%6N)
runtime=$((end-start))
echo "Done!!"
echo "Compile Time : $runtime ns"


#!/usr/bin/env bash
set -euo pipefail
PROGRAM="$1"
start=$(date +%s%6N)

# ==============================
# Step 1: Setting up RustGo Base Environment
# ==============================
export LIBRARY_DIR="$BUILD_DIR/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export AFLPP_RT_DIR="$REPO_ROOT/evaluation/compared-fuzzer/build/afl.rs/afl.rs-original/RuntimeLib_Orig/afl.rs-0.12.9/afl-llvm-rt"
export AFLPP_SCRIPT="$REPO_ROOT/evaluation/evaluation-artifact/scripts/performance/aflpp"
export HARNESS_DIR="$PWD"

# ==============================
# Step 1: Build LLVM IR from fuzz target
# ==============================
echo "[*] Building fuzz target with LLVM IR emission..."
RUSTFLAGS="--emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu --bin fuzz-target

echo "[*] Merging LLVM IR files..."
python3 merge-aflpp.py "$PROGRAM"

echo "[*] Running AFLPP instrumentation and binary generation..."
RLIBS=$(find "$LIBRARY_DIR" -name "*.rlib" -type f | sort -u)
BUILDS_DIR="$HARNESS_DIR/target/x86_64-unknown-linux-gnu/debug/build"
DLIBRARY_DIR="$HARNESS_DIR/target/x86_64-unknown-linux-gnu/debug/deps"

DRLIBS=$(find "$DLIBRARY_DIR" -name "lib*.rlib" -type f 2>/dev/null | sort -u)
FRLIBS=""
OBJECTS=""

echo "[*] Collecting target program .rlib and .o files..."
while read -r lib; do
    [[ -z "$lib" ]] && continue
    echo "  - Found library: $lib"
    FRLIBS="$FRLIBS $lib"
done < <(find "$BUILDS_DIR" -name "*.rlib" -type f | sort -u)

while read -r obj; do
    [[ -z "$obj" ]] && continue
    echo "  - Found object file: $obj"
    OBJECTS="$OBJECTS $obj"
done < <(find "$BUILDS_DIR" -name "*.o" -type f ! -path "*/out/depend/*" | sort -u)


# ==============================
# Step 2: AFLPP Instrumentation and Binary Generation
# ==============================
opt -O2 total.ll -o total2.ll
opt -passes=sancov-module -sanitizer-coverage-level=3 -sanitizer-coverage-trace-pc-guard -sanitizer-coverage-prune-blocks=0 -S total2.ll -o sancov.ll
llvm-as sancov.ll -o total.bc
llc -filetype=obj total.bc -o total.o

# Generate Binary
clang total.o -o output -L$LIBRARY_DIR -lstd-bc021c2817d56996\
    -lpthread -ldl -lm -L$AFLPP_RT_DIR -lafl-llvm-rt \
    -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
    -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group \

rm -f sancov.ll output.bc output.o total*

cp "$AFLPP_SCRIPT/fuzz-aflpp.sh" .
cp -r "$AFLPP_SCRIPT/in" .

end=$(date +%s%6N)
runtime=$((end-start))
echo "Done!!"
echo "Compile Time : $runtime ns"


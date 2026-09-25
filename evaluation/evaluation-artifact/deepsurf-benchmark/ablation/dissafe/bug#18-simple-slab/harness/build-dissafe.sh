#!/bin/bash
set -e
PROGRAM="$1"

# ==============================
# Step 1: Setting up Dissafe Environment
# ==============================
export LIBRARY_DIR="$REPO_ROOT/build/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export RUSTGO_DISSAFE_LD="$REPO_ROOT/evaluation/ablation-variants/dissafe/RustGoDissafe/RuntimeLib_RustGoDissafe/afl.rs-0.12.9/afl-llvm-rt"
export HARNESS_DIR="$PWD"


# ==============================
# Step 2: Build Target Program
# ==============================
echo "[*] Building fuzz target with LLVM IR emission..."
RUSTFLAGS="-Zsanitizer=address --emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu

echo "[*] Merging LLVM IR files..."
python3 merge-dissafe.py "$PROGRAM"

echo "[*] Setting Library Path"
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
# Step 2: Perform Dissafe analysis and instrumentation
# ==============================
echo "[*] Applying LLVM optimization and instrumentation..."
s2pidy total.ll -rustgo-no-safecode-feedback
opt -O2 RustGoUnsafeMarkedIR.ll -o RustGoUnsafeMarkedIR2.ll
opt -passes=sancov-module-no-safe -sanitizer-coverage-level-no-safe=3 -sanitizer-coverage-trace-pc-guard-no-safe -sanitizer-coverage-prune-blocks-no-safe=0 -S RustGoUnsafeMarkedIR2.ll -o sancov-no-safe.ll
llvm-as sancov-no-safe.ll -o output.bc
llc -filetype=obj output.bc -o output.o

# Generate Binary
clang output.o -o rustgo-dissafe -fsanitize=address -L"$LIBRARY_DIR" -lstd-bc021c2817d56996 \
    -lpthread -ldl -lm -L"$RUSTGO_DISSAFE_LD" -lafl-llvm-rt \
    -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
    -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group 

echo "[+] Build complete. Output: rustgo-dissafe"
end=$(date +%s%6N)
runtime=$((end - start))
echo "[+] Compile time: $runtime ns"

# ==============================
# Step 4: Prepare output directories
# ==============================
echo "[*] Preparing output directories..."

# Clean and recreate dissafe-build & fuzz
rm -rf dissafe-build rustgo-dissafe-fuzz
mkdir dissafe-build rustgo-dissafe-fuzz

echo "[*] Moving and organizing build artifacts..."
mv ./*.ll dissafe-build/ 2>/dev/null || true
mv ./*.o dissafe-build/ 2>/dev/null || true
mv ./*.bc dissafe-build/ 2>/dev/null || true
mv ./*.txt dissafe-build/ 2>/dev/null || true

echo "[*] Processing RustGo target binary and evaluation script..."
cp fuzz-dissafe.sh rustgo-dissafe-fuzz/
cp -r in rustgo-dissafe-fuzz/
mv rustgo-dissafe rustgo-dissafe-fuzz/

echo "[+] RustGo Dissafe Setup Complete."

#!/bin/bash
set -Eeuo pipefail
PROGRAM="$1"

# ==============================
# Step 1: Setting up Environment
# ==============================
export LIBRARY_DIR="$BUILD_DIR/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export RUSTGO_COMPILER_RT="$REPO_ROOT/build/RustGo/RuntimeLib_RustGo/afl.rs-0.12.9/afl-llvm-rt"
export RustGo="$REPO_ROOT/build/RustGo/afl.rs/AFLplusplus"
export RustGo_SCRIPT="$ARTIFACT_ROOT/scripts/performance/rustgo"
export HARNESS_DIR="$PWD"

# ==============================
# Step 1: Build LLVM IR from fuzz target
# ==============================
echo "[*] Building fuzz target with LLVM IR emission"
RUSTFLAGS="--emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu

echo "[*] Merging LLVM IR files"
python3 merge-rustgo.py "$PROGRAM"

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
# Step 3: Run RustGo build script
# ==============================
echo "[*] Applying LLVM optimization and instrumentation..."
s2pidy total.ll -afl-no-input-sharing-mode
opt -O2 S2FuzzAnalysis.ll -o S2FuzzAnalysis.ll
opt -passes=sancov-module -sanitizer-coverage-level=3 \
    -sanitizer-coverage-trace-pc-guard -sanitizer-coverage-prune-blocks=0 \
    -S S2FuzzAnalysis.ll -o sancov.ll
opt --asan-module -S sancov.ll -o asan.ll

echo "[*] Compiling instrumented IR to object..."
llvm-as asan.ll -o output.bc
llc -filetype=obj output.bc -o output.o

echo "[*] Linking final binary..."
clang -fsanitize=address output.o -o rustgo-target -L$LIBRARY_DIR -lstd-bc021c2817d56996 \
    -lpthread -ldl -lm -L$RUSTGO_COMPILER_RT -lafl-llvm-rt \
    -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
    -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group \

echo "[✔] Build complete. Output: rustgo-target"

# ==============================
# Step 4: Prepare output directories
# ==============================
echo "[*] Preparing output directories..."

# Clean and recreate rustgo-build & fuzz
if [ -d "rustgo-build" ]; then
    rm -rf rustgo-build
fi
mkdir rustgo-build

if [ -d "rustgo-fuzz" ]; then
    rm -rf rustgo-fuzz
fi
mkdir rustgo-fuzz

# ==============================
# Step 5: Move and organize files
# ==============================

echo "[*] Moving and organizing build artifacts..."

# Move build artifacts to rustgo-build
mv *.ll rustgo-build/ || true
mv *.o  rustgo-build/ || true
mv *.bc rustgo-build/ || true
mv *.txt rustgo-build/ || true

# Move compiled binary to rustgo-fuzz
mv rustgo-target rustgo-fuzz/

# Move and copy important analysis results
mv rustgo-build/TotalBBIndexCount.txt rustgo-fuzz/
cp rustgo-build/TogglePostDominated.txt rustgo-fuzz/ToggleInfoNew.txt

# Copy evaluation script and input corpus
cp fuzz-rustgo.sh rustgo-fuzz/
cp -r in rustgo-fuzz/

echo "[✔] RustGO instrumentation and setup complete."

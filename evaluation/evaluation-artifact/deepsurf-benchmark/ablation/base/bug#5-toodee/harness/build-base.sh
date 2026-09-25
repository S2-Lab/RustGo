#!/usr/bin/env bash
set -euo pipefail
PROGRAM="$1"
# ==============================
# Step 1: Setting up Base Environment
# ==============================
export LIBRARY_DIR="$REPO_ROOT/build/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export RUSTGO_BASE_LD="$REPO_ROOT/evaluation/ablation-variants/base/RustGoBase/RuntimeLib_RustGoBase/afl.rs-0.12.9/afl-llvm-rt"
export HARNESS_DIR="$PWD"
export BASE_SCRIPT="$ARTIFACT_ROOT/scripts/ablation/base"

# ==============================
# Step 1: Build LLVM IR from fuzz target
# ==============================
# echo "[*] Building fuzz-target with LLVM IR emission..."
RUSTFLAGS="--emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu

# ==============================
# Step 2: Merge LLVM IR files
# ==============================
echo "[*] Merging LLVM IR files..."
python3 merge-base.py $PROGRAM

# ==============================
# Step 3: Run RustGo build script
# ==============================
echo "[*] Running RustGo instrumentation and binary generation..."

# Rust standard library path
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
# Apply LLVM passes and build binary
# ==============================
echo "Running LLVM passes and generating binary..."

echo "[*] Applying LLVM optimization and instrumentation..."
s2pidy total.ll -newvul-ablation-module-base
opt -O2 NewVulAblationStudyModuleBase.ll -o NewVulAblationStudyModuleBase.ll
opt -passes=sancov-module -sanitizer-coverage-level=3 -sanitizer-coverage-trace-pc-guard -sanitizer-coverage-prune-blocks=0 -S NewVulAblationStudyModuleBase.ll -o sancov.ll
opt --asan-module -S sancov.ll -o asan.ll
llvm-as asan.ll -o output.bc
llc -filetype=obj output.bc -o output.o

echo "[*] Linking final binary..."
clang output.o -o rustgo-base -fsanitize=address -L$LIBRARY_DIR -lstd-bc021c2817d56996 \
    -lpthread -ldl -lm -L$RUSTGO_BASE_LD -lafl-llvm-rt \
    -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
    -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group 

echo "[✔] Build complete. Output: rustgo-target"

# ==============================
# Step 4: Prepare output directories
# ==============================
echo "[*] Preparing output directories..."

# Clean and recreate rustgo-base-build & fuzz
if [ -d "rustgo-base-build" ]; then
    rm -rf rustgo-base-build
fi
mkdir rustgo-base-build

if [ -d "rustgo-base-fuzz" ]; then
    rm -rf rustgo-base-fuzz
fi
mkdir rustgo-base-fuzz

# ==============================
# Step 5: Move and organize files
# ==============================

echo "[*] Moving and organizing build artifacts..."

# Move build artifacts to rustgo-base-build
mv *.ll rustgo-base-build/ || true
mv *.o  rustgo-base-build/ || true
mv *.bc rustgo-base-build/ || true
mv *.txt rustgo-base-build/ || true

# Move compiled binary to rustgo-base-fuzz
mv rustgo-base rustgo-base-fuzz/

# Copy evaluation script and input corpus
cp fuzz-base.sh rustgo-base-fuzz/
cp -r in rustgo-base-fuzz/

echo "[✔] base instrumentation and setup complete."





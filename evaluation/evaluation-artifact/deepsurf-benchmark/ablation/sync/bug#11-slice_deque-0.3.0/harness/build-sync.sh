#!/usr/bin/env bash
set -euo pipefail
PROGRAM="$1"
# ==============================
# Step 1: Setting up Sync Environment
# ==============================
export LIBRARY_DIR="$REPO_ROOT/build/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export RUSTGO_SYNC_LD="$REPO_ROOT/evaluation/ablation-variants/sync/RustGoSync/RuntimeLib_RustGoSync/afl.rs-0.12.9/afl-llvm-rt"
export HARNESS_DIR="$PWD"
export SYNC_SCRIPT="$ARTIFACT_ROOT/scripts/ablation/sync"
cp "$SYNC_SCRIPT/aggregate-target.py" .

# ==============================
# Step 2: Build Target Program
# ==============================
echo "[*] Building fuzz-target with LLVM IR emission..."
RUSTFLAGS="--emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu

echo "[*] Merging LLVM IR files..."
python3 merge-sync.py "$PROGRAM"

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
# Step 3: Generate Binary
# ==============================
s2pidy total.ll -afl-no-input-sharing-mode-async-safe
opt -O2 S2FuzzAsyncSafeAnalysis.ll -o S2FuzzAsyncSafeAnalysis.ll
opt -passes=sancov-module -sanitizer-coverage-level=3 -sanitizer-coverage-trace-pc-guard -sanitizer-coverage-prune-blocks=0 -S S2FuzzAsyncSafeAnalysis.ll -o sancov.ll
opt --asan-module -S sancov.ll -o asan.ll
llvm-as asan.ll -o output.bc
llc -filetype=obj output.bc -o output.o

echo "[*] Linking final binary..."
clang output.o -o rustgo-sync -fsanitize=address -L"$LIBRARY_DIR" -lstd-bc021c2817d56996 \
    -lpthread -ldl -lm -L"$RUSTGO_SYNC_LD" -lafl-llvm-rt \
    -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
    -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group 

# ==============================
# Step 4: Prepare output directories
# ==============================
echo "[*] Aggregating toggle information..."
python3 aggregate-target.py TogglePostDominated.txt AggregatedToggleInfo.txt

# Clean and recreate rustgo-sync-build & fuzz
rm -rf rustgo-sync-build rustgo-sync-fuzz
mkdir rustgo-sync-build rustgo-sync-fuzz

echo "[*] Moving and organizing build artifacts..."
# Move build artifacts to rustgo-sync-build
mv *.ll rustgo-sync-build/ || true
mv *.o  rustgo-sync-build/ || true
mv *.bc rustgo-sync-build/ || true
mv *.txt rustgo-sync-build/ || true

# Move compiled binary to rustgo-sync-fuzz
mv rustgo-sync rustgo-sync-fuzz/

python3 aggregate-target.py rustgo-sync-build/TogglePostDominated.txt rustgo-sync-build/AggregatedToggleInfo.txt

# Move and copy important analysis results
mv rustgo-sync-build/TotalBBIndexCount.txt rustgo-sync-fuzz/
cp rustgo-sync-build/AggregatedToggleInfo.txt rustgo-sync-fuzz/ToggleInfoNew.txt

# Copy evaluation script and input corpus
cp fuzz-sync.sh rustgo-sync-fuzz/
cp -r in rustgo-sync-fuzz/

echo "[+] Sync instrumentation and setup complete."

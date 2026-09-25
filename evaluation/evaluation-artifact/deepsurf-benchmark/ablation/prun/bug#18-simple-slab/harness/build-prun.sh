#!/usr/bin/env bash
set -euo pipefail
PROGRAM="$1"
# ==============================
# Step 1: Setting up Prun Environment
# ==============================
export LIBRARY_DIR="$REPO_ROOT/build/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export RUSTGO_PRUN_LD="$REPO_ROOT/evaluation/ablation-variants/prun/RustGoPrun/RuntimeLib_RustGoPrun/afl.rs-0.12.9/afl-llvm-rt"
export HARNESS_DIR="$PWD"
export PRUN_SCRIPT="$ARTIFACT_ROOT/scripts/ablation/prun"
cp "$PRUN_SCRIPT/aggregate-target.py" .

# ==============================
# Step 2: Build Target Program
# ==============================
echo "[*] Building fuzz-target with LLVM IR emission..."
RUSTFLAGS="--emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu

echo "[*] Merging LLVM IR files..."
python3 merge-prun.py "$PROGRAM"

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
# Step 3: Run RustGo Prun build script
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
clang -fsanitize=address output.o -o rustgo-prun -L$LIBRARY_DIR -lstd-bc021c2817d56996 \
    -lpthread -ldl -lm -L$RUSTGO_PRUN_LD -lafl-llvm-rt \
    -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
    -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group \


echo "[*] Aggregating toggle information..."
python3 aggregate-target.py TogglePostDominated.txt AggregatedToggleInfo.txt

# ==============================
# Step 4: Prepare output directories
# ==============================
echo "[*] Preparing output directories..."

for dir in rustgo-prun-build rustgo-prun-fuzz; do
    rm -rf "$dir"
    mkdir "$dir"
done

# Move build artifacts to rustgo-prun-build
mv *.ll rustgo-prun-build/ || true
mv *.o  rustgo-prun-build/ || true
mv *.bc rustgo-prun-build/ || true
mv *.txt rustgo-prun-build/ || true

mv rustgo-prun rustgo-prun-fuzz/
cp rustgo-prun-build/AggregatedToggleInfo.txt rustgo-prun-fuzz/ToggleInfoNew.txt
cp rustgo-prun-build/TotalBBIndexCount.txt rustgo-prun-fuzz/

cp fuzz-prun.sh rustgo-prun-fuzz/
cp -r in rustgo-prun-fuzz/

echo "[+] RustGo-Prun build and setup complete."

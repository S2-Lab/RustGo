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
RUSTFLAGS="--emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu --bin fuzz-target

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
# Step 2: Perform RustGo prun analysis and instrumentation
# ==============================
declare -a function_list=(
    "RUSTSEC_2019_0009" "RUSTSEC_2019_0023" "RUSTSEC_2019_0034"
    "RUSTSEC_2020_0005" "RUSTSEC_2020_0039" "RUSTSEC_2021_0018"
    "RUSTSEC_2021_0028" "RUSTSEC_2021_0031" "RUSTSEC_2021_0033"
    "RUSTSEC_2021_0039" "RUSTSEC_2021_0042" "RUSTSEC_2021_0048"
    "RUSTSEC_2021_0049" "RUSTSEC_2021_0053" "RUSTSEC_2022_0070"
)

echo "[*] Generating manual target info..."
sync_command="s2pidy total.ll -afl-manual-target-collection"
basic_options="-b bb1"
target_info_file="TargetInfo.txt"

echo "[*] Collecting target ICFG nodes..."
rm -f "$target_info_file"

for func in "${function_list[@]}"; do
    echo "  - Targeting: $func"
    $sync_command -f "$func" $basic_options
    sleep 1
done

echo "[*] Performing manual analysis..."
icfg_nodes=$(grep -oP 'ICFGNode \d+' "$target_info_file" | awk '{print $2}' | paste -sd, -)
echo "    Extracted ICFGNodes: $icfg_nodes"
s2pidy total.ll -afl-manual-analysis-async-safe -t="$icfg_nodes"

# ==============================
# Step 3: Generate Binary
# ==============================
echo "[*] Running LLVM passes and generating binary..."
opt -O2 S2FuzzManualTargetAnalysis.ll -o S2FuzzManualTargetAnalysis.ll
opt -passes=sancov-module -sanitizer-coverage-level=3 \
    -sanitizer-coverage-trace-pc-guard -sanitizer-coverage-prune-blocks=0 \
    -S S2FuzzManualTargetAnalysis.ll -o sancov.ll

# Assemble and compile to object
llvm-as sancov.ll -o output.bc
llc -filetype=obj output.bc -o output.o

# Link all objects into final binary
clang output.o -o rustgo-prun \
    -L"$LIBRARY_DIR" -lstd-bc021c2817d56996 \
    -L"$RUSTGO_PRUN_LD" -lafl-llvm-rt \
    -lpthread -ldl -lm \
    -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
    -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group

echo "[*] Aggregating toggle information..."
python3 aggregate-target.py TogglePostDominated.txt AggregatedToggleInfo.txt

# ==============================
# Step 4: Prepare output directories
# ==============================
echo "[*] Preparing output directories..."

# Clean and recreate rustgo-prun-build & fuzz
rm -rf rustgo-prun-build rustgo-prun-fuzz
mkdir rustgo-prun-build rustgo-prun-fuzz

echo "[*] Moving and organizing build artifacts..."
mv ./*.ll rustgo-prun-build/ 2>/dev/null || true
mv ./*.o rustgo-prun-build/ 2>/dev/null || true
mv ./*.bc rustgo-prun-build/ 2>/dev/null || true
mv ./*.txt rustgo-prun-build/ 2>/dev/null || true

echo "[*] Processing RustGo target binary and evaluation script..."
cp "$PRUN_SCRIPT/fuzz-prun.sh" rustgo-prun-fuzz/
cp -r "$PRUN_SCRIPT/in" rustgo-prun-fuzz/
mv rustgo-prun rustgo-prun-fuzz/
cp rustgo-prun-build/AggregatedToggleInfo.txt rustgo-prun-fuzz/ToggleInfoNew.txt
cp rustgo-prun-build/TotalBBIndexCount.txt rustgo-prun-fuzz/

echo "[+] RustGo-prun build and setup complete."

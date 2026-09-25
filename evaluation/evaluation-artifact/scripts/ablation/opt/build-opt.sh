#!/usr/bin/env bash
set -euo pipefail
PROGRAM="$1"
# ==============================
# Step 1: Setting up Opt Environment
# ==============================
export LIBRARY_DIR="$REPO_ROOT/build/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export RUSTGO_OPT_LD="$REPO_ROOT/evaluation/ablation-variants/opt/RustGoOpt/RuntimeLib_RustGoOpt/afl.rs-0.12.9/afl-llvm-rt"
export HARNESS_DIR="$PWD"
export OPT_SCRIPT="$ARTIFACT_ROOT/scripts/ablation/opt"

# ==============================
# Step 2: Build Target Program
# ==============================
echo "[*] Building fuzz target with LLVM IR emission"
RUSTFLAGS="--emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu --bin fuzz-target

echo "[*] Merging LLVM IR files"
python3 merge-opt.py "$PROGRAM"

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
# Step 2: Perform Opt analysis and instrumentation
# ==============================
declare -a function_list=(
    "RUSTSEC_2019_0009" "RUSTSEC_2019_0023" "RUSTSEC_2019_0034"
    "RUSTSEC_2020_0005" "RUSTSEC_2020_0039" "RUSTSEC_2021_0018"
    "RUSTSEC_2021_0028" "RUSTSEC_2021_0031" "RUSTSEC_2021_0033"
    "RUSTSEC_2021_0039" "RUSTSEC_2021_0042" "RUSTSEC_2021_0048"
    "RUSTSEC_2021_0049" "RUSTSEC_2021_0053" "RUSTSEC_2022_0070"
)

echo "[*] Generating manual target info..."
base_command="s2pidy total.ll -afl-manual-target-collection"
basic_options="-b bb1"
target_info_file="TargetInfo.txt"
rm -f "$target_info_file"
for func in "${function_list[@]}"; do
    echo "Running: $base_command -f $func $basic_options"
    $base_command -f "$func" $basic_options
    sleep 1
done
echo "[✔] Manual target collection complete."

icfg_nodes=$(grep -oP 'ICFGNode \d+' "$target_info_file" | awk '{print $2}' | paste -sd, -)
echo "[*] Extracted ICFGNodes: $icfg_nodes"

echo "[*] Running Opt target optimization..."
s2pidy total.ll -afl-manual-analysis-no-input-sharing -t=$icfg_nodes

# Extract optimized target nodes
target_opt_info_file="TogglePostDominatedLineInfo.txt"
icfg_opt_nodes=$(grep -oP 'targetID\s*:\s*\[\K\d+(?=\])' "$target_opt_info_file" | paste -sd, -)
echo "[*] Extracted optimized ICFGNodes: $icfg_opt_nodes"

echo "[*] Running SieveFuzz backward analysis..."
s2pidy total.ll -ablation-module-base -t=$icfg_opt_nodes


# ==============================
# Step 3: Generate Binary
# ==============================
echo "[*] Running LLVM passes and generating binary..."
# Optimization and coverage instrumentation
opt -O2 AblationStudyModuleBase.ll -o AblationStudyModuleBaseOpt2.ll
opt -passes=sancov-module -sanitizer-coverage-level=3 \
    -sanitizer-coverage-trace-pc-guard \
    -sanitizer-coverage-prune-blocks=0 \
    -S AblationStudyModuleBaseOpt2.ll -o sancov.ll

# Assemble to bitcode and object
llvm-as sancov.ll -o output.bc
llc -filetype=obj output.bc -o output.o

# Link final binary
clang output.o -o rustgo-opt \
    -L"$LIBRARY_DIR" -lstd-bc021c2817d56996 \
    -L"$RUSTGO_OPT_LD" -lafl-llvm-rt \
    -lpthread -ldl -lm \
    -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
    -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group


# ==============================
# Step 4: Prepare output directories
# ==============================
echo "[*] Preparing output directories..."

# Clean and recreate rustgo-opt-build & fuzz
rm -rf rustgo-opt-build rustgo-opt-fuzz
mkdir rustgo-opt-build rustgo-opt-fuzz

echo "[*] Moving and organizing build artifacts..."
mv ./*.ll rustgo-opt-build/ 2>/dev/null || true
mv ./*.o rustgo-opt-build/ 2>/dev/null || true
mv ./*.bc rustgo-opt-build/ 2>/dev/null || true
mv ./*.txt rustgo-opt-build/ 2>/dev/null || true

echo "[*] Processing RustGo target binary and evaluation script..."
cp "$OPT_SCRIPT/fuzz-opt.sh" rustgo-opt-fuzz/
cp -r "$OPT_SCRIPT/in" rustgo-opt-fuzz/
mv rustgo-opt rustgo-opt-fuzz/

echo "[+] RustGo Opt Setup Complete."
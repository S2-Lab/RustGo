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
# Step 2: Build Target Program
# ==============================
echo "[*] Building fuzz target with LLVM IR emission"
RUSTFLAGS="--emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu --bin fuzz-target

echo "[*] Merging LLVM IR files"
python3 merge-base.py "$PROGRAM"

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
# Step 2: Perform Base analysis and instrumentation
# ==============================
echo "[*] Setting manual target functions."
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
for func in "${function_list[@]}"; do
    echo "  - Targeting function: $func"
    $base_command -f "$func" $basic_options
    sleep 1
done
icfg_nodes=$(grep -oP 'ICFGNode \d+' "$target_info_file" | awk '{print $2}' | paste -sd, -)
if [[ -z "$icfg_nodes" ]]; then
    echo "[-] No ICFG nodes were collected in $target_info_file" >&2
    exit 1
fi

echo "[*] Extracted ICFG nodes: $icfg_nodes"
echo "[*] Applying RustGo base analysis with manual targets..."
s2pidy total.ll -ablation-module-base -t="$icfg_nodes"


# ==============================
# Step 3: Generate Binary
# ==============================
echo "[*] Running LLVM optimization and instrumentation..."
opt -O2 AblationStudyModuleBase.ll -o AblationStudyModuleBaseOpt2.ll
opt -passes=sancov-module -sanitizer-coverage-level=3 -sanitizer-coverage-trace-pc-guard -sanitizer-coverage-prune-blocks=0 -S AblationStudyModuleBaseOpt2.ll -o sancov.ll
llvm-as sancov.ll -o output.bc
llc -filetype=obj output.bc -o output.o

echo "[*] Linking final binary..."
clang output.o -o rustgo-base -L"$LIBRARY_DIR" -lstd-bc021c2817d56996 \
    -lpthread -ldl -lm -L"$RUSTGO_BASE_LD" -lafl-llvm-rt \
    -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
    -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group 

# ==============================
# Step 4: Prepare output directories
# ==============================
echo "[*] Preparing output directories..."

# Clean and recreate rustgo-base-build & fuzz
rm -rf rustgo-base-build rustgo-base-fuzz
mkdir rustgo-base-build rustgo-base-fuzz

echo "[*] Moving and organizing build artifacts..."
mv ./*.ll rustgo-base-build/ 2>/dev/null || true
mv ./*.o rustgo-base-build/ 2>/dev/null || true
mv ./*.bc rustgo-base-build/ 2>/dev/null || true
mv ./*.txt rustgo-base-build/ 2>/dev/null || true

echo "[*] Processing RustGo target binary and evaluation script..."
cp "$BASE_SCRIPT/fuzz-base.sh" rustgo-base-fuzz/
cp -r "$BASE_SCRIPT/in" rustgo-base-fuzz/
mv rustgo-base rustgo-base-fuzz/

echo "[+] RustGo Base Setup Complete."

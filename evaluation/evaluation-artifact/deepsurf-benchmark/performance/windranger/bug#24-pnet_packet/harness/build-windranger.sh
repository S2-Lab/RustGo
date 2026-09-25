#!/bin/bash
PROGRAM="$1"
# ==============================
# Step 1: Setting up Environment
# ==============================
export WINDRANGER="$REPO_ROOT/evaluation/compared-fuzzer/build/windranger/WindRanger"
export CC=clang-12
export CXX=clang++-12
export LLVM_CONFIG=llvm-config-12
export AFL_REAL_LD=ld.lld-12
export WR_BB_TARGETS="$PWD/temp/BBtargets.txt"
export WR_TARGETS="src::poc" 
export PATH="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/bin:$PATH"
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH"
export LIBRARY_DIR="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export HARNESS_DIR="$PWD"

# ==============================
# Step 1: Build LLVM IR from fuzz target
# ==============================
echo "[*] Building fuzz target with LLVM IR emission"
RUSTFLAGS="-Zsanitizer=address --emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu
echo "[*] Merging LLVM IR files"
python3 merge-windranger.py $PROGRAM


# Target program build artifacts
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
# Step 2: WindRanger Instrumentation and Binary Generation
# ==============================
# LLVM Pass and Generate Object Code
opt-12 -load $WINDRANGER/afl-llvm-pass.so -O0 total.ll -o total_.ll
opt-12 -load $WINDRANGER/libcbi.so -O0 -targets=$WR_BB_TARGETS -tmpdir=$PWD total_.ll -o total_windranger.ll
llc-12 -relocation-model=pic -filetype=obj total_windranger.ll -o total_windranger.o

clang-12 total_windranger.o -o output -L$LIBRARY_DIR -lstd-1a95cddb5b8aaf6b -fsanitize=address \
        -L$LD_LIBRARY_PATH -L$WINDRANGER -lafl-llvm-rt -lpthread -ldl -lm \
        -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
        -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group 

rm total_.ll total.ll total_windranger.o total_windranger.ll
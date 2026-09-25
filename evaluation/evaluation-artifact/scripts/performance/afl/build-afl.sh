#!/bin/bash
PROGRAM="$1"
# ==============================
# Step 1: Setting up Environment
# ==============================

# Environment
export AFL="$REPO_ROOT/evaluation/compared-fuzzer/build/afl/AFL"
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH"
export PATH="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/bin:$PATH"
export LIBRARY_DIR="$HOME/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export HARNESS_DIR="$PWD"
RLIBS=$(find "$LIBRARY_DIR" -name "*.rlib")

# ==============================
# Step 1: Build LLVM IR from fuzz target
# ==============================
RUSTFLAGS="--emit=llvm-ir" cargo build --target x86_64-unknown-linux-gnu --bin fuzz-target
python3 merge-afl.py "$PROGRAM"

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
# Step 2: AFL Instrumentation and Binary Generation
# ==============================
# Generate LLVM IR and Object Code
opt-12 -O2 total.ll -o total.ll
opt-12 -load "$AFL/afl-llvm-pass.so" -O0 total.ll -o total_.ll
llc-12 -relocation-model=pic -filetype=obj total_.ll -o output.o

# Generate Binary
clang-12 output.o -o output \
  -L"$LIBRARY_DIR" -lstd-1a95cddb5b8aaf6b \
  -L"$AFL" -lafl-llvm-rt \
  -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
  -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group

# Clean
rm -f total_.ll total.ll output.o

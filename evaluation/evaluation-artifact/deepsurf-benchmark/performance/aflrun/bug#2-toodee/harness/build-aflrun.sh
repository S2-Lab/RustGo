#!/usr/bin/env bash
set -euo pipefail

PROGRAM="$1"

# ==============================
# Step 1: Setting up RustGo Base Environment
# ==============================
export LD_LIBRARY_PATH="$HOME/.rustup/toolchains/1.69.0-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:${LD_LIBRARY_PATH:-}"
export LIBRARY_DIR="$HOME/.rustup/toolchains/1.69.0-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib"
export PATH="$HOME/.rustup/toolchains/1.69.0-x86_64-unknown-linux-gnu/bin:$PATH"
export LLVM="$REPO_ROOT/evaluation/compared-fuzzer/build/AFLRun/llvm-install/bin"
export CC="$LLVM/clang"
export CXX="$LLVM/clang++"
export HARNESS_DIR="$PWD"

export AFLRUN="$REPO_ROOT/evaluation/compared-fuzzer/build/AFLRun/AFLRun"
export AFLRUN_BB_TARGETS="$PWD/temp/BBtargets.txt"
export AFLRUN_TARGETS="::"
export AFLRUN_TEMP_DIR="$PWD/temp"


### Build the target program
cargo clean
RUSTFLAGS="--emit=llvm-bc" cargo build --target x86_64-unknown-linux-gnu

### Merge LLVM IRs
$LLVM/llvm-link -o total.bc $(find $PWD/target/x86_64-unknown-linux-gnu/debug/deps -name "*.bc")

# Adapt lto marker pass to use the total LLVM IR
$LLVM/opt -load-pass-plugin=$AFLRUN/lto-marker.so -O0 total.bc -o total_.bc

BUILDS_DIR="$HARNESS_DIR/target/x86_64-unknown-linux-gnu/debug/build"
DLIBRARY_DIR="$HARNESS_DIR/target/x86_64-unknown-linux-gnu/debug/deps"
DRLIBS=$(find "$DLIBRARY_DIR" -name "lib*.rlib" -type f 2>/dev/null | sort -u)
RLIBS=""
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

# Generate Binary
$LLVM/clang -fsanitize=address -flto=full -fuse-ld="$LLVM/ld.lld" \
  -Wl,--plugin-opt=O1 \
  -Wl,--load-pass-plugin=$AFLRUN/SanitizerCoverageLTO.so -Wl,--allow-multiple-definition \
  $AFLRUN/afl-compiler-rt.o $AFLRUN/afl-llvm-rt-lto.o \
  -Wl,--dynamic-list=$AFLRUN/dynamic_list.txt \
  -L $LIBRARY_DIR -lstd-89bc084783fdc439 \
  "$LIBRARY_DIR/libcompiler_builtins-957b4aff41f8cd46.rlib" \
  -Wl,--start-group $RLIBS $FRLIBS $OBJECTS $DRLIBS \
  -lz -llzma -lbz2 -llz4 -lzstd -lpthread -ldl -lm -Wl,--end-group \
  total_.bc -o output

#!/bin/bash

# Build the patched Rust compiler, LLVM, and compiler-rt in a single x.py run.
set -euo pipefail

RUST_COMMIT="94f8ee1f7c27b749f84ce9dbea0762975aaa7770"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARTIFACT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$ARTIFACT_DIR/src"
RUST_DIR="${RUSTGO_RUST_DIR:-$SCRIPT_DIR/rust}"
JOBS="${JOBS:-$(nproc)}"
CHECK_ONLY=0

if [ "${1:-}" = "--check-only" ]; then
    CHECK_ONLY=1
fi

missing=0

require_file() {
    if [ ! -f "$1" ]; then
        echo "[MISSING] $1" >&2
        missing=1
    elif [ ! -s "$1" ]; then
        echo "[EMPTY] $1" >&2
        missing=1
    fi
}

require_dir() {
    if [ ! -d "$1" ]; then
        echo "[MISSING] $1" >&2
        missing=1
    fi
}

check_input_files() {
    echo "[Check artifact replacement files]"

    require_file "$SRC_DIR/rustc-1.64.0/config-file/config.toml"

    require_file "$SRC_DIR/rustc-1.64.0/compiler/rustc_codegen_ssa/src/mir/statement.rs"
    require_file "$SRC_DIR/rustc-1.64.0/compiler/rustc_codegen_ssa/src/mir/block.rs"
    require_file "$SRC_DIR/rustc-1.64.0/compiler/rustc_codegen_ssa/src/mir/operand.rs"
    require_file "$SRC_DIR/rustc-1.64.0/compiler/rustc_codegen_ssa/src/traits/builder.rs"
    require_file "$SRC_DIR/rustc-1.64.0/compiler/rustc_codegen_llvm/src/builder.rs"

    require_file "$SRC_DIR/rustc-1.64.0/src/bootstrap/native.rs"
    require_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp"

    require_file "$SRC_DIR/rustc-1.64.0/opt-files/CallPrinter.h"
    require_file "$SRC_DIR/rustc-1.64.0/opt-files/CallPrinter.cpp"
    require_file "$SRC_DIR/rustc-1.64.0/opt-files/PassBuilder.cpp"
    require_file "$SRC_DIR/rustc-1.64.0/opt-files/PassRegistry.def"
    require_file "$SRC_DIR/rustc-1.64.0/opt-files/heat-colors-graphs.ll"
    require_file "$SRC_DIR/rustc-1.64.0/opt-files/heat-colors-multigraph.ll"

    require_file "$SRC_DIR/RustGo_stuffs/afl-llvm-pass_rust/SanitizerCoverage.cpp"

    require_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/llvm/lib/Transforms/Instrumentation/SanitizerCoverageNoSafeCode.cpp"
    require_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/include/llvm/Transforms/Instrumentation/SanitizerCoverageNoSafeCode.h"
    require_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/llvm/lib/Transforms/Instrumentation/CMakeLists.txt"
    require_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/llvm/lib/Passes/PassRegistry.def"
    require_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/llvm/lib/Passes/PassBuilder.cpp"
    require_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/include/llvm/InitializePasses.h"
    require_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/llvm/lib/Transforms/Instrumentation/Instrumentation.cpp"

    if [ "$missing" -ne 0 ]; then
        echo "[ERROR] Required replacement files are missing. Restore them under $SRC_DIR before building." >&2
        exit 1
    fi
}

checkout_rust() {
    if [ ! -d "$RUST_DIR/.git" ]; then
        echo "[Clone rust $RUST_COMMIT]"
        git clone https://github.com/rust-lang/rust.git "$RUST_DIR"
        git -C "$RUST_DIR" reset --hard "$RUST_COMMIT"
    else
        current_commit="$(git -C "$RUST_DIR" rev-parse HEAD)"
        if [ "$current_commit" != "$RUST_COMMIT" ]; then
            echo "[ERROR] $RUST_DIR is at $current_commit, expected $RUST_COMMIT." >&2
            echo "        Use a clean rust checkout or set RUSTGO_RUST_DIR to another directory." >&2
            exit 1
        fi
    fi

    echo "[Initialize llvm-project before applying LLVM patches]"
    git -C "$RUST_DIR" submodule update --init --recursive src/llvm-project
}

check_rust_targets() {
    echo "[Check rust checkout target directories]"

    require_dir "$RUST_DIR/compiler/rustc_codegen_ssa/src/mir"
    require_dir "$RUST_DIR/compiler/rustc_codegen_ssa/src/traits"
    require_dir "$RUST_DIR/compiler/rustc_codegen_llvm/src"
    require_dir "$RUST_DIR/src/bootstrap"
    require_dir "$RUST_DIR/src/llvm-project/llvm/lib/Transforms/Instrumentation"
    require_dir "$RUST_DIR/src/llvm-project/llvm/include/llvm/Transforms/Instrumentation"
    require_dir "$RUST_DIR/src/llvm-project/llvm/lib/Analysis"
    require_dir "$RUST_DIR/src/llvm-project/llvm/include/llvm/Analysis"
    require_dir "$RUST_DIR/src/llvm-project/llvm/lib/Passes"
    require_dir "$RUST_DIR/src/llvm-project/llvm/include/llvm"
    require_dir "$RUST_DIR/src/llvm-project/llvm/test/Other"

    if [ "$missing" -ne 0 ]; then
        echo "[ERROR] Rust checkout is missing required target directories." >&2
        exit 1
    fi
}

copy_file() {
    src="$1"
    dst="$2"
    echo "[Copy] $src -> $dst"
    cp "$src" "$dst"
}

apply_replacements() {
    echo "[Apply Rust compiler patches]"
    copy_file "$SRC_DIR/rustc-1.64.0/config-file/config.toml" "$RUST_DIR/config.toml"
    copy_file "$SRC_DIR/rustc-1.64.0/compiler/rustc_codegen_ssa/src/mir/statement.rs" "$RUST_DIR/compiler/rustc_codegen_ssa/src/mir/statement.rs"
    copy_file "$SRC_DIR/rustc-1.64.0/compiler/rustc_codegen_ssa/src/mir/block.rs" "$RUST_DIR/compiler/rustc_codegen_ssa/src/mir/block.rs"
    copy_file "$SRC_DIR/rustc-1.64.0/compiler/rustc_codegen_ssa/src/mir/operand.rs" "$RUST_DIR/compiler/rustc_codegen_ssa/src/mir/operand.rs"
    copy_file "$SRC_DIR/rustc-1.64.0/compiler/rustc_codegen_ssa/src/traits/builder.rs" "$RUST_DIR/compiler/rustc_codegen_ssa/src/traits/builder.rs"
    copy_file "$SRC_DIR/rustc-1.64.0/compiler/rustc_codegen_llvm/src/builder.rs" "$RUST_DIR/compiler/rustc_codegen_llvm/src/builder.rs"

    echo "[Apply compiler-rt and LLVM patches]"
    copy_file "$SRC_DIR/rustc-1.64.0/src/bootstrap/native.rs" "$RUST_DIR/src/bootstrap/native.rs"
    copy_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp" "$RUST_DIR/src/llvm-project/llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp"

    copy_file "$SRC_DIR/rustc-1.64.0/opt-files/CallPrinter.h" "$RUST_DIR/src/llvm-project/llvm/include/llvm/Analysis/CallPrinter.h"
    copy_file "$SRC_DIR/rustc-1.64.0/opt-files/CallPrinter.cpp" "$RUST_DIR/src/llvm-project/llvm/lib/Analysis/CallPrinter.cpp"
    copy_file "$SRC_DIR/rustc-1.64.0/opt-files/PassBuilder.cpp" "$RUST_DIR/src/llvm-project/llvm/lib/Passes/PassBuilder.cpp"
    copy_file "$SRC_DIR/rustc-1.64.0/opt-files/PassRegistry.def" "$RUST_DIR/src/llvm-project/llvm/lib/Passes/PassRegistry.def"
    copy_file "$SRC_DIR/rustc-1.64.0/opt-files/heat-colors-graphs.ll" "$RUST_DIR/src/llvm-project/llvm/test/Other/heat-colors-graphs.ll"
    copy_file "$SRC_DIR/rustc-1.64.0/opt-files/heat-colors-multigraph.ll" "$RUST_DIR/src/llvm-project/llvm/test/Other/heat-colors-multigraph.ll"

    copy_file "$SRC_DIR/RustGo_stuffs/afl-llvm-pass_rust/SanitizerCoverage.cpp" "$RUST_DIR/src/llvm-project/llvm/lib/Transforms/Instrumentation/SanitizerCoverage.cpp"

    echo "[Apply no-safe-code sanitizer coverage pass]"
    copy_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/llvm/lib/Transforms/Instrumentation/SanitizerCoverageNoSafeCode.cpp" "$RUST_DIR/src/llvm-project/llvm/lib/Transforms/Instrumentation/SanitizerCoverageNoSafeCode.cpp"
    copy_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/include/llvm/Transforms/Instrumentation/SanitizerCoverageNoSafeCode.h" "$RUST_DIR/src/llvm-project/llvm/include/llvm/Transforms/Instrumentation/SanitizerCoverageNoSafeCode.h"
    copy_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/llvm/lib/Transforms/Instrumentation/CMakeLists.txt" "$RUST_DIR/src/llvm-project/llvm/lib/Transforms/Instrumentation/CMakeLists.txt"
    copy_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/llvm/lib/Passes/PassRegistry.def" "$RUST_DIR/src/llvm-project/llvm/lib/Passes/PassRegistry.def"
    copy_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/llvm/lib/Passes/PassBuilder.cpp" "$RUST_DIR/src/llvm-project/llvm/lib/Passes/PassBuilder.cpp"
    copy_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/include/llvm/InitializePasses.h" "$RUST_DIR/src/llvm-project/llvm/include/llvm/InitializePasses.h"
    copy_file "$SRC_DIR/rustc-1.64.0/src/llvm-project/llvm/lib/Transforms/Instrumentation/Instrumentation.cpp" "$RUST_DIR/src/llvm-project/llvm/lib/Transforms/Instrumentation/Instrumentation.cpp"

    rm -f "$RUST_DIR/build/x86_64-unknown-linux-gnu/llvm/llvm-finished-building"
}

verify_stage1_sysroot() {
    local target_lib="$RUST_DIR/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/x86_64-unknown-linux-gnu/lib"

    echo "[Verify stage1 sysroot]"
    if [ ! -x "$RUST_DIR/build/x86_64-unknown-linux-gnu/stage1/bin/rustc" ]; then
        echo "[ERROR] Missing stage1 rustc." >&2
        exit 1
    fi

    if ! find "$target_lib" -maxdepth 1 -type f -name 'libcore*.rlib' | grep -q .; then
        echo "[ERROR] Missing libcore in stage1 sysroot: $target_lib" >&2
        exit 1
    fi

    if ! find "$target_lib" -maxdepth 1 -type f -name 'libstd*.rlib' | grep -q .; then
        echo "[ERROR] Missing libstd in stage1 sysroot: $target_lib" >&2
        exit 1
    fi

    if ! find "$target_lib" -maxdepth 1 -type f -name 'libcompiler_builtins*.rlib' | grep -q .; then
        echo "[ERROR] Missing libcompiler_builtins in stage1 sysroot: $target_lib" >&2
        exit 1
    fi

    if ! find "$target_lib" -maxdepth 1 -type f -name 'libproc_macro*' | grep -q .; then
        echo "[ERROR] Missing libproc_macro in stage1 sysroot: $target_lib" >&2
        exit 1
    fi
}

check_input_files

if [ "$CHECK_ONLY" -eq 1 ]; then
    echo "[Check-only finished]"
    exit 0
fi

checkout_rust
check_rust_targets
apply_replacements

echo "[Build stage1 Rust compiler, LLVM, and compiler-rt with RustGo metadata disabled]"
cd "$RUST_DIR"
RUSTGO_METADATA_MODE=off RUSTGO_ENABLE_METADATA=0 \
    python3 ./x.py build --stage 1 compiler/rustc -j "$JOBS"

echo "[Build instrumented stage1 standard library with RustGo metadata enabled]"
rm -rf "$RUST_DIR/build/x86_64-unknown-linux-gnu/stage1-std"
RUSTGO_METADATA_MODE=full RUSTGO_ENABLE_METADATA=1 \
    python3 ./x.py build --stage 1 library/std library/proc_macro -j "$JOBS"
verify_stage1_sysroot
echo "[Build Finished]"

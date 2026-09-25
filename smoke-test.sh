#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TARGET="x86_64-unknown-linux-gnu"
SYSROOT_LIB="$ROOT/build/rust/build/x86_64-unknown-linux-gnu/stage1/lib/rustlib/$TARGET/lib"

source_env() {
    set +u
    pushd "$ROOT/env" >/dev/null
    # shellcheck source=env.sh
    source ./env.sh
    popd >/dev/null
    # shellcheck source=evaluation/evaluation-artifact/env.sh
    source "$ROOT/evaluation/evaluation-artifact/env.sh"
    set -u
}

require_glob() {
    local pattern=$1
    if ! compgen -G "$pattern" >/dev/null; then
        echo "[ERROR] Missing expected file: $pattern" >&2
        exit 1
    fi
}

check_toolchain() {
    echo "[1/5] Checking RustGo Rust toolchain"
    rustc --version
    cargo --version
    llvm-config --version
}

check_sysroot() {
    echo "[2/5] Checking metadata-enabled stage1 sysroot"
    require_glob "$SYSROOT_LIB/libcore-*.rlib"
    require_glob "$SYSROOT_LIB/libstd-*.rlib"
    require_glob "$SYSROOT_LIB/libcompiler_builtins-*.rlib"
    require_glob "$SYSROOT_LIB/libproc_macro-*"
}

check_s2pidy() {
    echo "[3/5] Checking S2pidy"
    test -x "$ROOT/build/SVF/Release-build/bin/s2pidy"
    s2pidy --help >/dev/null || true
}

check_rustgo() {
    echo "[4/5] Checking RustGo runtime"
    test -d "$ROOT/build/RustGo/RuntimeLib_RustGo"
    require_glob "$ROOT/build/RustGo/RuntimeLib_RustGo/afl.rs-0.12.9/afl-llvm-rt/libafl-llvm-rt.a"
}

build_unicode_process() {
    echo "[5/5] Building one RustGo smoke benchmark: unicode-process"
    cd "$ROOT/evaluation/evaluation-artifact/performance"
    bash "$ROOT/evaluation/evaluation-artifact/performance/build_common.sh" unicode-process rustgo
    test -x "$ROOT/evaluation/evaluation-artifact/performance/unicode-process-src/rustgo-fuzz/rustgo-target"
}

main() {
    source_env
    check_toolchain
    check_sysroot
    check_s2pidy
    check_rustgo
    build_unicode_process
    echo "[OK] RustGo prebuilt Docker smoke test completed."
}

main "$@"

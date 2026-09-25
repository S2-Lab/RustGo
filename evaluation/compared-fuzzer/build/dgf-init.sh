#!/usr/bin/env bash

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$ROOT/dgf-build-logs"

declare -a SUCCEEDED=()
declare -a FAILED=()

require_file() {
  local path="$1"

  if [ ! -s "$path" ]; then
    echo "Required build artifact is missing or empty: $path" >&2
    return 1
  fi
}

require_executable() {
  local path="$1"

  if [ ! -x "$path" ]; then
    echo "Required executable is missing or not executable: $path" >&2
    return 1
  fi
}

missing_dgf_prereq() {
  local cmd

  for cmd in wget clang-11 clang++-11 llvm-config-11 clang-12 clang++-12 llvm-config-12 ld.lld ld.lld-11 ld.lld-12; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
      return 0
    fi
  done

  return 1
}

ensure_dgf_prereqs() {
  if ! missing_dgf_prereq; then
    return 0
  fi

  if ! command -v apt-get >/dev/null 2>&1; then
    echo "Missing DGF build prerequisites and apt-get is unavailable." >&2
    return 1
  fi

  if [ "$(id -u)" -ne 0 ]; then
    echo "Missing DGF build prerequisites. Re-run dgf_init.sh as root or install wget, clang/llvm/lld 11 and 12." >&2
    return 1
  fi

  echo "[0/7] Installing DGF build prerequisites"
  apt-get update
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    wget xz-utils \
    clang-11 llvm-11-dev lld-11 \
    clang-12 llvm-12-dev lld-12 \
    lld \
    libboost-program-options-dev libboost-graph-dev libboost-regex-dev \
    libtinfo5

  missing_dgf_prereq && {
    echo "DGF prerequisite installation completed but required tools are still missing." >&2
    return 1
  }
}

clean_path() {
  local path="$1"

  if [ -e "$path" ]; then
    rm -rf "$path"
  fi
}

run_script() {
  local dir="$1"
  local script="$2"
  local script_path="$ROOT/$dir/$script"

  if [ ! -f "$script_path" ]; then
    echo "Required build script not found: $script_path" >&2
    return 1
  fi

  (
    cd "$ROOT/$dir" &&
      bash "$script"
  )
}

run_optional_script() {
  local dir="$1"
  local script="$2"

  if [ -f "$ROOT/$dir/$script" ]; then
    run_script "$dir" "$script"
  fi
}

source_rustgo_env() {
  local env_dir="$ROOT/../../../env"
  local previous_dir="$PWD"
  local status

  if [ ! -f "$env_dir/env.sh" ]; then
    return 0
  fi

  cd "$env_dir" || return 1
  set +u
  # shellcheck disable=SC1091
  source ./env.sh
  status=$?
  set -u
  cd "$previous_dir" || return 1

  return "$status"
}

build_afl() {
  clean_path "$ROOT/afl/AFL" &&
    run_script "afl" "afl-build.sh" &&
    run_optional_script "afl" "afl-rust-compiler.sh" &&
    require_executable "$ROOT/afl/AFL/afl-fuzz" &&
    require_file "$ROOT/afl/AFL/libafl-llvm-rt.a"
}

build_afl_rs() {
  source_rustgo_env &&
    clean_path "$ROOT/afl.rs/afl.rs-original" &&
    run_script "afl.rs" "aflrs-build.sh" &&
    require_file "$ROOT/afl.rs/afl.rs-original/RuntimeLib_Orig/afl.rs-0.12.9/afl-llvm-rt/libafl-llvm-rt.a"
}

build_aflgo() {
  clean_path "$ROOT/aflgo/aflgo" &&
    run_script "aflgo" "aflgo-build.sh" &&
    run_optional_script "aflgo" "aflgo-rust-compiler.sh" &&
    require_executable "$ROOT/aflgo/aflgo/afl-fuzz" &&
    require_file "$ROOT/aflgo/aflgo/libafl-llvm-rt.a"
}

build_aflrun() {
  clean_path "$ROOT/AFLRun/AFLRun" &&
    clean_path "$ROOT/AFLRun/build" &&
    clean_path "$ROOT/AFLRun/binutils-2.39" &&
    clean_path "$ROOT/AFLRun/binutils.tar.gz" &&
    clean_path "$ROOT/AFLRun/cmake-3.25.1-linux-x86_64" &&
    clean_path "$ROOT/AFLRun/cmake.tar.gz" &&
    clean_path "$ROOT/AFLRun/llvm-project" &&
    clean_path "$ROOT/AFLRun/llvm-install" &&
    run_script "AFLRun" "AFLRun-build.sh" &&
    run_optional_script "AFLRun" "aflrun-rust-compiler.sh" &&
    require_executable "$ROOT/AFLRun/AFLRun/afl-fuzz" &&
    require_executable "$ROOT/AFLRun/AFLRun/aflrun-cc" &&
    require_file "$ROOT/AFLRun/AFLRun/afl-compiler-rt.o" &&
    require_file "$ROOT/AFLRun/AFLRun/SanitizerCoverageLTO.so"
}

build_lyso() {
  clean_path "$ROOT/lyso/magma/fuzzers/lyso/llvm-11" &&
    clean_path "$ROOT/lyso/magma/fuzzers/lyso/clang+llvm-11.0.1-x86_64-linux-gnu-ubuntu-16.04.tar.xz" &&
    run_script "lyso" "lyso-build.sh" &&
    require_executable "$ROOT/lyso/magma/fuzzers/lyso/repo/afl-fuzz" &&
    require_file "$ROOT/lyso/magma/fuzzers/lyso/repo/libafl-llvm-rt.a"
}

build_panickiller() {
  clean_path "$ROOT/panickiller/ERASan" &&
    clean_path "$ROOT/panickiller/afl.rs" &&
    run_script "panickiller" "panickiller-build.sh" &&
    require_executable "$ROOT/panickiller/ERASan/build/SVF/Release-build/bin/erasan" &&
    require_file "$ROOT/panickiller/afl.rs/target/release/libafl.rlib"
}

build_windranger() {
  clean_path "$ROOT/windranger/WindRanger" &&
    run_script "windranger" "windranger-build.sh" &&
    run_optional_script "windranger" "windranger-rust-compiler.sh" &&
    require_executable "$ROOT/windranger/WindRanger/afl-fuzz" &&
    require_file "$ROOT/windranger/WindRanger/libafl-llvm-rt.a"
}

run_fuzzer() {
  local number="$1"
  local name="$2"
  local log_name="$3"
  local build_function="$4"
  local log_file="$LOG_DIR/$log_name.log"

  printf '[%s/7] Building %s... ' "$number" "$name"

  if "$build_function" >"$log_file" 2>&1; then
    echo "SUCCESS"
    SUCCEEDED+=("$name")
  else
    echo "FAILED (log: $log_file)"
    FAILED+=("$name")
  fi
}

mkdir -p "$LOG_DIR"
cd "$ROOT"
ensure_dgf_prereqs

run_fuzzer 1 "AFL" "afl" build_afl
run_fuzzer 2 "AFL.rs" "afl.rs" build_afl_rs
run_fuzzer 3 "AFLGo" "aflgo" build_aflgo
run_fuzzer 4 "AFLRun" "aflrun" build_aflrun
run_fuzzer 5 "Lyso" "lyso" build_lyso
run_fuzzer 6 "PanicKiller" "panickiller" build_panickiller
run_fuzzer 7 "WindRanger" "windranger" build_windranger

echo
echo "[8] FishFuzz Pre-builtin"
echo "Build summary"
echo "  Success: ${#SUCCEEDED[@]} (${SUCCEEDED[*]:-none})"
echo "  Failed:  ${#FAILED[@]} (${FAILED[*]:-none})"
echo "  Logs:    $LOG_DIR"

if ((${#FAILED[@]} > 0)); then
  exit 1
fi

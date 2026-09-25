#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
AFLRUN_WORKDIR="$SCRIPT_DIR"
LLVM_PROJECT="$AFLRUN_WORKDIR/llvm-project"
LLVM_BUILD="$AFLRUN_WORKDIR/build"
LLVM_INSTALL="$AFLRUN_WORKDIR/llvm-install"
AFLRUN_SOURCE="$AFLRUN_WORKDIR/AFLRun"
LLVM_COMMIT="4a2c05b05ed07f1f620e94f6524a8b4b2760a0b1"
JOBS="${JOBS:-$(nproc)}"
unset DEBUG

# Clone LLVM project.
if [[ ! -d "$LLVM_PROJECT/.git" ]]; then
  git clone --depth=1 https://github.com/llvm/llvm-project.git "$LLVM_PROJECT"
fi
git -C "$LLVM_PROJECT" fetch origin --depth=1 "$LLVM_COMMIT"
git -C "$LLVM_PROJECT" checkout --detach "$LLVM_COMMIT"

# Download binutils.
if [[ ! -f "$AFLRUN_WORKDIR/binutils.tar.gz" ]]; then
  wget https://ftp.gnu.org/gnu/binutils/binutils-2.39.tar.gz \
    -O "$AFLRUN_WORKDIR/binutils.tar.gz"
fi
if [[ ! -d "$AFLRUN_WORKDIR/binutils-2.39" ]]; then
  tar -xf "$AFLRUN_WORKDIR/binutils.tar.gz" -C "$AFLRUN_WORKDIR"
fi

# Download CMake.
if [[ ! -f "$AFLRUN_WORKDIR/cmake.tar.gz" ]]; then
  wget https://github.com/Kitware/CMake/releases/download/v3.25.1/cmake-3.25.1-linux-x86_64.tar.gz \
    -O "$AFLRUN_WORKDIR/cmake.tar.gz"
fi
if [[ ! -d "$AFLRUN_WORKDIR/cmake-3.25.1-linux-x86_64" ]]; then
  tar -xf "$AFLRUN_WORKDIR/cmake.tar.gz" -C "$AFLRUN_WORKDIR"
fi

# Compile and install LLVM project.
mkdir -p "$LLVM_BUILD"
CC=gcc CXX=g++ "$AFLRUN_WORKDIR/cmake-3.25.1-linux-x86_64/bin/cmake" \
  -S "$LLVM_PROJECT/llvm" \
  -B "$LLVM_BUILD" \
  -G Ninja \
  -DLLVM_BINUTILS_INCDIR="$AFLRUN_WORKDIR/binutils-2.39/include" \
  -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=host \
  -DLLVM_ENABLE_PROJECTS="clang;compiler-rt;lld" \
  -DCMAKE_INSTALL_PREFIX="$LLVM_INSTALL" \
  -DLLVM_INSTALL_BINUTILS_SYMLINKS=ON
ninja -C "$LLVM_BUILD" -j "$JOBS"
ninja -C "$LLVM_BUILD" install

if [[ ! -d "$AFLRUN_SOURCE/.git" ]]; then
  git clone https://github.com/Mem2019/AFLRun.git "$AFLRUN_SOURCE"
fi
git -C "$AFLRUN_SOURCE" submodule update --init robin-hood-hashing/

# Newer Ubuntu clang versions promote a couple of AFLRun warnings to hard
# failures because the upstream debug build uses -Werror. Keep the diagnostics
# visible, but do not let these legacy warnings stop the artifact setup.
sed -i \
  -e 's/-Wall -Wextra -Werror/-Wall -Wextra -Wno-error=incompatible-function-pointer-types -Wno-error=unused-but-set-variable/g' \
  "$AFLRUN_SOURCE/GNUmakefile"

export PATH="$LLVM_INSTALL/bin:$PATH"
export LLVM_CONFIG="$LLVM_INSTALL/bin/llvm-config"
export CC="$LLVM_INSTALL/bin/clang"
export CXX="$LLVM_INSTALL/bin/clang++"

for tool in "$LLVM_CONFIG" "$CC" "$CXX" "$LLVM_INSTALL/bin/opt" \
  "$LLVM_INSTALL/bin/llvm-link" "$LLVM_INSTALL/bin/ld.lld"; do
  if [[ ! -x "$tool" ]]; then
    echo "error: required LLVM tool is missing or not executable: $tool" >&2
    exit 1
  fi
done

cp "$AFLRUN_WORKDIR/AFLRun_working_directory/SanitizerCoverageLTO.so.cc" \
  "$AFLRUN_SOURCE/instrumentation/SanitizerCoverageLTO.so.cc"

make -C "$AFLRUN_SOURCE" clean
make -C "$AFLRUN_SOURCE" -j "$JOBS" all

# AFLRun treats LTO support as optional in its default build. Build the files
# required by the Rust harness explicitly so a partial build cannot pass.
make -C "$AFLRUN_SOURCE" -f GNUmakefile.llvm -j "$JOBS" \
  afl-compiler-rt.o \
  lto-marker.so \
  SanitizerCoverageLTO.so

required_outputs=(
  afl-compiler-rt.o
  afl-llvm-rt-lto.o
  lto-marker.so
  SanitizerCoverageLTO.so
)

for output in "${required_outputs[@]}"; do
  if [[ ! -s "$AFLRUN_SOURCE/$output" ]]; then
    echo "error: AFLRun build did not produce $AFLRUN_SOURCE/$output" >&2
    exit 1
  fi
done

for plugin in lto-marker.so SanitizerCoverageLTO.so; do
  if ! plugin_output="$(
    "$LLVM_INSTALL/bin/opt" \
      -load-pass-plugin="$AFLRUN_SOURCE/$plugin" \
      --version 2>&1
  )"; then
    echo "$plugin_output" >&2
    echo "error: failed to load AFLRun plugin: $plugin" >&2
    exit 1
  fi
  if [[ "$plugin_output" == *"Failed to load passes"* ]]; then
    echo "$plugin_output" >&2
    echo "error: AFLRun plugin is incompatible with $LLVM_INSTALL/bin/opt: $plugin" >&2
    exit 1
  fi
done

echo "[+] AFLRun and required LTO components built successfully."

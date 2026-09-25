#!/bin/bash

# immediately stop building if error occurs
set -e

# install prerequisite package
echo "build ERASan"

export PK_HOME=$PWD

# set current working directory
export PK_WORKING_DIR_SVF=$PWD/panickiller_working_directory/SVF
export PK_WORKING_DIR_AFL=$PWD/panickiller_working_directory/AFLplusplus

# build ERASan
if [ ! -d ERASan/.git ]; then
    git clone https://github.com/S2-Lab/ERASan.git
fi
export ERASAN=$PWD/ERASan
export ERASAN_SVF=$ERASAN/build/SVF
export ERASAN_SVF_INC=$ERASAN_SVF/include/ERAsan
export ERASAN_SVF_LIB=$ERASAN_SVF/lib/ERAsan
export ERASAN_SVF_TOOLS=$ERASAN_SVF/tools/ERAsan

if [ ! -x "$ERASAN_SVF/Release-build/bin/erasan" ]; then
    cd $ERASAN/env && source env.sh && cd ../build
    ./SVF-build.sh

    rm $ERASAN_SVF_INC/util.h && rm $ERASAN_SVF_LIB/util.cpp && rm $ERASAN_SVF_TOOLS/erasan.cpp
    cp $PK_WORKING_DIR_SVF/util.h $ERASAN_SVF_INC
    cp $PK_WORKING_DIR_SVF/util.cpp $ERASAN_SVF_LIB
    cp $PK_WORKING_DIR_SVF/erasan.cpp $ERASAN_SVF_TOOLS

    # build SVF
    cd $ERASAN_SVF
    echo "[Build SVF]"
    ./build.sh
else
    echo "[*] Reusing existing ERASan build at $ERASAN_SVF"
fi

# now build panickiller engine (afl++)
cd $PK_HOME
# git clone of afl.rs
rm -rf afl.rs
git clone https://github.com/rust-fuzz/afl.rs.git
cd afl.rs
git reset --hard b0b398bde12ae1943b26b26e74e348f3c812025f

# git clone of AFLplusplus in afl.rs
export AFL_RS=$PWD
rm -rf AFLplusplus 
git clone https://github.com/AFLplusplus/AFLplusplus.git 
cd AFLplusplus

git reset --hard 143c9d175e9357ba548413ee7dcee6a8de23f733
export AFLplusplus_DIR_INST=$PWD/instrumentation
export AFLplusplus_DIR_INC=$PWD/include
export AFLplusplus_DIR_SRC=$PWD/src

replace_if_nonempty() {
    local src="$1"
    local dst="$2"
    if [ -s "$src" ]; then
        rm -f "$dst"
        cp "$src" "$dst"
    else
        echo "[*] Keeping upstream AFL++ file because patch payload is empty: $src"
    fi
}

replace_if_nonempty "$PK_WORKING_DIR_AFL/instrumentation/afl-compiler-rt.o.c" "$AFLplusplus_DIR_INST/afl-compiler-rt.o.c"
replace_if_nonempty "$PK_WORKING_DIR_AFL/include/afl-fuzz.h" "$AFLplusplus_DIR_INC/afl-fuzz.h"
replace_if_nonempty "$PK_WORKING_DIR_AFL/include/config.h" "$AFLplusplus_DIR_INC/config.h"
replace_if_nonempty "$PK_WORKING_DIR_AFL/include/sharedmem.h" "$AFLplusplus_DIR_INC/sharedmem.h"
replace_if_nonempty "$PK_WORKING_DIR_AFL/src/afl-fuzz-queue.c" "$AFLplusplus_DIR_SRC/afl-fuzz-queue.c"
replace_if_nonempty "$PK_WORKING_DIR_AFL/src/afl-fuzz-run.c" "$AFLplusplus_DIR_SRC/afl-fuzz-run.c"
replace_if_nonempty "$PK_WORKING_DIR_AFL/src/afl-fuzz.c" "$AFLplusplus_DIR_SRC/afl-fuzz.c"
replace_if_nonempty "$PK_WORKING_DIR_AFL/src/afl-sharedmem.c" "$AFLplusplus_DIR_SRC/afl-sharedmem.c"

# build afl.rs
cd $AFL_RS
ARTIFACT_ENV="$PK_HOME/../../../../env/env.sh"
if [ -f "$ARTIFACT_ENV" ]; then
    old_pwd="$PWD"
    cd "$(dirname "$ARTIFACT_ENV")"
    source ./env.sh
    cd "$old_pwd"
fi
export LLVM_CONFIG="${LLVM_CONFIG:-llvm-config-12}"
export AFL_NO_X86=1
cargo build --release

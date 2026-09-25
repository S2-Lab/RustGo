#!/bin/bash
export CURR=$PWD
export ENV_DIR=$CURR/../../../../env
pushd $ENV_DIR
source env.sh
popd
mkdir afl.rs-original
cd afl.rs-original
mkdir RuntimeLib_Orig
export ORIG_RUNTIME=$PWD/RuntimeLib_Orig
# git clone of afl.rs
git clone https://github.com/rust-fuzz/afl.rs.git
cd afl.rs
git reset --hard b0b398bde12ae1943b26b26e74e348f3c812025f

# git clone of AFLplusplus in afl.rs
export AFL_RS=$PWD
rm -rf AFLplusplus 
git clone https://github.com/AFLplusplus/AFLplusplus.git 
cd AFLplusplus

git reset --hard 143c9d175e9357ba548413ee7dcee6a8de23f733

# env.sh exports the RustGo cargo path.
: "${CARGO:?CARGO must be set by env.sh}"


# build afl.rs
cd $AFL_RS
cargo build

export AFL_RUNTIME_LIB=$HOME/.local/share/afl.rs/rustc-1.64.0-dev/afl.rs-0.12.9/
cp -r $AFL_RUNTIME_LIB $ORIG_RUNTIME

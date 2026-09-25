#!/bin/bash

# immediately stop building if error occurs
set -e

# Build Titan && you should have built Beacon first
echo "##Set Rust Compiler##"

# If you don't have nightly-2017-11-01, you can install it by running the following command
#rustup install nightly-2021-07-26
rustup default nightly-2021-07-26-x86_64-unknown-linux-gnu

# Set LD_LIBRARY_PATH to your nightly-2021-07-26 version
export LD_LIBRARY_PATH=~/.rustup/toolchains/nightly-2021-07-26-x86_64-unknown-linux-gnu/lib/rustlib/x86_64-unknown-linux-gnu/lib:$LD_LIBRARY_PATH
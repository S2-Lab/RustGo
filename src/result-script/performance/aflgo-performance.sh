#!/usr/bin/env bash

set -uo pipefail

FUZZER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export FUZZER_NAME="aflgo"
export FUZZER_DIR

source "$FUZZER_DIR/../run-performance-benchmark.sh"

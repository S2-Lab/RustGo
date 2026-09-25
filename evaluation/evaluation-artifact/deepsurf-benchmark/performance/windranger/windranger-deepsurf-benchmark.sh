#!/usr/bin/env bash

set -uo pipefail

FUZZER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export FUZZER_NAME="windranger"
export FUZZER_DIR
export RUN_SUBDIR="${RUN_SUBDIR:-harness}"
export EVAL_SCRIPT_NAME="${EVAL_SCRIPT_NAME:-fuzz-windranger.sh}"

source "$FUZZER_DIR/../run-deepsurf-benchmark.sh"

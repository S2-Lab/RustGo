#!/usr/bin/env bash

set -uo pipefail

VARIANT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export VARIANT_NAME="sync"
export VARIANT_DIR
export RUN_SUBDIR="${RUN_SUBDIR:-harness}"
export EVAL_SCRIPT_NAME="${EVAL_SCRIPT_NAME:-fuzz-sync.sh}"

source "$VARIANT_DIR/../run-deepsurf-ablation.sh"

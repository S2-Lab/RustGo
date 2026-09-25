#!/usr/bin/env bash

set -uo pipefail

VARIANT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export VARIANT_NAME="dissafe"
export VARIANT_DIR

source "$VARIANT_DIR/../run-ablation-benchmark.sh"

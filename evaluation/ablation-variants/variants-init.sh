#!/usr/bin/env bash

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$ROOT/variant-build-logs"

declare -a SUCCEEDED=()
declare -a FAILED=()

run_variant() {
  local number="$1"
  local name="$2"
  local variant="$3"
  local setup_script="$4"
  local script_path="$ROOT/$variant/$setup_script"
  local log_file="$LOG_DIR/$variant.log"

  printf '[%s/5] Building %s... ' "$number" "$name"

  if [ ! -f "$script_path" ]; then
    echo "FAILED (setup script not found: $script_path)"
    FAILED+=("$name")
    return
  fi

  if (
    cd "$ROOT/$variant" &&
      bash "$setup_script"
  ) >"$log_file" 2>&1; then
    echo "SUCCESS"
    SUCCEEDED+=("$name")
  else
    echo "FAILED (log: $log_file)"
    FAILED+=("$name")
  fi
}

mkdir -p "$LOG_DIR"

run_variant 1 "Dissafe" "dissafe" "RG-Dissafe-setup.sh"
run_variant 2 "Base" "base" "RG-Base-setup.sh"
run_variant 3 "Opt" "opt" "RG-Opt-setup.sh"
run_variant 4 "Prun" "prun" "RG-Prun-setup.sh"
run_variant 5 "Sync" "sync" "RG-Sync-setup.sh"

echo
echo "Build summary"
echo "  Success: ${#SUCCEEDED[@]} (${SUCCEEDED[*]:-none})"
echo "  Failed:  ${#FAILED[@]} (${FAILED[*]:-none})"
echo "  Logs:    $LOG_DIR"

if ((${#FAILED[@]} > 0)); then
  exit 1
fi

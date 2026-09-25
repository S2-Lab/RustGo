#!/usr/bin/env bash

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TARGET_LIST=(
    "nom"
    "json"
    "serde-yaml"
    "unicode-process"
    "unicode-streaming"
    "data-encoding"
    "chrono"
    "mqtt-broker"
    "qcms"
    "httparse"
    "quick-xml"
    "lz4_flex"
    "gimli"
)

count_target_optimization() {
    local target=$1
    local target_dir="$SCRIPT_DIR/$target-src"
    local -a toggle_files=()

    if [[ ! -d "$target_dir" ]]; then
        return 0
    fi

    while IFS= read -r -d '' toggle_file; do
        toggle_files+=("$toggle_file")
    done < <(find "$target_dir" -path '*/rustgo-fuzz/ToggleInfoNew.txt' -type f -print0)

    if ((${#toggle_files[@]} == 0)); then
        return 0
    fi

    awk 'NF { count++ } END { print count + 0 }' "${toggle_files[@]}"
}

extract_log_value() {
    local log_file=$1
    local label=$2

    if [[ ! -f "$log_file" ]]; then
        return 0
    fi

    awk -v label="[$label]" '
        index($0, label) {
            value = $0
            sub(/^.*: */, "", value)
            sub(/ *\*.*$/, "", value)
            gsub(/%/, "", value)
        }
        END {
            if (value != "") {
                print value
            }
        }
    ' "$log_file"
}

echo "program,target_optimization_count,all_basic_blocks,reachable_basic_blocks,pruning_rate_percent"

for target in "${TARGET_LIST[@]}"; do
    log_file="$SCRIPT_DIR/build-$target.log"

    target_optimization_count="$(count_target_optimization "$target")"
    all_basic_blocks="$(extract_log_value "$log_file" "All Basic Blocks")"
    reachable_basic_blocks="$(extract_log_value "$log_file" "Average Total Reachable Blocks")"
    pruning_rate="$(extract_log_value "$log_file" "Total Pruning Rate")"

    printf '%s,%s,%s,%s,%s\n' \
        "$target" \
        "$target_optimization_count" \
        "$all_basic_blocks" \
        "$reachable_basic_blocks" \
        "$pruning_rate"
done

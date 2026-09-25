#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -z "${ARTIFACT_ROOT:-}" ]]; then
    # shellcheck source=../env.sh
    . "$SCRIPT_DIR/../env.sh"
fi

ABLATION_DIR="$ARTIFACT_ROOT/ablation"
BUILD_COMMON="$ABLATION_DIR/build_common.sh"
REPO_ROOT="$(cd "$ARTIFACT_ROOT/../.." && pwd)"
ABLATION_RESULT_SCRIPT_DIR="$REPO_ROOT/src/result-script/ablation"

FUZZER_LIST=(
    "dissafe"
    "base"
    "opt"
    "prun"
    "sync"
)

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

TOTAL_BUILDS=$((${#FUZZER_LIST[@]} * ${#TARGET_LIST[@]}))
CURRENT_BUILD=0

declare -a SUCCEEDED=()
declare -a SKIPPED=()
declare -a FAILED=()

function prepare_fuzzer_result_scripts {
    local fuzzer=$1
    local fuzzer_dir="$ABLATION_DIR/$fuzzer"
    local ablation_script="$ABLATION_RESULT_SCRIPT_DIR/$fuzzer-ablation.sh"

    mkdir -p "$fuzzer_dir"
    if [[ ! -f "$ablation_script" ]]; then
        echo "Missing ablation result script: $ablation_script" >&2
        return 1
    fi

    cp "$ablation_script" "$fuzzer_dir/"
    chmod +x "$fuzzer_dir/$(basename "$ablation_script")"
}

function expected_binary {
    local fuzzer=$1

    case "$fuzzer" in
        "dissafe")
            printf "%s\n" "rustgo-dissafe-fuzz/rustgo-dissafe"
            ;;
        "base")
            printf "%s\n" "rustgo-base-fuzz/rustgo-base"
            ;;
        "opt")
            printf "%s\n" "rustgo-opt-fuzz/rustgo-opt"
            ;;
        "prun")
            printf "%s\n" "rustgo-prun-fuzz/rustgo-prun"
            ;;
        "sync")
            printf "%s\n" "rustgo-sync-fuzz/rustgo-sync"
            ;;
        *)
            return 1
            ;;
    esac
}

function build_is_complete {
    local fuzzer=$1
    local target=$2
    local src_dir="$ABLATION_DIR/$fuzzer/$target-src"
    local binary_suffix

    [[ -d "$src_dir" ]] || return 1

    binary_suffix="$(expected_binary "$fuzzer")"
    find "$src_dir" -path "*/$binary_suffix" -type f -executable -print -quit | grep -q .

}

function do_fuzz {
    local fuzzer=$1
    local target=$2
    local fuzzer_dir="$ABLATION_DIR/$fuzzer"
    local log_file="$fuzzer_dir/build-$target.log"

    mkdir -p "$fuzzer_dir"

    CURRENT_BUILD=$((CURRENT_BUILD + 1))
    printf '[%03d/%03d] Building %s/%s... ' "$CURRENT_BUILD" "$TOTAL_BUILDS" "$fuzzer" "$target"

    if build_is_complete "$fuzzer" "$target"; then
        echo "SKIPPED"
        SKIPPED+=("$fuzzer/$target")
        return 0
    fi

    if (cd "$fuzzer_dir" && bash "$BUILD_COMMON" "$target" "$fuzzer") >"$log_file" 2>&1; then
        echo "SUCCESS"
        SUCCEEDED+=("$fuzzer/$target")
    else
        echo "FAILED (log: $log_file)"
        FAILED+=("$fuzzer/$target")
        return 1
    fi
}

function fuzz_all_targets {
    local fuzzer=$1
    local failures=0
    
    for target in "${TARGET_LIST[@]}"; do
        if ! do_fuzz "$fuzzer" "$target"; then
            failures=$((failures + 1))
        fi
    done

    return "$failures"
}

function fuzz_all_fuzzers {
    local failures=0

    for fuzzer in "${FUZZER_LIST[@]}"; do
        prepare_fuzzer_result_scripts "$fuzzer" || return 1
        if ! fuzz_all_targets "$fuzzer"; then
            failures=$((failures + 1))
        fi
    done

    return "$failures"
}

function fuzz_all {
    fuzz_all_fuzzers
}

fuzz_all

echo
echo "Build summary"
echo "  Success: ${#SUCCEEDED[@]} / $TOTAL_BUILDS"
echo "  Skipped: ${#SKIPPED[@]} / $TOTAL_BUILDS"
echo "  Failed:  ${#FAILED[@]} / $TOTAL_BUILDS"

if ((${#FAILED[@]} > 0)); then
    echo "  Failed builds: ${FAILED[*]}"
    exit 1
fi

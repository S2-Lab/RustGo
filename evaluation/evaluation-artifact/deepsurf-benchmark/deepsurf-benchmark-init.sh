#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARTIFACT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$ARTIFACT_ROOT/../.." && pwd)"
ENV_DIR="$REPO_ROOT/env"
DEEPSURF_DIR="$SCRIPT_DIR/deepSURF"
DEEPSURF_REPOSITORY="${DEEPSURF_REPOSITORY:-https://github.com/purseclab/deepSURF}"
DEEPSURF_COMMIT="${DEEPSURF_COMMIT:-69e93a5b3f454194106b868856d0f3f5361b406d}"
PERFORMANCE_DIR="$SCRIPT_DIR/performance"
ABLATION_DIR="$SCRIPT_DIR/ablation"
DEEPSURF_REBUILD="${DEEPSURF_REBUILD:-0}"
PERFORMANCE_FUZZERS=(
    "afl"
    "aflpp"
    "aflgo"
    "aflrun"
    "windranger"
    "fishfuzz"
    "rustgo"
    "lyso"
    "panickiller"
)
ABLATION_FUZZERS=(
    "dissafe"
    "base"
    "opt"
    "prun"
    "sync"
)
BUILD_FAILURES=()

export ARTIFACT_ROOT REPO_ROOT
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:detect_leaks=0:symbolize=1}"

if [[ -f "$ENV_DIR/env.sh" ]]; then
    previous_dir="$PWD"
    cd "$ENV_DIR"
    set +u
    # shellcheck disable=SC1091
    source ./env.sh
    set -u
    cd "$previous_dir"
fi

if [[ ! -e /evaluation && -d "$REPO_ROOT/evaluation" ]]; then
    ln -s "$REPO_ROOT/evaluation" /evaluation
fi

require_path() {
    if [[ ! -e "$1" ]]; then
        echo "[-] Missing required path: $1" >&2
        exit 1
    fi
}

prepare_deepsurf_repository() {
    if [[ ! -e "$DEEPSURF_DIR" ]]; then
        echo "[+] Cloning deepSURF from $DEEPSURF_REPOSITORY"
        git clone "$DEEPSURF_REPOSITORY" "$DEEPSURF_DIR"
    elif [[ ! -d "$DEEPSURF_DIR/.git" ]]; then
        echo "[-] Existing deepSURF path is not a Git repository: $DEEPSURF_DIR" >&2
        exit 1
    else
        echo "[+] Using existing deepSURF repository: $DEEPSURF_DIR"
    fi

    if ! git -C "$DEEPSURF_DIR" cat-file -e "$DEEPSURF_COMMIT^{commit}" 2>/dev/null; then
        echo "[+] Fetching deepSURF commit $DEEPSURF_COMMIT"
        git -C "$DEEPSURF_DIR" fetch origin "$DEEPSURF_COMMIT"
    fi

    git -C "$DEEPSURF_DIR" checkout --detach "$DEEPSURF_COMMIT"
    git -C "$DEEPSURF_DIR" submodule update --init --recursive
}

validate_variant_layout() {
    local root="$1"
    local variant="$2"
    local kind="$3"
    local count
    local bug_num
    local matches
    local harness_dir
    local build_script="build-$variant.sh"
    local fuzz_script

    require_path "$root/$variant"
    count="$(find "$root/$variant" -mindepth 1 -maxdepth 1 -type d -name 'bug#*' | wc -l)"
    if [[ "$count" -ne 26 ]]; then
        echo "[-] Expected 26 bugs under $root/$variant, found $count" >&2
        exit 1
    fi

    for bug_num in $(seq 1 26); do
        mapfile -t matches < <(find "$root/$variant" -mindepth 1 -maxdepth 1 \
            -type d -name "bug#${bug_num}-*" | sort)
        if [[ ${#matches[@]} -ne 1 || ! -d "${matches[0]}/harness" ]]; then
            echo "[-] Expected one harness for $variant/bug#$bug_num" >&2
            exit 1
        fi
        harness_dir="${matches[0]}/harness"
        require_path "$harness_dir/Cargo.toml"
        require_path "$harness_dir/$build_script"
        require_path "$harness_dir/in"
        if [[ -z "$(find "$harness_dir/in" -mindepth 1 -maxdepth 1 \
            -type f -print -quit)" ]]; then
            echo "[-] Seed corpus is empty: $harness_dir/in" >&2
            exit 1
        fi

        fuzz_script="fuzz-$variant.sh"
        require_path "$harness_dir/$fuzz_script"
    done
}

program_name_for_bug() {
    local program="${1#*-}"

    case "$program" in
        slice_deque*) program="slice" ;;
        simple-slab*) program="simple" ;;
        smallvec*) program="smallvec" ;;
    esac
    printf '%s\n' "$program"
}

build_marker_for() {
    local kind="$1"
    local variant="$2"
    local harness_dir="$3"

    if [[ "$kind" == "performance" ]]; then
        case "$variant" in
            rustgo) printf '%s/rustgo-fuzz/rustgo-target\n' "$harness_dir" ;;
            panickiller) printf '%s/panickiller-fuzz/pk-target\n' "$harness_dir" ;;
            *) printf '%s/output\n' "$harness_dir" ;;
        esac
    else
        case "$variant" in
            dissafe) printf '%s/rustgo-dissafe-fuzz/rustgo-dissafe\n' "$harness_dir" ;;
            base) printf '%s/rustgo-base-fuzz/rustgo-base\n' "$harness_dir" ;;
            opt) printf '%s/rustgo-opt-fuzz/rustgo-opt\n' "$harness_dir" ;;
            prun) printf '%s/rustgo-prun-fuzz/rustgo-prun\n' "$harness_dir" ;;
            sync) printf '%s/rustgo-sync-fuzz/rustgo-sync\n' "$harness_dir" ;;
        esac
    fi
}

build_variant() {
    local kind="$1"
    local root="$2"
    local variant="$3"
    local build_script="build-$variant.sh"
    local bug_dir
    local harness_dir
    local marker
    local program
    local log_file
    local stamp_file
    local build_hash
    local recorded_hash

    for bug_dir in "$root/$variant"/bug#*; do
        harness_dir="$bug_dir/harness"
        marker="$(build_marker_for "$kind" "$variant" "$harness_dir")"
        stamp_file="$harness_dir/.deepsurf-build-stamp-$variant"
        build_hash="$(
            {
                find "$harness_dir/src" -type f -print0
                printf '%s\0' "$harness_dir/Cargo.toml" \
                    "$harness_dir/$build_script"
                find "$harness_dir" -maxdepth 1 -type f \
                    -name 'merge-*.py' -print0
            } | sort -z | xargs -0 sha256sum | sha256sum | awk '{print $1}'
        )"
        recorded_hash=""
        if [[ -f "$stamp_file" ]]; then
            recorded_hash="$(<"$stamp_file")"
        fi
        if [[ "$DEEPSURF_REBUILD" != "1" && -x "$marker" \
            && "$recorded_hash" == "$build_hash" ]]; then
            echo "[+] Already built: $variant/$(basename "$bug_dir")"
            continue
        fi

        require_path "$harness_dir/$build_script"
        program="$(program_name_for_bug "$(basename "$bug_dir")")"
        log_file="$harness_dir/setup-build-$variant.log"
        echo "[+] Building: $variant/$(basename "$bug_dir")"
        rm -rf "$harness_dir/target"
        rm -f "$harness_dir"/{output,output.o,output.bc,total.ll,total.bc,total_.ll}
        if ! (cd "$harness_dir" && \
            LOG_FILE="$harness_dir/instrumentation-build-$variant.log" \
            bash "./$build_script" "$program") >"$log_file" 2>&1; then
            echo "[-] Build failed: $variant/$(basename "$bug_dir")" >&2
            echo "[-] Log: $log_file" >&2
            BUILD_FAILURES+=("$kind/$variant/$(basename "$bug_dir")|$log_file")
            continue
        fi
        if [[ ! -x "$marker" ]]; then
            echo "[-] Missing built executable: $marker" >&2
            BUILD_FAILURES+=("$kind/$variant/$(basename "$bug_dir")|$log_file")
            continue
        fi
        printf '%s\n' "$build_hash" >"$stamp_file"
    done
}

prepare_deepsurf_repository

echo "[+] Validating prepared deepSURF benchmark under $SCRIPT_DIR"
require_path "$DEEPSURF_DIR"
require_path "$DEEPSURF_DIR/code/global_data/Cargo.toml"

# LLVM 12 fuzzers use Rust nightly versions from before edition 2021.
sed -i 's/edition = "2021"/edition = "2018"/' \
    "$DEEPSURF_DIR/code/global_data/Cargo.toml"
find "$PERFORMANCE_DIR" "$ABLATION_DIR" -path '*/harness/Cargo.toml' -print0 |
    xargs -0 sed -i 's/edition = "2021"/edition = "2018"/'

for fuzzer in "${PERFORMANCE_FUZZERS[@]}"; do
    validate_variant_layout "$PERFORMANCE_DIR" "$fuzzer" "performance"
done
for fuzzer in "${ABLATION_FUZZERS[@]}"; do
    validate_variant_layout "$ABLATION_DIR" "$fuzzer" "ablation"
done

for fuzzer in "${PERFORMANCE_FUZZERS[@]}"; do
    build_variant "performance" "$PERFORMANCE_DIR" "$fuzzer"
done
for fuzzer in "${ABLATION_FUZZERS[@]}"; do
    build_variant "ablation" "$ABLATION_DIR" "$fuzzer"
done

if [[ ${#BUILD_FAILURES[@]} -gt 0 ]]; then
    echo "[-] ${#BUILD_FAILURES[@]} build(s) failed:" >&2
    for failure in "${BUILD_FAILURES[@]}"; do
        echo "[-] ${failure%%|*}" >&2
        echo "    Log: ${failure#*|}" >&2
    done
    exit 1
fi

echo "[+] deepSURF benchmark build complete"

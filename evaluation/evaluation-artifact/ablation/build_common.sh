#!/usr/bin/env bash
declare -A URLS=(
    ["nom"]="https://github.com/rust-bakery/nom.git"
    ["json"]="https://github.com/serde-rs/json.git"
    ["serde-yaml"]="https://github.com/dtolnay/serde-yaml.git"
    ["unicode-process"]="https://github.com/unicode-rs/unicode-normalization.git"
    ["unicode-streaming"]="https://github.com/unicode-rs/unicode-normalization.git"
    ["data-encoding"]="https://github.com/ia0/data-encoding.git"
    ["chrono"]="https://github.com/chronotope/chrono.git"
    ["mqtt-broker"]="https://github.com/bschwind/mqtt-broker.git"
    ["qcms"]="https://github.com/FirefoxGraphics/qcms.git"
    ["httparse"]="https://github.com/seanmonstar/httparse.git"
    ["quick-xml"]="https://github.com/tafia/quick-xml.git"
    ["lz4_flex"]="https://github.com/PSeitz/lz4_flex.git"
    ["gimli"]="https://github.com/gimli-rs/gimli.git"
    
)
declare -A COMMITS=(
    ["nom"]="db9d5f814c2a724b6b57f4552449a9f659d1676d"
    ["json"]="9e36f611dbc37498a89fcbf43e01b2a9a2312c18"
    ["serde-yaml"]="e5c11139e5c47d55ffb71cbd20aee77c0cc75aaa"
    ["unicode-process"]="1598cfaff2d5e0661e2f506d77c931c01e1f23ea"
    ["unicode-streaming"]="1598cfaff2d5e0661e2f506d77c931c01e1f23ea"
    ["data-encoding"]="5d4ce64fbbd3ef88c72cca908127c3ea9058a704"
    ["chrono"]="b3cea499191257b678454091d2539fce7311db76"
    ["mqtt-broker"]="cf556d769a76bc440e1ba67087e1f29738542620"
    ["qcms"]="10a66470f8fe57bb76ac36614ebe17cc184f25c5"
    ["httparse"]="638848eee31f6eeb604dc3e4358941c8905e65eb"
    ["quick-xml"]="4d06891f44c3a754662958a1e9d3a14f5441ebdf"
    ["lz4_flex"]="b0ebe864087dc7b259d8d9bdd2aad4d683d65bd2"
    ["gimli"]="24a25b9fede9fb4364222100415987105ec053b3"
)
declare -A WORKDIRS=(
    ["nom"]="/"
    ["json"]="/"
    ["serde-yaml"]="/"
    ["unicode-process"]="/"
    ["unicode-streaming"]="/"
    ["data-encoding"]="/lib/fuzz"
    ["chrono"]="/fuzz"
    ["mqtt-broker"]="/fuzz"
    ["qcms"]="/fuzz"
    ["httparse"]="/fuzz"
    ["quick-xml"]="/fuzz"
    ["lz4_flex"]="/fuzz"
    ["gimli"]="/fuzz"
)

declare -A PACKAGES=(
    ["nom"]=""
    ["json"]="ryu:1.0.20"
    ["serde-yaml"]=""
    ["unicode-process"]=""
    ["unicode-streaming"]=""
    ["data-encoding"]=""
    ["chrono"]="libc:0.2.126"
    ["mqtt-broker"]=""
    ["qcms"]=""
    ["httparse"]=""
    ["quick-xml"]="libc:0.2.126"
    ["lz4_flex"]=""
    ["gimli"]=""
)

PROJECTS=( "${!URLS[@]}" )
FUZZER_LIST=(
    "dissafe"
    "base"
    "opt"
    "prun"
    "sync"
)

if [[ $# -ne 2 ]]; then
    echo "[+] Usage: $0 <project> <fuzzer>"
    echo "[+] Available projects: ${PROJECTS[*]}"
    exit 1
fi

if [[ -f "$HOME/.cargo/env" ]]; then
    # shellcheck source=/dev/null
    . "$HOME/.cargo/env"
fi

# ==============================================
# 1. Environment Variables
# ==============================================
if [[ -z "${ARTIFACT_ROOT:-}" ]]; then
    BUILD_COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    if [[ -f "$BUILD_COMMON_DIR/../env.sh" ]]; then
        # shellcheck source=../env.sh
        . "$BUILD_COMMON_DIR/../env.sh"
    fi
fi

if [[ -z "${ARTIFACT_ROOT:-}" ]]; then
    echo "[+] ARTIFACT_ROOT is not set."
    echo "[+] Please run 'source env.sh' in the top directory of this artifact."
    exit 1
fi

REPO_ROOT="$(cd "$ARTIFACT_ROOT/../.." && pwd)"
SCRIPT_DIR="$ARTIFACT_ROOT/scripts"
ABLATION_SCRIPT_DIR="$SCRIPT_DIR/ablation"
PROGRAM_SRC_DIR="$ARTIFACT_ROOT/program-source"

PROJECT=$1
FUZZER=$2
GIT_URL=""
COMMIT=""
PROJECT_WORKDIR=""
FUZZER_SCRIPT_DIR="$ABLATION_SCRIPT_DIR/$FUZZER"
FUZZER_INPUT_DIR="$PROGRAM_SRC_DIR/$PROJECT/in"

# Get the git url and commit hash from the target list
if [[ -n "${URLS[$PROJECT]:-}" ]] && [[ -n "${COMMITS[$PROJECT]:-}" ]] && [[ -n "${WORKDIRS[$PROJECT]:-}" ]]; then
    GIT_URL="${URLS[$PROJECT]}"
    COMMIT="${COMMITS[$PROJECT]}"
    PROJECT_WORKDIR="${WORKDIRS[$PROJECT]}"
else
    echo "[+] Error: Git URL or commit hash not found for project $PROJECT"
    echo "[+] Available projects: ${PROJECTS[@]}"
    echo "[+] Usage: $0 <project> <fuzzer>"
    echo "[+] Given project: $PROJECT"
    exit 1
fi

if [[ ! -d "$FUZZER_SCRIPT_DIR" ]]; then
    echo "[+] Missing fuzzer script directory: $FUZZER_SCRIPT_DIR"
    exit 1
fi

# ==============================================
# 2. Clone nom repository and reset to specific commit
# ==============================================

SRC="$PROGRAM_SRC_DIR/$PROJECT"
DEST="$PWD/$PROJECT-src"
FUZZ_DIR="$DEST$PROJECT_WORKDIR"

if [[ ! -d "$SRC" ]]; then
    echo "[+] Missing program source directory: $SRC"
    exit 1
fi

function find_local_clone {
    local project=$1
    local candidate

    for candidate_fuzzer in "${FUZZER_LIST[@]}"; do
        candidate="$ARTIFACT_ROOT/ablation/$candidate_fuzzer/$project-src"
        if [[ "$candidate" != "$DEST" ]] && [[ -d "$candidate/.git" ]]; then
            printf "%s\n" "$candidate"
            return 0
        fi
    done

    return 1
}

if [[ ! -d "$DEST/.git" ]]; then
    rm -rf "$DEST"
    if local_clone="$(find_local_clone "$PROJECT")"; then
        echo "[+] Copying local $PROJECT repository from $local_clone"
        cp -a "$local_clone" "$DEST"
    else
        echo "[+] Cloning $PROJECT from $GIT_URL"
        git clone "$GIT_URL" "$DEST"
    fi
else
    echo "[+] Using existing $PROJECT repository"
fi

echo "[+] Resetting $PROJECT to $COMMIT"
cd "$DEST"
git reset --hard "$COMMIT"
git clean -fdx
cd - >/dev/null

# ==============================================
# 3. Set the benchmark program
# ==============================================

echo "[+] Copying $PROJECT from $SRC to $DEST"
# Copy all files from the source directory to the destination directory
while IFS= read -r -d '' copy_file; do
    # Get the relative path of the file
    relative_path=$(realpath --relative-to="$SRC" "$copy_file")
    # echo "  - $copy_file -> $DEST/$relative_path"
    mkdir -p "$(dirname "$DEST/$relative_path")"
    cp -f "$copy_file" "$DEST/$relative_path"
done < <(find "$SRC" -type f -print0)

if [[ -n "${PACKAGES[$PROJECT]}" ]]; then
    IFS=',' read -r -a package_pins <<< "${PACKAGES[$PROJECT]}"
    for package_pin in "${package_pins[@]}"; do
        [[ -z "$package_pin" ]] && continue
        package_name="${package_pin%%:*}"
        package_version="${package_pin##*:}"
        echo "[+] Pinning cargo package $package_name to $package_version"
        if ! (cd "$FUZZ_DIR" && cargo update --offline -p "$package_name" --precise "$package_version"); then
            echo "[+] Package $package_name is not in this target's dependency graph; continuing."
        fi
    done
fi


# ==============================================
# 4. Build the program
# ==============================================

mkdir -p "$FUZZ_DIR"
cp "$FUZZER_SCRIPT_DIR/build-$FUZZER.sh" "$FUZZ_DIR"

if [[ -f "$FUZZER_SCRIPT_DIR/merge-$FUZZER.py" ]]; then
    cp "$FUZZER_SCRIPT_DIR/merge-$FUZZER.py" "$FUZZ_DIR"
else
    echo "[+] Missing merge script for fuzzer $FUZZER in $FUZZER_SCRIPT_DIR"
    exit 1
fi

if [[ -f "$FUZZER_SCRIPT_DIR/eval-$FUZZER.sh" ]]; then
    cp "$FUZZER_SCRIPT_DIR/eval-$FUZZER.sh" "$FUZZ_DIR"
fi
if [[ -f "$FUZZER_SCRIPT_DIR/fuzz-$FUZZER.sh" ]]; then
    cp "$FUZZER_SCRIPT_DIR/fuzz-$FUZZER.sh" "$FUZZ_DIR"
fi
if [[ -f "$FUZZER_SCRIPT_DIR/aggregate-target.py" ]]; then
    cp "$FUZZER_SCRIPT_DIR/aggregate-target.py" "$FUZZ_DIR"
fi
if [[ -d "$FUZZER_INPUT_DIR" ]]; then
    cp -r "$FUZZER_INPUT_DIR" "$FUZZ_DIR"
elif [[ -d "$FUZZER_SCRIPT_DIR/in" ]]; then
    cp -r "$FUZZER_SCRIPT_DIR/in" "$FUZZ_DIR"
else
    echo "[+] Missing input corpus for $PROJECT/$FUZZER"
    exit 1
fi

cd "$FUZZ_DIR"

echo "[+] cargo clean"
cargo clean

bash "build-$FUZZER.sh" "$(awk -F '-' '{print $1}' <<< "$PROJECT")" 2>&1 | tee build.log
build_status=${PIPESTATUS[0]}
if [ "$build_status" -ne 0 ]; then
    echo "[+] Build script failed with status $build_status"
    exit "$build_status"
fi

awk '
    /^#######################################################*$/ {
        output = ""
        next
    }
    {
        output = output $0 ORS
    }
    END {
        printf "%s", output
    }
' build.log > static_analysis_result.txt

# python3 merge-$FUZZER.py fuzz_target

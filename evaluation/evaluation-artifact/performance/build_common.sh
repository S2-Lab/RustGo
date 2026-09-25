set -Eeo pipefail
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
    ["nom"]="arbitrary:1.0.3,jobserver:0.1.26"
    ["json"]="arbitrary:1.0.3,jobserver:0.1.26,ryu:1.0.20,serde:1.0.137,trybuild:1.0.49"
    ["serde-yaml"]="indexmap:1.6.0,serde:1.0.137,serde_derive:1.0.137"
    ["unicode-process"]="serde:1.0.137,serde_derive:1.0.137,tinyvec:1.5.1"
    ["unicode-streaming"]="serde:1.0.137,serde_derive:1.0.137,tinyvec:1.5.1"
    ["data-encoding"]=""
    ["chrono"]="libc:0.2.126,num-traits:0.2.18,serde:1.0.99"
    ["mqtt-broker"]=""
    ["qcms"]=""
    ["httparse"]=""
    ["quick-xml"]="libc:0.2.126,memchr:2.5.0,serde:1.0.99"
    ["lz4_flex"]="arbitrary:1.0.3,jobserver:0.1.26,serde:1.0.99"
    ["gimli"]="indexmap:1.6.0,serde:1.0.99,stable_deref_trait:1.2.0"
)

declare -A MIDPACKAGES=(
    ["json"]="ryu:1.0.20"
    ["chrono"]="libc:0.2.126"
    ["quick-xml"]="libc:0.2.126"
)

PROJECTS=( "${!URLS[@]}" )

MID_COMPILERS=(
    "rustgo"
    "aflpp"
    "aflrun"
    "panickiller"
)

OLD_COMPILERS=(
    "afl"
    "aflgo"
    "windranger"
    "fishfuzz"
    "lyso"
)

cd "$HOME"
. "$HOME/.cargo/env"
cd - >/dev/null

# ==============================================
# 1. Environment Variables
# ==============================================
if [ -z "${ARTIFACT_ROOT:-}" ]; then
    BUILD_COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    if [ -f "$BUILD_COMMON_DIR/../env.sh" ]; then
        # shellcheck source=../env.sh
        . "$BUILD_COMMON_DIR/../env.sh"
    fi
fi

if [ -z "${ARTIFACT_ROOT:-}" ]; then
    echo "[+] ARTIFACT_ROOT is not set."
    echo "[+] Please run 'source env.sh' in the top directory of this artifact."
    exit 1
fi

SCRIPT_DIR="$ARTIFACT_ROOT/scripts"
PERFORMANCE_SCRIPT_DIR="$SCRIPT_DIR/performance"
PROGRAM_SRC_DIR="$ARTIFACT_ROOT/program-source"

PROJECT=$1 # "nom"
FUZZER=$2  # "afl"
GIT_URL=""
COMMIT=""
PROJECT_WORKDIR=""
FUZZER_SCRIPT_DIR="$PERFORMANCE_SCRIPT_DIR/$FUZZER"
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

# ==============================================
# 2. Clone nom repository and reset to specific commit
# ==============================================

SRC="$PROGRAM_SRC_DIR/$PROJECT"
DEST="$PWD/$PROJECT-src"
FUZZ_DIR="$DEST$PROJECT_WORKDIR"

if [ ! -d "$DEST" ]; then
    echo "[+] Cloning $PROJECT from $GIT_URL"
    git clone "$GIT_URL" "$DEST"
else
    echo "[+] Using existing $PROJECT repository"
fi

echo "[+] Resetting $PROJECT to $COMMIT"
cd "$DEST"
git reset --hard "$COMMIT"
cd - >/dev/null

# ==============================================
# 3. Set the benchmark program
# ==============================================

# Copy all files from the source directory to the destination directory
while IFS= read -r -d '' copy_file; do
    # Get the relative path of the file
    relative_path=$(realpath --relative-to="$SRC" "$copy_file")
    # echo "  - $copy_file -> $DEST/$relative_path"
    mkdir -p "$(dirname "$DEST/$relative_path")"
    cp -f "$copy_file" "$DEST/$relative_path"
done < <(find "$SRC" -type f -print0)

# Stage the fuzzer scripts before dependency updates. Dependency resolution can
# fail for an individual target, but the prepared source tree should still
# contain the scripts needed to diagnose or resume that build.
mkdir -p "$FUZZ_DIR"
cp "$FUZZER_SCRIPT_DIR/build-$FUZZER.sh" "$FUZZ_DIR"
cp "$FUZZER_SCRIPT_DIR/merge-$FUZZER.py" "$FUZZ_DIR"
if [ -f "$FUZZER_SCRIPT_DIR/eval-$FUZZER.sh" ]; then
    cp "$FUZZER_SCRIPT_DIR/eval-$FUZZER.sh" "$FUZZ_DIR"
fi
if [ -f "$FUZZER_SCRIPT_DIR/fuzz-$FUZZER.sh" ]; then
    cp "$FUZZER_SCRIPT_DIR/fuzz-$FUZZER.sh" "$FUZZ_DIR"
fi

if [[ "$FUZZER" == "lyso" ]]; then
    cp "$FUZZER_SCRIPT_DIR/target.txt" "$FUZZ_DIR"
fi

temp_dir="$FUZZER_SCRIPT_DIR/temp"
if [ -d "$temp_dir" ]; then
    cp -r "$temp_dir" "$FUZZ_DIR"
fi

cp -r "$FUZZER_INPUT_DIR" "$FUZZ_DIR"

if [ "$PROJECT" = "data-encoding" ]; then
    BUILD_PROGRAM_NAME="fuzz_target"
else
    BUILD_PROGRAM_NAME="$(awk -F '-' '{print $1}' <<< "$PROJECT")"
fi

update_package_if_present() {
    local package=$1
    local version=$2
    local update_log

    update_log="$(mktemp)"
    echo "[+] Updating $package to $version"
    echo "[+] cargo update -p $package --precise $version"
    if cargo update -p "$package" --precise "$version" >"$update_log" 2>&1; then
        cat "$update_log"
        rm -f "$update_log"
        return 0
    fi

    if grep -q "did not match any packages" "$update_log"; then
        cat "$update_log"
        echo "[+] Skipping $package: package is not present in this target"
        rm -f "$update_log"
        return 0
    fi

    cat "$update_log"
    rm -f "$update_log"
    return 1
}

# ==============================================
# 4. Set the packages for the program
# ==============================================
if [[ " ${MID_COMPILERS[@]} " =~ " $FUZZER " ]]; then
    echo "[+] Old compiler detected"
    echo "[+] Copying $PROJECT from $SRC to $DEST"
    cd "$FUZZ_DIR"

    echo "[+] PWD: $PWD"

    echo "[+] cargo clean"
    cargo clean
    for package_str in $(echo "${MIDPACKAGES[$PROJECT]}" | tr ',' '\n'); do
        [[ -z "$package_str" ]] && continue
        package=$(echo $package_str | cut -d':' -f1)
        version=$(echo $package_str | cut -d':' -f2)
        update_package_if_present "$package" "$version"
    done
fi

if [[ " ${OLD_COMPILERS[@]} " =~ " $FUZZER " ]]; then
    echo "[+] Old compiler detected"
    echo "[+] Copying $PROJECT from $SRC to $DEST"
    cd "$FUZZ_DIR"

    echo "[+] PWD: $PWD"

    echo "[+] cargo clean"
    cargo clean
    for package_str in $(echo "${PACKAGES[$PROJECT]}" | tr ',' '\n'); do
        [[ -z "$package_str" ]] && continue
        package=$(echo $package_str | cut -d':' -f1)
        version=$(echo $package_str | cut -d':' -f2)
        update_package_if_present "$package" "$version"
    done
fi

# ==============================================
# 5. Build the program
# ==============================================

cd "$FUZZ_DIR"


if [ "$PROJECT" == "serde-yaml" ]; then
    # serde-yaml is a special case; keep indexmap pinned for old compiler builds.
    if [[ " ${OLD_COMPILERS[@]} " =~ " $FUZZER " ]]; then
        echo "[+] Old compiler detected"
        cargo clean
        update_package_if_present indexmap 1.6.0
    fi
    bash build-$FUZZER.sh "$BUILD_PROGRAM_NAME"
else
    bash build-$FUZZER.sh "$BUILD_PROGRAM_NAME"

fi

# python3 merge-$FUZZER.py fuzz_target

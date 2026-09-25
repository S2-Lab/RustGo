#!/usr/bin/env bash
set -Eeuo pipefail

trap 'echo "[ERROR] Environment setup failed at line $LINENO." >&2' ERR

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
readonly ENV_FILE="$REPO_ROOT/env/env.sh"
readonly TARGET="x86_64-unknown-linux-gnu"

readonly -a TOOLCHAINS=(
    "nightly-2021-02-11"
    "nightly-2021-07-26"
    "1.69.0"
)

install_system_dependencies() {
    if ! command -v apt-get >/dev/null 2>&1; then
        echo "[ERROR] This setup script requires apt-get (Debian/Ubuntu)." >&2
        exit 1
    fi

    local -a privilege=()
    if (( EUID != 0 )); then
        if ! command -v sudo >/dev/null 2>&1; then
            echo "[ERROR] Run this script as root or install sudo." >&2
            exit 1
        fi
        privilege=(sudo)
    fi

    echo "[*] Installing system dependencies..."
    "${privilege[@]}" apt-get update
    "${privilege[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential \
        ca-certificates \
        clang \
        cmake \
        curl \
        git \
        libbz2-dev \
        liblz4-dev \
        liblzma-dev \
        libncurses-dev \
        libssl-dev \
        libzstd-dev \
        ninja-build \
        pkg-config \
        python3 \
        python3-networkx \
        python3-pydot \
        unzip \
        xz-utils \
        zlib1g-dev
}

install_rustup() {
    if [[ -f "$HOME/.cargo/env" ]]; then
        # shellcheck disable=SC1091
        source "$HOME/.cargo/env"
    else
        export PATH="$HOME/.cargo/bin:$PATH"
    fi

    if command -v rustup >/dev/null 2>&1; then
        echo "[+] rustup is already installed."
        return
    fi

    echo "[*] Installing rustup..."
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs |
        sh -s -- -y --profile minimal --no-modify-path

    # shellcheck disable=SC1091
    source "$HOME/.cargo/env"
}

install_rust_toolchains() {
    local toolchain rustc_path library_dir

    for toolchain in "${TOOLCHAINS[@]}"; do
        echo "[*] Installing Rust toolchain: $toolchain"
        rustup toolchain install "$toolchain" \
            --profile minimal \
            --target "$TARGET"

        rustc_path="$(rustup which --toolchain "$toolchain" rustc)"
        library_dir="$(
            rustup run "$toolchain" rustc \
                --print target-libdir \
                --target "$TARGET"
        )"

        if [[ ! -x "$rustc_path" ]]; then
            echo "[ERROR] rustc not found: $rustc_path" >&2
            exit 1
        fi

        if [[ ! -d "$library_dir" ]]; then
            echo "[ERROR] Rust runtime library not found: $library_dir" >&2
            exit 1
        fi

        echo "[+] $("$rustc_path" --version)"
        echo "[+] Runtime libraries: $library_dir"
    done
}

load_repository_environment() {
    if [[ ! -f "$ENV_FILE" ]]; then
        echo "[ERROR] Repository environment file not found: $ENV_FILE" >&2
        exit 1
    fi

    # This prepares the environment for checks performed by this process.
    # Users must source env/env.sh again in each new shell.
    # shellcheck disable=SC1090
    source "$ENV_FILE"
}

configure_git_safety() {
    git config --global --add safe.directory '*'
}

main() {
    echo "[*] Repository root: $REPO_ROOT"
    install_system_dependencies
    configure_git_safety
    install_rustup
    install_rust_toolchains
    load_repository_environment

    echo
    echo "[+] Environment initialization completed."
    rustup toolchain list
    echo
    echo "Before building in a new shell, run:"
    printf '  source %q\n' "$ENV_FILE"
}

main "$@"

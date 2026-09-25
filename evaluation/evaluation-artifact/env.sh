export ARTIFACT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export DGF_ROOT="$ARTIFACT_ROOT/../dgfs-porting"
export REPO_ROOT="$ARTIFACT_ROOT/../.."

echo "[+] ARTIFACT_ROOT: $ARTIFACT_ROOT"
echo "[+] DGF_ROOT: $DGF_ROOT"
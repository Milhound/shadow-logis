#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
format="${1:-}"
case "$format" in
    DEB) base_image=debian:13-slim; label=debian13;;
    RPM) base_image=fedora:44; label=fedora44;;
    *) echo 'Usage: bash tools/build-docker.sh DEB|RPM' >&2; exit 2;;
esac
for tool in docker python3; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
docker buildx version >/dev/null || { echo 'Docker Buildx is required for artifact export.' >&2; exit 1; }
docker info >/dev/null || { echo 'Docker daemon is unavailable to this terminal. No build was started.' >&2; exit 1; }
python3 "$project_dir/tools/check-publication.py"
python3 "$project_dir/tools/package-source.py"
mkdir -p "$project_dir/package"
context="$(mktemp -d "$project_dir/package/.docker-context.XXXXXX")"
output="$(mktemp -d "$project_dir/package/linux-docker-$label.XXXXXX")"
cp "$project_dir/native/linux/Dockerfile.packages" "$context/Dockerfile"
cp "$project_dir/dist/shadow-logis-source.zip" "$context/"
# No repository bind mount or repository-wide build context: private data stays out.
docker buildx build --platform linux/amd64 --progress plain --target artifacts \
    --build-arg "BASE_IMAGE=$base_image" --build-arg "PACKAGE_FORMAT=$format" \
    --build-arg "JOBS=${CMAKE_BUILD_PARALLEL_LEVEL:-4}" \
    --output "type=local,dest=$output" "$context"
python3 - "$output" "$format" <<'PY'
from pathlib import Path
import hashlib,sys
root=Path(sys.argv[1]); extension='.deb' if sys.argv[2]=='DEB' else '.rpm'
packages=list(root.glob('*'+extension))
if len(packages)!=1:
    raise SystemExit('Expected exactly one exported '+extension+' package')
expected={packages[0].name,'shadow-logis-source.zip','SHA256SUMS'}
if {p.name for p in root.iterdir()}!=expected:
    raise SystemExit('Unexpected exported files')
seen=set()
for line in (root/'SHA256SUMS').read_text().splitlines():
    digest,name=line.split('  ',1)
    if name not in expected-{'SHA256SUMS'} or name in seen:
        raise SystemExit('Unexpected checksum entry')
    seen.add(name)
    if hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest:
        raise SystemExit('Checksum mismatch: '+name)
if seen!=expected-{'SHA256SUMS'}:
    raise SystemExit('Missing release checksums')
print('PASS: exact Docker release files and checksums.')
PY
echo "Docker package and matching source: $output"

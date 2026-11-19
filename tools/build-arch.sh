#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || { echo 'Build this package on x86_64 Arch/CachyOS.' >&2; exit 1; }
[[ "$EUID" != 0 ]] || { echo 'Run as an ordinary user; makepkg must not run as root.' >&2; exit 1; }
for tool in makepkg pacman cmake cpack ctest ninja python3 bsdtar; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
pacman -T base-devel cmake ninja python poppler fontconfig qt6-base qt6-webengine qt6-wayland ttf-liberation || {
    echo 'Install the missing build dependencies listed above, then retry.' >&2; exit 1;
}
python3 "$project_dir/tools/check-publication.py"
python3 "$project_dir/tools/package-source.py"
mkdir -p "$project_dir/package"
work="$(mktemp -d "$project_dir/package/.arch-build.XXXXXX")"
output="$(mktemp -d "$project_dir/package/linux-arch.XXXXXX")"
cp "$project_dir/dist/shadow-logis-source.zip" "$work/"
python3 - "$project_dir" "$work" <<'PY'
from pathlib import Path
import hashlib, re, sys
root,work=map(Path,sys.argv[1:])
version=re.search(r'project\(ShadowLogis VERSION (\d+\.\d+\.\d+)',(root/'CMakeLists.txt').read_text()).group(1)
digest=hashlib.sha256((work/'shadow-logis-source.zip').read_bytes()).hexdigest()
recipe=(root/'native/linux/PKGBUILD.in').read_text().replace('@VERSION@',version).replace('@SOURCE_SHA256@',digest)
(work/'PKGBUILD').write_text(recipe)
PY
(
    cd "$work"
    # Explicit destinations prevent user makepkg settings writing outside this build.
    PKGDEST="$output" SRCDEST="$work" SRCPKGDEST="$output" LOGDEST="$work" BUILDDIR="$work" \
        PKGEXT='.pkg.tar.zst' CARCH=x86_64 PACKAGECARCH=x86_64 PACKAGER='Shadow Logis' \
        makepkg --config "$project_dir/native/linux/makepkg.conf" --check --nosign --noconfirm
)
# Verify the final compressed package, including makepkg's stripping/metadata.
python3 "$project_dir/tools/verify_arch_package.py" "$output"/*.pkg.tar.zst
# Keep the standalone tarball as an alternative, from the same tested build.
cpack --config "$work/src/build/CPackConfig.cmake" -G TGZ -B "$output"
cp "$work/PKGBUILD" "$work/shadow-logis-source.zip" "$output/"
python3 - "$output" <<'PY'
from pathlib import Path
import hashlib,sys
root=Path(sys.argv[1])
files=sorted(p for p in root.iterdir() if p.is_file())
(root/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in files))
PY
echo "Verified Arch package, tarball and matching source: $output"

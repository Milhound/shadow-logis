#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
format="${1:-TGZ}"
case "$format" in
    ARCH) exec bash "$project_dir/tools/build-arch.sh";;
    DEB|RPM|TGZ) ;;
    *) echo 'Usage: bash tools/build-linux.sh DEB|RPM|TGZ|ARCH' >&2; exit 2;;
esac
[[ "$(uname -s)" == Linux ]] || { echo 'Build Linux packages on Linux.' >&2; exit 1; }
for tool in cmake ctest cpack ninja python3 pdftotext pdftoppm; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
# CPack inspects ELF files when generating native package dependencies.
# Minimal build images do not necessarily include the file utility.
if [[ "$format" == DEB || "$format" == RPM ]]; then
    command -v file >/dev/null || { echo 'Missing tool: file (install the file package before building DEB/RPM packages).' >&2; exit 1; }
fi
case "$format" in
    DEB) command -v dpkg-shlibdeps >/dev/null || { echo 'DEB builds require Debian dpkg-dev tools.' >&2; exit 1; };;
    RPM) command -v rpmbuild >/dev/null || { echo 'RPM builds require rpm-build.' >&2; exit 1; };;
esac
. /etc/os-release
build="$project_dir/build-linux-${format,,}"
cmake -S "$project_dir" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_TESTING=ON \
    "-DSHADOW_LOGIS_DISTRO_ID=$ID" "-DSHADOW_LOGIS_PACKAGE_CONTACT=${SHADOW_LOGIS_PACKAGE_CONTACT:-Milhound}"
cmake --build "$build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
ctest --test-dir "$build" --output-on-failure --parallel 2
mkdir -p "$project_dir/package"
stage="$(mktemp -d "$project_dir/package/.linux-stage.XXXXXX")"
DESTDIR="$stage" cmake --install "$build" --prefix /usr
python3 "$project_dir/tools/verify_linux_package.py" "$stage/usr"
# Stage is verified before CPack creates packages from the same install rules.
output="$(mktemp -d "$project_dir/package/linux-${format,,}-$ID.XXXXXX")"
cpack --config "$build/CPackConfig.cmake" -G "$format" -B "$output"
if [[ "$format" == TGZ ]]; then
    python3 - "$project_dir" "$output" <<'PY'
from pathlib import Path
import sys, tarfile, tempfile
sys.path.insert(0,str(Path(sys.argv[1])/'tools'))
from verify_linux_package import verify
archives=list(Path(sys.argv[2]).glob('*.tar.gz'))
if len(archives)!=1:
    raise SystemExit('Expected one Linux tarball')
with tempfile.TemporaryDirectory(prefix='shadow-logis-tarball-') as folder:
    with tarfile.open(archives[0]) as archive:
        archive.extractall(folder,filter='data')
    roots=list(Path(folder).iterdir())
    if len(roots)!=1 or not roots[0].is_dir():
        raise SystemExit('Unexpected tarball directory layout')
    verify(roots[0])
PY
fi
python3 "$project_dir/tools/package-source.py"
cp "$project_dir/dist/shadow-logis-source.zip" "$output/"
python3 - "$output" <<'PY'
from pathlib import Path
import hashlib,sys
root=Path(sys.argv[1])
files=sorted(p for p in root.iterdir() if p.is_file() and p.suffix in {'.deb','.rpm','.gz','.zip'})
(root/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in files))
PY
echo "Verified Linux package and corresponding source: $output"

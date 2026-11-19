#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$(uname -s)" != Darwin ]]; then
    echo 'Run this script natively on an Apple Silicon Mac with an Xcode SDK, Qt 6, CMake, Ninja, Python 3 and Poppler.' >&2
    exit 1
fi
if [[ "$(uname -m)" != arm64 ]]; then
    echo 'Open a native Apple Silicon terminal (not Rosetta) to build the ARM64 app.' >&2
    exit 1
fi
for tool in cmake ctest ninja python3 pdftotext pdftoppm xcrun; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing build/check tool: $tool. See README.md for Mac dependencies." >&2
        exit 1
    fi
done
if [[ -z "${CMAKE_PREFIX_PATH:-}" ]]; then
    if command -v brew >/dev/null 2>&1; then
        qt_base="$(brew --prefix qtbase)"
        qt_pdf="$(brew --prefix qtwebengine)"
        if [[ ! -d "$qt_base/lib/cmake/Qt6" || ! -d "$qt_pdf/lib/cmake/Qt6Pdf" ]]; then
            echo 'Install matching Qt modules: brew install qtbase qtwebengine qttools' >&2
            exit 1
        fi
        qt_prefix="$qt_base;$qt_pdf;$(brew --prefix)"
    else
        echo 'Set CMAKE_PREFIX_PATH to your Qt 6 macOS installation.' >&2
        exit 1
    fi
else
    qt_prefix="$CMAKE_PREFIX_PATH"
fi
sdk="$(xcrun --sdk macosx --show-sdk-path)"
compiler="$(xcrun --sdk macosx --find clang++)"
# Default to this build machine's OS instead of promising compatibility below
# the installed Qt/Homebrew libraries. An older supported target can be explicit.
deployment_target="${MACOSX_DEPLOYMENT_TARGET:-$(sw_vers -productVersion | cut -d. -f1,2)}"
cmake -S "$project_dir" -B "$project_dir/build-macos" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_CXX_COMPILER="$compiler" -DCMAKE_OSX_SYSROOT="$sdk" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" -DCMAKE_PREFIX_PATH="$qt_prefix" \
    -DBUILD_TESTING=ON
cmake --build "$project_dir/build-macos" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
ctest --test-dir "$project_dir/build-macos" --output-on-failure
# Never install over an old app: stale plugins/data must not enter the bundle.
# Keep any previous successful package until the replacement passes all checks.
mkdir -p "$project_dir/package"
stage="$(mktemp -d "$project_dir/package/.macos-stage.XXXXXX")"
cmake --install "$project_dir/build-macos" --prefix "$stage"
# Local integrity signature, with no signing identity/account required. Older
# supported Qt deployment tools do not consistently perform this step themselves.
/usr/bin/codesign --force --deep --sign - "$stage/shadow-logis.app"
python3 "$project_dir/tools/verify_macos_bundle.py" "$stage/shadow-logis.app"
if [[ -e "$project_dir/package/macos" ]]; then
    previous="$(mktemp -d "$project_dir/package/macos-previous.XXXXXX")"
    mv "$project_dir/package/macos" "$previous/package"
    echo "Previous package preserved in $previous/package"
fi
mv "$stage" "$project_dir/package/macos"
echo "Built $project_dir/package/macos/shadow-logis.app"
echo "Deployment target: macOS $deployment_target (installed dependencies may require newer)."
echo 'Open the app, then create/open a workspace or import a legacy TPS installation.'
echo 'Local development build; Developer ID signing and notarization are not configured.'

# Shadow Logis

Shadow Logis is a native desktop application for managing freight bookings,
shipments and shipping documents. Keep shipment details together, reuse customer
and routing information across documents, and preview paperwork before printing
or saving a PDF.

Built with C++20 and Qt 6, Shadow Logis stores workspaces locally in SQLite and
targets Linux, Apple Silicon macOS and Windows x64. It runs without Wine or the
original legacy application.

## Features

- A searchable shipment workspace with linked air waybills, ocean bills of lading,
  dock receipts, certificates of origin and sight drafts.
- An address book and company profile with reusable contact details and a logo
  for dock receipts.
- Shared shipment details that prefill new documents, with explicit choices when
  updating existing documents.
- Weight and volume conversions, volumetric weight calculations and AWB charges,
  with manual overrides.
- PDF preview and printing, including Laser AWB, single-copy Dot Matrix AWB and
  text-only ocean bills of lading for preprinted stock.
- Searchable airline, airport, country, currency and Incoterms references.
- New workspaces, selective imports from a legacy TPS installation, database
  backups, edit history, recoverable deletion and remembered user preferences.

This is an early test release. Linux builds have been exercised; native macOS,
Windows and physical printer validation are still pending. Review generated forms
and test alignment with your carrier's requirements and paper stock before use.
Workspaces are local and single-user; there is no cloud sync or TPS write-back.

## Install

Use a package built for your operating system and architecture. Packages and
matching source archives are produced by the build commands below; these
instructions do not imply that published binaries are already available.

### Linux

Install the matching package with your distribution's package manager so its
runtime dependencies are resolved. Substitute the actual downloaded filename:

```bash
# Debian 13
sudo apt install ./shadow-logis-VERSION.deb

# Fedora 44
sudo dnf install ./shadow-logis-VERSION.rpm

# Arch / CachyOS
sudo pacman -U ./shadow-logis-VERSION-1-x86_64.pkg.tar.zst
# Alternatively: yay -U ./shadow-logis-VERSION-1-x86_64.pkg.tar.zst
```

Launch **Shadow Logis** from your application menu or run `shadow-logis`.

For the Arch/CachyOS standalone `.tar.gz`, install `qt6-base`, `qt6-wayland`,
`qt6-webengine` and `ttf-liberation`, then extract the archive and run
`bin/shadow-logis` inside it. A `.tar.gz` is an extract-and-run archive, not a
package for `pacman -U` or `yay -U`. Linux binaries depend on the distribution
and library versions used to build them.

### macOS — Apple Silicon

After building, copy `package/macos/shadow-logis.app` to Applications or launch
it directly. The development bundle is ad-hoc signed, not notarized; it is not
yet a signed public installer. Intel Macs are not currently targeted.

### Windows — x64

Copy the entire `package/windows` folder to the destination machine, then launch
`bin/shadow-logis.exe`. Keep its supporting files together. Install the
[Microsoft Visual C++ x64 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)
if needed. The current output is an unsigned portable folder, not an installer.

## First launch

1. Choose **New workspace**, **Open workspace**, or **Import legacy TPS installation**.
2. For an import, close the legacy application and back up its installation first.
   Point the wizard at that installation, select the data folder if prompted,
   and choose the data categories to import using the checkboxes. Import creates
   a separate SQLite workspace and leaves the originals unchanged.
3. Set your company, contact and logo under **Address book > Company profile**.
4. Add or select a shipment, then use its document tabs—or double-click it and
   use its document controls—to create, edit and preview documents.

Keep workspace files in a private folder outside the application and source
checkout. Use **File > Back up database** for a consistent backup. Review the
migration report after importing; files that cannot be decoded are identified.

## Build from source

Clone the repository or extract its matching source archive:

```bash
git clone https://github.com/Milhound/shadow-logis.git
cd shadow-logis
```

The project requires a C++20 compiler, CMake 3.21+, Ninja and Qt 6.5+ with Widgets,
SQL/SQLite, PDF, PDF Widgets and PrintSupport. Python 3 and Poppler are needed for
packaging and document tests. The shared commands use GNU Make.

### Linux dependencies

Install the prerequisites for your build distribution:

```bash
# Debian 13
sudo apt install build-essential cmake ninja-build python3 poppler-utils \
  fontconfig file dpkg-dev qt6-base-dev qt6-pdf-dev qt6-wayland \
  libqt6sql6-sqlite fonts-liberation

# Fedora 44
sudo dnf install gcc-c++ make cmake ninja-build python3 poppler-utils \
  fontconfig file rpm-build qt6-qtbase-devel qt6-qtpdf-devel qt6-qtwayland \
  liberation-serif-fonts liberation-sans-fonts liberation-mono-fonts

# Arch / CachyOS
sudo pacman -S --needed base-devel cmake ninja python poppler fontconfig \
  qt6-base qt6-wayland qt6-webengine ttf-liberation
```

Then build, test and run:

```bash
make build
make test
make run
```

### macOS build

Use a native Apple Silicon terminal with a compatible Xcode/SDK installed and
selected. With Homebrew installed:

```bash
brew install qtbase qtwebengine qttools cmake ninja python poppler
make macos
open package/macos/shadow-logis.app
```

The script discovers Homebrew Qt. For another Qt installation, set
`CMAKE_PREFIX_PATH` to the matching Qt prefix. The minimum supported macOS version
is determined by the build SDK, deployment target and bundled dependencies.

### Windows build

Install Visual Studio 2022 or Build Tools with Desktop development with C++,
an x64 Windows SDK, and a matching Qt MSVC 2022 x64 kit including Qt PDF/PDF
Widgets and image formats. Also install CMake, Ninja and Python 3; put Poppler's
`pdftotext.exe` and `pdftoppm.exe` on `PATH`.

In an x64 Visual Studio Developer PowerShell, adjust the Qt path as needed:

```powershell
python -m pip install pefile
.\tools\build-windows.ps1 -QtPrefix 'C:\Qt\6.12.0\msvc2022_64'
```

With GNU Make installed, the equivalent is
`make windows QT_PREFIX=C:/Qt/6.12.0/msvc2022_64`.
The desktop executable is `package/windows/bin/shadow-logis.exe`; the console
entry point is `package/windows/bin/shadow-logis-cli.exe`.

### Package builds

| Command | Output |
| --- | --- |
| `make deb` | Debian package, built on Debian |
| `make rpm` | RPM package, built on Fedora |
| `make arch` | Arch package and standalone tarball; run as a regular user |
| `make tarball` | Standalone Linux archive |
| `make macos` | Apple Silicon application bundle |
| `make windows` | Windows x64 deployment folder |
| `make source` | Audited source ZIP in `dist/` |

To build Debian or Fedora packages from another Linux distribution, use Docker
with Buildx and a running daemon accessible to your terminal:

```bash
make docker-deb
make docker-rpm
# Or build both:
make docker-linux
```

Docker builds target Linux x86-64; ARM hosts need amd64 emulation. They send only
the allowlisted source archive to the container and export packages, matching
source and checksums under `package/linux-docker-*`. No host installation occurs.
Native Linux packages are written under `package/linux-*`.

Package builds run tests and verification before producing release files.
Use `make help` for all targets. `JOBS=8` changes build concurrency;
`BUILD_DIR=build-debug BUILD_TYPE=Debug` selects a development build directory
and configuration. `QT_PREFIX` supplies a non-default Qt installation.

## Development and license

See [AGENTS.md](AGENTS.md) for development conventions, implementation details,
migration constraints and publication checks. Dated technical notes live in
[review/REVIEW.txt](review/REVIEW.txt).

Shadow Logis is licensed under [AGPL-3.0-only](LICENSE). Third-party components
retain their own licenses; see [third-party notices](third_party/NOTICE.txt).
Binary distributions must provide corresponding source. User workspaces and
shipping records are not included in the repository or release packages.

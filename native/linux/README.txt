Shadow Logis Linux test packages

Packages are built separately against their target distribution's system Qt.
A Debian package is not a claim of compatibility with every Ubuntu release;
an RPM built on Fedora is not a claim of compatibility with every RPM distro.
The initial build recipes target Debian 13, Fedora 44 and current Arch x86_64.
From a different Linux distribution, make docker-deb or make docker-rpm builds
inside Debian/Fedora using Docker and Buildx. make docker-linux runs both.
Only the allowlisted source is sent; outputs appear under package/linux-docker-*.

Debian: install the matching .deb through apt so dependencies are resolved.
Fedora: install the matching .rpm through dnf so dependencies are resolved.
Both install the executable and desktop launcher under /usr.
Packages also require Liberation Serif, Sans and Mono for shipping documents.
Embedded Inter supplies the UI font, not the document serif/monospace fonts.

Arch/CachyOS installable package: use yay -U or sudo pacman -U with the supplied
shadow-logis-<version>-1-x86_64.pkg.tar.zst file. Pacman manages dependencies,
the desktop launcher, upgrades and removal. The .tar.gz below is a separate
extract-and-run option and cannot be installed with pacman -U.
Build an installable package as a regular user with:
  bash tools/build-linux.sh ARCH
The output includes a checksummed PKGBUILD and its matching source ZIP. To
rebuild that release, put those two files together and run makepkg --check.
Use native/linux/makepkg.conf from the source for generic x86-64 build flags.

Arch/CachyOS tarball: inspect/install the qt6-base, qt6-wayland and
qt6-webengine and ttf-liberation system packages first. Extract the archive into a user-owned
directory and run bin/shadow-logis inside it. No root installation is necessary.
The executable depends on the Qt and C/C++ runtime versions used to build it;
use a current, fully updated Arch system or rebuild from the source package.
The launcher/icon under share may optionally be installed by the user.

First launch: choose New workspace, Open workspace, or Import legacy TPS
installation. Store workspace files in a private user folder outside this
package. An import creates a separate workspace and never edits source files.
The program remembers the last successfully opened workspace.

No operational records, PDFs, passwords or company logos are included.
Source: https://github.com/Milhound/shadow-logis
License: GNU Affero General Public License version 3 only (AGPL-3.0-only).
The corresponding release source must accompany binary release availability.
Third-party fonts and reference data retain their own licenses; see licenses/.

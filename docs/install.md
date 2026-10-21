# Install Omawing

[Home](../README.md) · [Gallery](gallery.md) · [Playing](playing.md) · [Install](install.md) · [Development](development.md)

## Install

Pick whichever you're comfortable trusting. Each installs only files, with no
install scripts: the program, the soundtrack, a launcher entry, an icon, and
the licence. Omawing runs as your user, makes no network connections, and runs
no program other than `hyprctl` (to read the window rounding).

**Build it yourself from the recipe.** This is exactly what the AUR does. Read
[aur/PKGBUILD](../aur/PKGBUILD) first if you like; it downloads the tagged source
from this repository, checks its SHA-256, and builds it with Arch's compiler
flags:

```sh
git clone https://github.com/thepixelgardener-create/omawing
cd omawing/aur && makepkg -si
```

**Or download the package GitHub built.** Release packages are built by
[a public workflow](../.github/workflows/release.yml) in a clean Arch Linux
container, never on anyone's computer, and carry a signed record of the
workflow and commit that built them. Download `omawing-*.pkg.tar.zst` from the
[latest release](https://github.com/thepixelgardener-create/omawing/releases/latest),
then check it and install it:

```sh
gh attestation verify omawing-0.4.0-1-x86_64.pkg.tar.zst -R thepixelgardener-create/omawing
sudo pacman -U omawing-0.4.0-1-x86_64.pkg.tar.zst
```

The `aur/` directory contains the build recipe; it does not imply that the
package is published in the AUR.

To build, install a C++ compiler, `make`, `qmake6`, and the Qt 6 Base,
Declarative, Wayland, and Multimedia development packages.

To try it without installing anything:

```sh
git clone https://github.com/thepixelgardener-create/omawing
cd omawing && bin/build && build/omawing
```


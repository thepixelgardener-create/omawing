# Install Omawing

[Home](../README.md) · [Gallery](gallery.md) · [Playing](playing.md) · [Install](install.md) · [Development](development.md)

## Install

Pick whichever you're comfortable trusting. Each installs only files, with no
install scripts: the program, the soundtrack, a launcher entry, an icon, and
the licence. Omawing runs as your user, makes no network connections, and runs
no program other than `hyprctl` (to read the window rounding).

**Build it yourself from the recipe.** Read
[release/PKGBUILD](../release/PKGBUILD) first if you like; it downloads the
tagged source from this repository, checks its SHA-256, and builds it with
Arch's compiler flags:

```sh
git clone https://github.com/thepixelgardener-create/omawing
cd omawing/release && makepkg -si
```

**Or download the package GitHub built.** Release packages are built by
[a public workflow](../.github/workflows/release.yml) in a clean Arch Linux
container, never on anyone's computer, and carry a signed record of the
workflow and commit that built them. Download `omawing-*.pkg.tar.zst` from the
[latest release](https://github.com/thepixelgardener-create/omawing/releases/latest),
then check it and install it:

```sh
gh attestation verify omawing-*-x86_64.pkg.tar.zst -R thepixelgardener-create/omawing
sudo pacman -U omawing-*-x86_64.pkg.tar.zst
```

Omawing is made for Omarchy, so it isn't in the AUR.

To build, install a C++20 compiler, `make`, `qmake6`, and Qt 6.8 or newer: the
Base, Declarative, Wayland, Multimedia (with its FFmpeg backend), and SVG
packages. Building from the recipe also runs the test suite; `makepkg --nocheck`
skips it.

To try it without installing anything:

```sh
git clone https://github.com/thepixelgardener-create/omawing
cd omawing && bin/build && build/omawing
```


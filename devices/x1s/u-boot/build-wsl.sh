#!/bin/bash
# The S20's U-Boot built on the owner's PC in WSL (Ubuntu 24.04, the packages .github/workflows/x1s.yml installs),
# the same way the x1s workflow builds it, without GitHub's minutes:
#
#   wsl -d Ubuntu-24.04 -- bash /mnt/e/hakios-s20/devices/x1s/u-boot/build-wsl.sh
#
# The tree lives in WSL's own filesystem (git cannot chmod on /mnt/e under WSL 1; about 400 MB): U_BOOT, default
# ~/u-boot-x1s, cloned once by build.sh; on later runs it is put back to U-Boot's own state and
# HakiOS's and the S20's changes are applied afresh, so it always builds what this checkout has.
# HAKIOS_UEFI: a checkout of hakios-uefi at devices/manifest.conf's revision (default /mnt/e/s20-work/hakios-uefi).
# Output: u-boot-x1s.bin, .config and .dtb in $OUT (default /mnt/e/s20-g981b/builds/wsl-uboot).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
export HAKIOS_UEFI=${HAKIOS_UEFI:-/mnt/e/s20-work/hakios-uefi}
export U_BOOT=${U_BOOT:-$HOME/u-boot-x1s}
OUT=${OUT:-/mnt/e/s20-g981b/builds/wsl-uboot}
want=$(awk '/^uefi hakios-uefi/{f=1} f&&/revision/{print $2; exit}' "$ROOT/devices/manifest.conf")
have=$(git -C "$HAKIOS_UEFI" rev-parse HEAD)
[ "$want" = "$have" ] || { echo "build-wsl.sh: $HAKIOS_UEFI is at $have, the manifest wants $want" >&2; exit 1; }
if [ -d "$U_BOOT/.git" ]; then
	git -C "$U_BOOT" checkout -q -- .
	git -C "$U_BOOT" clean -fdq -e build
	git -C "$U_BOOT" apply --whitespace=nowarn "$HAKIOS_UEFI/u-boot/hakios.patch"
	cp -r "$HAKIOS_UEFI/u-boot/files/." "$U_BOOT/"
	cp -r "$HERE/files/." "$U_BOOT/"
	git -C "$U_BOOT" apply --whitespace=nowarn "$HERE/x1s.patch"
fi
bash "$HERE/build.sh"
mkdir -p "$OUT"
cp "$U_BOOT/build/u-boot.bin" "$OUT/u-boot-x1s.bin"
cp "$U_BOOT/build/.config" "$OUT/u-boot-x1s.config"
cp "$U_BOOT/build/u-boot.dtb" "$OUT/u-boot-x1s.dtb"
sha256sum "$OUT/u-boot-x1s.bin"

#!/bin/bash
# HakiOS's U-Boot for the Galaxy S20 5G (x1s): mainline U-Boot at the release HakiOS uses (uefi/u-boot/BASE), HakiOS's
# changes (uefi/u-boot/hakios.patch and files/: the hakios command, the loader's USB device protocol), this phone's files
# (devices/x1s/u-boot/files: device tree, defconfig, environment) and x1s.patch (the Exynos phone board taking S-BOOT's
# RAM banks, the screen's refresh, the heat guard, Exynos990 USB and keys, the payload's address).
#
#   devices/x1s/u-boot/build.sh            -> u-boot-x1s/build/u-boot.bin
#
# HAKIOS_UEFI: a checkout of github.com/oosexgang/hakios-uefi (default: uefi/, as tools/hakios-device.py sync makes it).
# U_BOOT: where the source tree goes (default: u-boot-x1s/; made fresh when it is not there).
# CROSS_COMPILE: an AArch64 GCC (default: aarch64-linux-gnu-).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
UEFI=${HAKIOS_UEFI:-$ROOT/uefi}
UB=${U_BOOT:-$ROOT/u-boot-x1s}
CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-gnu-}
[ -f "$UEFI/u-boot/BASE" ] || { echo "build.sh: no $UEFI/u-boot (tools/hakios-device.py sync hakios-uefi, or HAKIOS_UEFI=...)" >&2; exit 1; }

if [ ! -d "$UB" ]; then
	# GitHub's mirror of U-Boot, at the same release tag as upstream's apply.sh (source.denx.de)
	git clone -q --depth 1 -b "$(cat "$UEFI/u-boot/BASE")" https://github.com/u-boot/u-boot.git "$UB"
	git -C "$UB" apply --whitespace=nowarn "$UEFI/u-boot/hakios.patch"
	cp -r "$UEFI/u-boot/files/." "$UB/"
	cp -r "$HERE/files/." "$UB/"
	git -C "$UB" apply --whitespace=nowarn "$HERE/x1s.patch"
	echo "build.sh: U-Boot $(cat "$UEFI/u-boot/BASE") with HakiOS's and x1s's changes in $UB"
fi

out=$UB/build
make -C "$UB" -s O="$out" CROSS_COMPILE="$CROSS_COMPILE" hakios_x1s_defconfig
make -C "$UB" -s O="$out" CROSS_COMPILE="$CROSS_COMPILE" -j"$(nproc)"
ls -la "$out/u-boot.bin"

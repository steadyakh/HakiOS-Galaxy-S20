#!/bin/bash
# The S20's iOS images on the owner's Windows PC, in Git Bash (no WSL): userland/root.apfs (our programs and the
# launchd jobs) and userland/system.apfs (Darwin userland 16.5 from the owner's own IPSW, 2.5 GB).
#
#   devices/x1s/build-images.sh DIR [VAR=value ...]
#
# DIR holds our programs compiled on CI (the x1s-userland artifact of .github/workflows/x1s-kernel.yml), so
# nothing here needs the iOS toolchain; the Darwin files come from sysimage/ (unpacked with sysimage/apfs-extract.py
# and 7-Zip; never on GitHub). VAR=value pairs go to userland/build.sh (SYSTEM=1, REST=1, RC_JOBS=...).
set -e
cd "$(dirname "$(readlink -f "$0")")/../.."
P=${1:?usage: devices/x1s/build-images.sh DIR-OF-PROGRAMS [VAR=value ...]}
shift
[ -f "$P/stage0" ] && [ -f "$P/init" ] || { echo "build-images.sh: $P has no stage0 and init" >&2; exit 1; }
for d in rootfs cryptex cryptex-app 20F66__iPhone10,3_6; do
	[ -d sysimage/$d ] || { echo "build-images.sh: sysimage/$d is missing (unpack the IPSW first)" >&2; exit 1; }
done
# "python3" in Git Bash is the Microsoft Store's stub: a python3 that runs the real python
shim=$(mktemp -d)
printf '#!/bin/sh\nexec python "$@"\n' > "$shim/python3"
chmod +x "$shim/python3"
trap 'rm -rf "$shim"' EXIT
python3_ok=$(PATH="$shim:$PATH" python3 -c 'print(1)')
[ "$python3_ok" = 1 ] || { echo "build-images.sh: python does not run" >&2; exit 1; }
env PATH="$shim:$PATH" PREBUILT=1 PREBUILT_DIR="$(cd "$P" && pwd)" "$@" userland/build.sh
ls -la userland/root.apfs userland/system.apfs

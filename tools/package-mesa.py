"""Allowlisted locally compiled outputs only; never upload the build tree or SDK."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys

PROGRAMS = """init child stage0 var_store hello_dyn svc_test surf_test rwtouch rwremote
sound_test mg_test torch_test camera_test camera_avtest record_test cpu_test cputop
cow_test cg_test mali_test metal_test fps_info shm_test apfs_mmap_test vol_ls snap_test
font_test net_test battery_test keychain_test wifipw_test libhakios.dylib libhakios.sb.dylib
libhakios.tweaks.dylib libhakios.app.dylib libhakios.media.dylib HakiosAudio HakiosMetal
libhakios.gl.dylib gl_test u2f_test libOSMesa.8.dylib""".split()

def package(root, port, out):
    # A fresh directory prevents old files from being included in the upload.
    out.mkdir(parents=True, exist_ok=False)
    for name in ["hello"] + PROGRAMS:
        src = root / "userland" / (name if name == "hello" else "obj/" + name)
        if src.is_symlink() or not src.is_file():
            raise RuntimeError(f"Missing/non-regular build output: {src}")
        # Mach-O 64-bit ARM64, executable/dylib/bundle; no archives or SDK stubs.
        data = src.read_bytes()
        if (data[:4] != bytes.fromhex("cffaedfe") or
            int.from_bytes(data[4:8], "little") != 0x100000c or
            int.from_bytes(data[12:16], "little") not in (2, 6, 8)):
            raise RuntimeError(f"Unexpected output format: {src}")
        shutil.copyfile(src, out / name)
    shutil.copyfile(root / "xnu-deps/build/mesa/out/built-from", out / "built-from")
    shutil.copyfile(port / "CREDITS.md", out / "CREDITS.md")
    shutil.copyfile(port / "LICENSE", out / "LICENSE.HakiOS")
    patch = (root / "tools/mesa/panfrost-retain.patch").read_text()
    (out / "LICENSE.panfrost-retain").write_text(patch.split("Source:", 1)[0])
    # Mesa's licence inventory is documentation, not an SDK or firmware file.
    shutil.copyfile(root / "xnu-deps/build/mesa/mesa-25.0.7/docs/license.rst", out / "LICENSE.Mesa")
    patches = {str(p.relative_to(port)): hashlib.sha256(p.read_bytes()).hexdigest()
               for p in sorted((port / "patches").glob("*/*.patch"))}
    provenance = {"upstream": (port / "patches/UPSTREAM_BASE").read_text().strip(),
                  "port_commit": subprocess.check_output(["git", "-C", str(port), "rev-parse", "HEAD"], text=True).strip(),
                  "run_id": os.environ.get("GITHUB_RUN_ID"), "patches_sha256": patches}
    (out / "BUILD.json").write_text(json.dumps(provenance, indent=2) + "\n")
    files = sorted(p for p in out.iterdir() if p.is_file())
    (out / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in files))
    print("Artifact file list:")
    print("\n".join(p.name for p in sorted(out.iterdir())))

if __name__ == "__main__":
    package(*(Path(p).resolve() for p in sys.argv[1:]))

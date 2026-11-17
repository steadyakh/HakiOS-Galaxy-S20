"""Allowlisted E1 Metal build outputs only; never upload the build tree or SDK."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys

# Only what the workflow compiles here. No Apple framework, SDK or Samsung file.
PROGRAMS = ["RedwoodMetal", "libredwoodair.dylib"]

def macho_ok(src):
    if src.is_symlink() or not src.is_file():
        return False
    data = src.read_bytes()[:16]
    if data[:4] != bytes.fromhex("cffaedfe") or int.from_bytes(data[4:8], "little") != 0x100000c:
        return False
    # MH_EXECUTE=2, MH_DYLIB=6, MH_BUNDLE=8
    return int.from_bytes(data[12:16], "little") in (2, 6, 8)

def package(root, port, out):
    out.mkdir(parents=True, exist_ok=False)  # fresh: no stale files in the upload
    metal = root / "metal-e1-build"
    for name in PROGRAMS:
        src = metal / name
        if not macho_ok(src):
            raise RuntimeError(f"Missing/non-Mach-O Metal output: {src}")
        shutil.copyfile(src, out / name)

    # Text files authored in this repo / upstream, not vendor binaries.
    for name, src in [
        ("Info.plist", port / "metal-e1/userland/metal_plugin.plist"),
        ("CREDITS.md", port / "CREDITS.md"),
        ("LICENSE.z3sios", port / "metal-e1/LICENSE.z3sios"),
        ("LICENSE.HakiOS", port / "LICENSE"),
    ]:
        shutil.copyfile(src, out / name)

    provenance = {
        "upstream": (port / "patches/UPSTREAM_BASE").read_text().strip(),
        "port_commit": subprocess.check_output(["git", "-C", str(port), "rev-parse", "HEAD"], text=True).strip(),
        "run_id": os.environ.get("GITHUB_RUN_ID"),
        "e1_commit": "1323aa51d4c16816fa8fd95309f864f7e3888793",
        "metal_inputs_sha256": {
            p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted((port / "metal-e1/air").glob("*"))
        },
    }
    (out / "BUILD.json").write_text(json.dumps(provenance, indent=2) + "\n")
    files = sorted(p for p in out.iterdir() if p.is_file())
    (out / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in files))
    print("Metal artifact:")
    print("\n".join(p.name for p in files))

if __name__ == "__main__":
    package(*(Path(p).resolve() for p in sys.argv[1:]))

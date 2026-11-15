# The S20's changes to HakiOS, by area

Each folder is one area of the port, as a patch against HakiOS at the commit in [UPSTREAM_BASE](UPSTREAM_BASE)
(HakiOS 2.3.4). Apply them in order (01 to 09); together they are every S20 change outside `devices/x1s/`.

```sh
for p in ../HakiOS-Galaxy-S20/patches/*/*.patch; do git apply "$p"; done
```

A file belongs to one area only. Where a file holds changes for several areas, it is in its main area and the other
parts are named below.

| Folder | What it does | Files |
|---|---|---|
| [01-kernel-core](01-kernel-core) | XNU on the Exynos 990: GICv2 under H-Arx, PSCI by HVC, cores by their real MPIDR and through a page below 4 GB (`psci_smp.c`, `gic_v3_irq.c`); the loader mailbox for remote touches (`loader_return.c`, `kern.hakios.remote` in `hakios.platform.c`); boot-arg and symbol lists | 11 |
| [02-loader](02-loader) | xnuloader on the S20: platform entry, PSCI by HVC, the kernel's core started through a pad below 4 GB, heartbeat read past the cache, `rd_part=` (iOS image read from a partition), the userdata write window, `oem touch` | 2 |
| [03-gpu-mali-mesa](03-gpu-mali-mesa) | the Mali-G77: kernel driver through a private TTBR0 window (H-Arx drops stores made through the kernel's mappings), job queue, the GPU clock governor and touch boost (`hakios.mali.c`); Panfrost for Darwin and the G77 (`tools/mesa`); sync files for Panfrost's fences (`hakios-drm`); the GL window-server glue (`eagl.c`) | 10 |
| [04-display](04-display) | the framebuffer driver: GPU-scaled frames, the copy onto the panel, the timer vsync (`MiniIOMobileFramebuffer.cpp`), IOSurface | 3 |
| [05-input-touch-remote](05-input-touch-remote) | the touch screen (sec_ts Y79A on HSI2C15, `hakios.sec_ts.c`), the shared touch path with the GPU/CPU boost hooks (`hakios.input.c`), remote touches (`rwremote.c`) | 6 |
| [06-cpu-power-heat](06-cpu-power-heat) | the heat guard over the APM, CPU clock requests and the touch-boost CPU governor (`hakios.exynos_thermal.c`) | 2 |
| [07-storage-ufs](07-storage-ufs) | the kernel's storage service on userdata's 110 GB window (`hakios.ufs.c`), the data volume maker for TWRP, APFS tools | 4 |
| [08-userland-image-sileo](08-userland-image-sileo) | building the iOS images on Windows, `init` (data volume, Sileo kept), Sileo for Windows (`mk-jb-win.py`), launchd job lists | 10 |
| [09-build-ci-tools](09-build-ci-tools) | GitHub Actions for the kernel, Mesa and U-Boot, `xl` (the phone tool) for the S20 | 7 |

The S20's U-Boot (menu, UFS driver, device tree) is not a patch to HakiOS: it is in `devices/x1s/u-boot/`.

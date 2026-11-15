# HakiOS on the Samsung Galaxy S20 (SM-G981B, Exynos 990)

iOS 16.5's userland running on a **Samsung Galaxy S20 5G (SM-G981B, "x1s", Exynos 990, Mali-G77)**, on top of
[HakiOS](https://github.com/oosexgang/hakios-xnu-kernel) (an open-source XNU port to ARM64 phones). This repository is
**only the S20 port**: the device folder, the S20's changes to HakiOS as one patch, and the notes of every test.

It boots to the home screen with touch, the GPU drawing, and Sileo. **What it needs most now is help with smoothness**
(see *Where help is wanted*).

> Not affiliated with Apple or Samsung. No Apple or Samsung files are in this repository: you build the images
> yourself from an IPSW and from your own phone's backup.

## Status

| Part | State |
|---|---|
| Boot | S-BOOT starts HakiOS's U-Boot as the boot image's kernel; a graphical menu (volume keys, power) |
| Kernel | XNU boots (GICv2 mode under Samsung's H-Arx hypervisor, 26 MHz timer, PSCI by HVC) |
| CPU cores | 8 start (A55 x4, A76 x2, Mongoose M5 x2); cores sent through a page below 4 GB (the firmware keeps only 32 bits of CPU_ON's address) |
| Heat | kernel heat guard over the APM (power off at a set temperature) |
| Display | the framebuffer S-BOOT leaves (1440x3200, 60 Hz); frames scaled on the GPU, then copied |
| GPU | Mali-G77 via Mesa **Panfrost** (G77 added); register access through a private TTBR0 window (H-Arx drops stores made through the kernel's own mappings) |
| GPU clock | governor 260 -> 800 MHz on load and on touch (APM DVFS domain 10) |
| CPU clock | A76/A55 boosted on touch through the APM; **Mongoose clock not done** (Samsung's HIU) |
| Touch | sec_ts (Y79A) on HSI2C15, panel power through the display PMIC |
| Storage | U-Boot reads the UFS (S-BOOT leaves it running but asleep: START STOP UNIT, then the FMP/SMU secure calls) |
| Boot from storage | iOS's system image in SUPER, read into RAM in ~4 s; no PC needed |
| Writable /var | 110 GB HFS+ volume in userdata, kept between boots (works from the PC; from the menu: being fixed) |
| Sileo | in the image, installs kept on the writable volume |
| Remote control | `fastboot oem touch <steps>` while iOS runs (tap/swipe/home), for testing without hands on the phone |
| Wi-Fi, cellular, sound, camera | not done |

## Where help is wanted

1. **Smoothness while swiping** (the main problem). Measured per frame: during a swipe frames arrive 7-48 ms apart
   (about 50 fps, uneven) instead of a steady 16.7 ms. The GPU part of a frame is ~1.5 ms; the rest is the CPU side
   (Mesa, IOKit calls, CoreAnimation). Ideas not yet done:
   - a real vblank from Samsung's DECON (dpu30) driving CoreAnimation instead of a timer (reading DECON from the
     kernel froze the phone once; the GPU's TTBR0-window method may be the way);
   - the panel's 120 Hz mode (DSI commands + DECON timing);
   - the Mongoose cores' clock through Samsung's HIU (they sit at ~1 GHz; with them on, frames get slower);
   - zero-copy scan-out of the window server's surface (Scion's `fb_zc` on the Exynos 9810 is a model).
2. **The writable boot from the menu** (works when started from a PC; from the menu iOS stops drawing after a while).
3. Wi-Fi (BCM4375), sound, cellular.

Details, numbers and every test run are in [docs/PLAN.md](docs/PLAN.md).

## Layout

| Path | What |
|---|---|
| `devices/x1s/` | the S20: boot image packer, image build script, U-Boot (patch, device tree, menu, UFS driver), configs |
| `patches/hakios-2.3.4-s20.patch` | every S20 change to HakiOS outside `devices/x1s` (kernel drivers, loader, Mesa glue, tools) |
| `patches/UPSTREAM_BASE` | the HakiOS commit the patch applies to |
| `docs/PLAN.md` | the full log: phone facts, safety rules, every test and its result |

## Building

```sh
git clone https://github.com/oosexgang/hakios-xnu-kernel hakios && cd hakios
git checkout $(cat ../HakiOS-S20/patches/UPSTREAM_BASE)
git apply ../HakiOS-S20/patches/hakios-2.3.4-s20.patch
cp -r ../HakiOS-S20/devices/x1s devices/
```

Then follow `docs/PLAN.md` (kernel and Mesa build on Linux/CI, images on Windows or Linux from your own IPSW,
`devices/x1s/mkbootimg.py` with your own phone's stock `boot.img`).

## Safety rules (please keep them)

- Only the **boot** partition is written for tests (from TWRP). Never bootloader, param, up_param, efs, sec_efs or vbmeta.
- Keep backups of your phone's partitions (efs/sec_efs hold the IMEI) **out of git**.
- The heat guard stays on; short runs.
- No firmware, IPSW contents, device trees from your phone or other vendor files in commits.

## Credits

[HakiOS](https://github.com/oosexgang/hakios-xnu-kernel) (the XNU port this builds on), the
[Scion Project](https://github.com/Scion-Project) (HakiOS on the Galaxy S9, Exynos reference), U-Boot, Mesa/Panfrost,
Linux's and Samsung's open-source Exynos 990 kernel sources (read for register facts),
[ios-sweet](https://github.com/dipa-studio/ios-sweet), [UniXNU](https://github.com/UniXNU/UniXNU),
[iOSPlus10](https://github.com/akshaynexus/iOSPlus10) for how such a port can be shared.

## License

Files here keep the licenses of the HakiOS files they come from or change (see HakiOS's `LICENSE`, `LICENSE.APSL`,
`THIRD_PARTY_LICENSES.md`); U-Boot parts are GPL-2.0+. See [LEGAL.md](LEGAL.md).

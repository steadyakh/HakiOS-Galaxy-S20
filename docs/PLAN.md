# Plan: HakiOS on the Samsung Galaxy S20 5G (x1s, SM-G981B)

Status: **planning only. Nothing has been flashed.** Boot route chosen: Plan A2 (U-Boot), 10 Nov 2026. Started 10 Nov 2026.

This is its own project (`E:\hakios-s20`, branch `s20`), based on clean upstream HakiOS 2.3.4 (63fc2b4). The A71 port is
halted and is not part of it. The device folder is `devices/x1s/` (made with `tools/hakios-device.py new`). The device tree
the numbers below come from is `E:\s20-g981b\x1s-device-tree.dtb` (Samsung's file: private, never committed).

## The phone

| What | Value |
|---|---|
| SoC | Samsung Exynos 990 (`samsung,exynos990`), not Qualcomm |
| CPUs | 4x Cortex-A55 (MPIDR 0x0-0x3, max 2.0 GHz), 2x Cortex-A76 (0x4-0x5, 2.5 GHz), 2x Samsung Mongoose M5 (0x100-0x101, 2.73 GHz). Cores start through **PSCI 1.0**, like the A71 |
| Interrupt controller | **GICv2** (`arm,cortex-a15-gic`): distributor 0x10101000, CPU interface 0x10102000. Our kernel only has GICv3 (`xnu/files/osfmk/arm64/gic_v3_irq.c`) |
| RAM / screen | 12 GB / 1080x2400 AMOLED (has old burn-in) |
| Screen path | DECON (`decon_f` 0x19050000) -> DSIM 0x190C0000 -> panel. The bootloader leaves a picture on, so a framebuffer it set up can be used at first |
| Storage | UFS 3.0, `samsung,exynos-ufs` at 0x13100000 (standard UFSHCI + Samsung parts, PHY 0x13104000) |
| USB | DWC3 at 0x10E00000 (same USB core as on Qualcomm, different PHY) |
| Temperature | TMU: 0x10090000 (BIG, MID, LITTLE), 0x100A0000 (G3D, ISP, NPU), `exynos-tmu-v2`, also handled by the ACPM firmware |
| Watchdogs | 0x10050000 (`exynos9-v3-wdt`) and 0x10060000. Check whether the bootloader leaves one running |
| GPU / Wi-Fi | Mali-G77 (0x18500000) / Broadcom BCM4375 |
| Bootloader | `G981BXXSNHYB1` (revision N). Unlocked, Knox already tripped. TWRP (ArtisanROM). Runs a ColorOS 16 port |

These numbers come from the phone's own device tree (inside the boot partition backup) and a read-only adb check.

## Safety rules for this phone

1. **Only the boot partition is ever written**, by the owner in TWRP. Never sboot/bootloader, up_param, param, efs, sec_efs, vbmeta.
2. Backups (verified with sha256) are in `E:\s20-g981b\backup-20261110\`: boot, recovery, dtbo, vbmeta, vbmeta_samsung, efs, sec_efs. **Private (efs has the IMEI); never on GitHub.** To undo any test: flash `boot.img` back in TWRP.
3. Full reset: stock firmware SM-G981B, EUX, bootloader revision N or newer, with Odin (from Download Mode).
4. **Heat** (the Exynos 990 runs hot; the owner worries about the screen going green):
   - Never raise the CPU speed. Keep whatever the bootloader set.
   - Do not start the two Mongoose cores (0x100, 0x101) until temperature reading works. Start with 1 core, then the A55s.
   - Read the TMU early. If any sensor reaches **70 C**, power off (PSCI SYSTEM_OFF).
   - Short test runs. Do not leave a bright, still picture on the screen.
   - If the kernel freezes: hold Volume Down + Power for about 10 s (forced restart by the power chip), then Volume Up + Power to go to TWRP. Test this once at the start.

## How it will start (no UEFI exists for this phone)

The A71 starts XNU from our UEFI (`xnuloader/` is an EFI program). No UEFI exists for the Exynos 990 S20. Upstream HakiOS
also starts the same loader from **U-Boot** on a real phone (POCO X5 Pro, `docs/u-boot.md`) and in QEMU
(`devices/qemu/u-boot/`): U-Boot gives the loader enough of UEFI (screen, memory map, later storage and USB) to run it unchanged.

### What was found (web research, 10 Nov 2026; the phone was not used)

| Question | Answer |
|---|---|
| U-Boot on the Exynos 990? | **No working, published U-Boot for the Exynos 990 or x1s could be confirmed.** Mainline U-Boot has a generic board for Samsung Exynos phones, `exynos-mobile` (2025-26, `exynos-mobile_defconfig`), but its documented devices are Exynos 7870 phones only. One fork, `exynos990-freaky-mainline/u-boot` on chleb.me, could not be read (its site redirects) |
| How U-Boot is started on Samsung phones | The same way on every Exynos phone: **U-Boot replaces the Linux kernel inside boot.img**; Samsung's S-BOOT loads it and jumps to it (device tree address in x0). Only `boot` is written. `exynos-mobile` packs `u-boot-nodtb.bin` as the kernel; on the 7870 S-BOOT ignores the load offsets |
| Other loaders that run on **this** phone | **barebox** (a U-Boot-like bootloader): x1s support merged upstream in Sep 2025, "only a minimal set of features", the screen works, no UFS/USB/EFI mentioned. **uniLoader** (GPL-2.0, a tiny shim by the same developer): has an `x1s` board. Both prove that a replaced boot.img kernel starts and can draw on the screen |
| EFI for `xnuloader`? | U-Boot's own EFI layer (`bootefi`) does this on any board, and upstream already uses it. barebox's x1s patches do not mention EFI |
| Mainline Linux | `exynos990-x1s.dts` + `exynos990-x1s-common.dtsi` are in Linux since 6.14 (Umer Uddin); USB nodes were added in 2025. This is the device tree a U-Boot for x1s would use |
| postmarketOS | Boots x1s with a second-stage loader, because S-BOOT merges its own device-tree pieces into the tree it hands over. (Their wiki now blocks automated reading; this is from search snippets) |
| Another iOS-on-Exynos-990 project | `N1709/ios-exynos990` (own loader, aimed at the S20 Ultra). By its own README no phone has shown a picture yet and boots end in a reset loop. Not a source of proven facts |

### The framebuffer (mainline Linux and uniLoader agree)

- **Address 0xF1000000, 1440 x 3200 pixels, stride 5760 bytes (1440 x 4), format a8r8g8b8** (32-bit, `FB_FORMAT_ARGB8888`),
  size 0x1194000 (about 18 MB). The panel really is 1440x3200; ColorOS's 1080x2400 is Android drawing smaller and the screen scaling it up.
- Mainline reserves it (`cont_splash_mem`, no-map). **The phone's own Samsung device tree does not**, so our loader has to
  reserve it itself. It does not overlap any Samsung reservation (`cp_rmem` ends at 0xF0800000, the audio areas start at 0xF4200000).
- **S-BOOT leaves screen refresh switched off.** Pixels written to the buffer do not show until one register is set:
  **0x19050070 (DECON_F `HW_SW_TRIG_CONTROL`) = 0x1281** (bit 12 TRIG_AUTO_MASK_EN, bit 8 SW_TRIG_EN, bit 0 HW_TRIG_EN).
  barebox, uniLoader and the Linux SoC patches all do exactly this. It is a display register, not storage or power;
  it is still shown to the owner before the first test that uses it.
- RAM banks in mainline's x1s tree (S-BOOT fills `/memory` itself; this is what it is expected to say):
  0x8000_0000 +0x3AB0_0000, 0xC120_0000 +0x1EE0_0000, 0xE190_0000 +0x1E70_0000, 0x8_8000_0000 +0x2_7E80_0000 (about 11.85 GB).

### Found in our own files (read only)

- The stock boot.img (header v2, page 2048) says kernel at 0x10008000, ramdisk 0x11000000, dtb 0x10000000, cmdline
  `androidboot.hardware=exynos990 loop.max_part=7`; its kernel is 42 MB. barebox uses the same layout (`--base 0x10000000`).
  Where S-BOOT really puts the kernel is not known yet (U-Boot must be position independent, or built for that address).
- **`xnuloader/loader.c` puts its payload at 0xF0000000 (64 MB, `PAYLOAD_BASE`).** On x1s that is the modem's memory
  (`cp_rmem`) and the framebuffer. The loader needs a payload address per phone (for x1s e.g. in the high RAM bank, above
  Samsung's `sec_rdx_bootdev` at 0x8_8000_0000 +0xAD0_0000). Needed whatever plan is used.
- The loader knows a phone by its framebuffer address (`platform_redwood` at 0xE1000000) and has only GICv3 fields:
  x1s needs its own `platform` entry (framebuffer 0xF1000000, GICv2 0x10101000/0x10102000, PSCI by SMC).

### Recommendation: Plan A2, HakiOS's U-Boot on x1s (built on mainline `exynos-mobile`)

Why: upstream's loader (4,370 lines) needs a UEFI-like environment. A boot shim (Plan A) would have to rebuild much of it
(boot arguments, Apple device tree, memory map, later storage and USB). U-Boot gives all of that, upstream's tools
(`tools/mkubootimg.py`, the `hakios` command, `xl`) already work with it, and `exynos-mobile` already does the S-BOOT
hand-off for Samsung phones. What x1s adds is small to start with: the x1s device tree (from Linux), the DECON register
write, and a boot.img S-BOOT accepts (header v2, the stock dtb kept in it).

Steps (desk work first, nothing on the phone):
1. Get mainline U-Boot (`exynos-mobile`) and read how it takes memory and the device tree from S-BOOT, and whether x1s's
   header-v2 boot.img fits it. Settle where to build it (Windows without WSL: MSYS2 or a CI runner).
2. Add x1s: `exynos990-x1s.dts` from Linux, the framebuffer as `simple-framebuffer` (U-Boot gives it to the loader as the
   EFI screen), the DECON write, the payload area moved off 0xF0000000, EFI on, one core only, no clock changes.
3. **First phone test (needs the owner's OK):** boot.img = U-Boot only. Expected: U-Boot's text on the screen, nothing
   else touched. Undo: flash the backup boot.img in TWRP.
4. Then the loader with a hello payload (as on the POCO X5 Pro), then XNU.

**Fallback, Plan A:** if U-Boot does not come up and cannot be fixed quickly, a uniLoader-style shim (paint the screen,
then jump) is the smallest way to prove the boot chain, and the base for a hand-written loader.
**Plan B** (a full edk2 UEFI) stays the last resort.

Sources: U-Boot docs `board/samsung/exynos-mobile` (docs.u-boot.org); barebox x1s series (lore.barebox.org, applied
9 Sep 2025); github.com/ivoszbg/uniLoader (`board/samsung/board-x1s.c`, `configs/x1s_defconfig`); Linux x1s DT patches v6
(lists.infradead.org, Dec 2024) and the samsung-dt64-6.14 pull; github.com/N1709/ios-exynos990.

### Built so far (10 Nov 2026; the owner chose "do what you have to": Plan A2)

| Piece | Where | What it does |
|---|---|---|
| U-Boot for x1s | `devices/x1s/u-boot/` (`build.sh`, `x1s.patch`, `files/`) | Mainline U-Boot v2026.07 (`exynos-mobile`) + upstream HakiOS's U-Boot changes (hakios-uefi) + x1s: device tree (Linux's x1s tree, every fixed Samsung carve-out, the payload area), RAM banks from S-BOOT's tree, the DECON refresh write, keys (new Exynos990 pin controller driver), USB (Exynos990 variant of the USB PHY), the heat guard. Menu first; nothing starts by itself |
| Heat guard (U-Boot) | `x1s.patch` (exynos-mobile.c) | Powers the phone off (PSCI SYSTEM_OFF by SMC) after `hakios_max_minutes` (default **10**), with a warning a minute before. Runs whenever U-Boot waits. The temperature itself is not read yet: Samsung reads it through the ACPM firmware (IPC, `acpm_ipc` at 0x15900000, TMU channel 9), not from the TMU registers; that comes next, checked on the phone first |
| Boot image packer | `devices/x1s/mkbootimg.py` | boot.img v2: U-Boot as the kernel (text_offset 0x80000 like the stock kernel), optional HKUB ramdisk, the stock image's header values and device tree taken from the local backup (never committed) |
| Loader | `xnuloader/loader.c` | `platform_x1s`: found by the framebuffer 0xF1000000; payload at 0xC8000000; 26 MHz timer (S-BOOT leaves CNTFRQ_EL0 unset); GIC-400 node for the kernel; 6 cores at most (Mongoose cores left out); never pokes QEMU's fw_cfg on a phone |
| Build | `.github/workflows/x1s.yml`, private repo `steadyakh/hakios-s20` | `u-boot-x1s.bin` and `loader-open.efi` (no Apple data) as artifacts |

Not done yet, in order: the first phone test below; reading the temperature (ACPM); the kernel's GICv2 driver and timer;
the loader + hello payload through U-Boot; then XNU, iOS in RAM over USB (`xl`, as on the A71), touch.

### Phone tests, 11 Nov 2026: U-Boot runs on the S20

| Test | Result | What it showed |
|---|---|---|
| 1 | Samsung logo stays | S-BOOT's own log (TWRP `/proc/last_kmsg`): our image loaded, "Starting kernel...". S-BOOT also starts its hypervisor H-Arx at EL2: U-Boot runs at EL1 |
| 2 | red + green bars | U-Boot runs C code and reads the RAM; stops before board_init |
| 3 | a square per initcall | stops in initr_caches (MMU on). S-BOOT lists its 24 RAM ranges in **descending** order: they were not merged, the framebuffer's bank was left out of the MMU's map |
| 4 | **the menu, driven with the keys** | bdinfo: 4 banks (939, 494, 487 MiB, 10 GiB), relocated to 0xAFE750000, framebuffer@f1000000 active, 21 reserved regions |

| 5 | menu returns; power off restarts | PS_HOLD low does cut the power (S-BOOT: PMIC_OFFSRC MAIN_OFF PWRHOLD); the phone comes straight back on |
| 6 | **temperatures read** (about 22 C, phone cool); still restarts | the APM answers on its IPC queues (TMU channel 9); the APM STOP message alone is not enough |
| 7 | **power off stays off** | the power chip's WTSR and SMPL were on (RTC 0x01 = 0xd3); cleared first through the APM, as Samsung's kernel does, the phone stays off |

| 8 | power off by default | the menu's Power off and the heat guard switch the phone off |
| 9 | **the HakiOS loader runs** | from the boot image's HKUB container: EL1, MIDR 412fd050, display 1440x3200 at 0xF1000000, "platform x1s, payload at c8000000, timer 26000000 Hz" |
| 10-12 | USB silent | clocks on, DWC_usb31 1.10a (GSNPSID 0x33313130); the controller set up but forced to SuperSpeed, run bit never set |
| 13 | **USB works** | U-Boot compared a DWC_usb31's VER_NUMBER with DWC_usb3 revisions and gave it the oldest cores' workarounds (fixed: DWC3_REV_LT/LE/GT/GE). Windows: "Android Bootloader Interface" 18d1:4ee0; `fastboot getvar product` = samsung-x1s; `fastboot oem console` reads U-Boot's log |

| 14 | menu with "Restart into TWRP" | works (PMU INFORM3 = 0x12345604, INFORM2 bits 16-23 = 0x4E, reset) |

### The kernel on the S20, 11 Nov 2026

| Run | What happened |
|---|---|
| kernel 1 | the loader loaded the kernel and built its device tree, then stopped at `mrs icc_pmr_el1` (ESR 0x02000000): GICv3 system registers are undefined instructions below H-Arx. Fixed: `loader_has_icc` |
| **kernel 2** | **the Darwin kernel boots**: GICv2 driver, 26 MHz timer, VM, IOKit, `MiniIOMobileFramebuffer: display 1125x2436 on a panel of 1440x3200`, BSD up (`bsd_init: done`), `/sbin/launchd` from the RAM disk = `hello`, which runs and exits 0 ("pid 1 exited ... exit 0", the panic hello always ends in), back to the loader, restart into U-Boot |

| heat guard | `drivers/power/hakios.exynos_thermal.c` (exynos_thermal=1): the kernel reads BIG/MID/LITTLE from the APM every second (25-26 C at rest); with exynos_thermal_stop=20 it powered the phone off (WTSR/SMPL off, APM stop, PS_HOLD low), a USB cable in. 70 C is the default and the most |

Next: iOS's user space in RAM over USB, as the A71's first boot did: the loader's own USB link (`xl`) through U-Boot's
EFI USB device protocol, and the Darwin 16.5 system image made from the owner's own IPSW (`sysimage/unpack.sh`;
iPhone10,3/10,6 16.5 20F66) with the loader built with the iPhone's identity on this computer (Apple's data never on
GitHub).

Unpacked on Windows (11 Nov), in `sysimage/` (gitignored): the root file system with Windows 7-Zip (`x1s-rootfs/`, all
43,230 chosen files but the 12 whose names hold `*` or `?`, which Windows cannot name: web-credential JSONs and one sample
shortcut; the two big fonts trimmed). Windows 7-Zip skips every LZBITMAP file, so the cryptexes come out through
`sysimage/apfs-extract.py`: HakiOS's APFS reader taught the sealed-volume layout (header-less tree nodes, child links
counted from the root's identifier, the fext extent tree) plus LZBITMAP/LZVN/LZFSE through two DLLs built from open source
in CI (`.github/workflows/x1s-wintools.yml`). Symbolic links go to a `.manifest` next to each tree (mkapfs makes them).
Checked: every file 7-Zip could read is byte-identical, every size matches its listing; the OS cryptex (1,411 files), the
App cryptex (884) and the dyld shared cache (43 files, all 42 sub-cache UUIDs match the main cache) are out, and so is
`DeviceTree.d22ap.im4p`.

**The images, made on Windows (11 Nov):** our programs are compiled on GitHub against the SDK alone
(`COMPILE_ONLY=1 userland/build.sh` in `x1s-kernel.yml`, artifact `x1s-userland`, 37 programs; upstream's
`prebuilt/userland` is 46 commits behind). Then, in Git Bash: `devices/x1s/build-images.sh <that artifact> SYSTEM=1`
makes `userland/root.apfs` (4 MB) and `userland/system.apfs` (2.48 GB) in 2 minutes. Git Bash cannot make symbolic
links, so `build.sh` notes them and `mkapfs.py` puts them in (`MKAPFS_LINKS`); Windows has no permission bits, so
`mkapfs.py` makes Mach-O files and scripts 0755 and the rest 0644, and the manifest decides where it has a say.
Checked inside the image: 45,031 files, 671 links (the root's 667 + LaunchDaemons, the audio plug-in, libstdc++ x2),
`/sbin/launchd` = our stage0 (same SHA-256 as CI's), no world-writable file.

**The loader with the identity (11 Nov):** LLVM 23.1.4 (llvm-project's own release, its attestation checked) unpacked
to `E:\s20-work\llvm` (clang, lld-link, clang's headers; nothing installed on C:). `xnuloader/identity.h` made from
`sysimage/DeviceTree.d22ap.im4p` (im4p.py, LZFSE through lzfse.dll, mkidentity.py; ignored by git), then
`MSYS_NO_PATHCONV=1 PATH=/e/s20-work/llvm/bin:$PATH xnuloader/build.sh` -> `loader.efi` (72 KB).

**The loader's own USB link:** the loader speaks fastboot through the EFI USB device protocol, which U-Boot provides
(`efi_qcom_usb.c`, on the DWC3 fixed in test 13). It now also gives Windows the Microsoft OS descriptors U-Boot's own
fastboot gives (WinUSB, the same interface GUID), so the loader (18d1:d00d, "xnuloader") needs no driver. `xl` knows the
S20 (`XL_FIRMWARE=x1s`, found from `product: samsung-x1s`): it sends the loader and its USB payload to U-Boot's fastboot
as an HKUB container (`fastboot stage` + `fastboot continue`).

### Test 15 (ready, waiting for the owner): iOS's user space in RAM

Image: `E:\s20-g981b\hakios-x1s-boot-test15.img` (U-Boot run 43094360754 + its loader-open; sha256 5c4d9485...01bf5;
1,146,880 bytes). New in U-Boot: the 30-minute time limit ("time limit (not heat)") and the loader's Windows descriptors.
Nothing new is written to the hardware: the same USB set-up as tests 13-14, the same kernel and heat guard as the
kernel runs; no storage is touched.

1. Owner: TWRP, flash the image to **boot** (pushed to /tmp first, sha256 checked). Restart: U-Boot menu, then
   "HakiOS over USB (fastboot)".
2. PC, in Git Bash: `./xl enter` (the loader on USB: does Windows bind it?), then `./xl ramdisk userland/system.apfs`
   (2.4 GB into RAM).
3. One short run, heat rules kept (one core, the guard on, back after 90 s):
   `XL_FIRMWARE=x1s XL_HOME=fastboot XL_KERNEL=E:/s20-g981b/builds/kernel-43092681287/out/kernel-x1s XL_CMDLINE="-v cpus=1 exynos_thermal=1" XL_SECS=90 ./xl run`
4. Goal: Darwin's launchd starts from the system image. A lock screen this first time would be luck: the log says
   where it stops.

| Run (11 Nov) | What happened |
|---|---|
| 15a | Windows gave the loader (18d1:d00d) no usable driver. Fixed: the loader built with `LOADER_CFLAGS=-DXNULOADER_USB_PID=0x4ee0`, U-Boot fastboot's ID, so Windows uses the Google driver it has for U-Boot. Then `xl enter` works: "loader is up" |
| 15b | system.apfs 2,360 MB into RAM in 57-61 s (~40 MB/s). Kernel, init's checks, **Darwin's launchd starts**, then quits: "unable to resolve 'mobile'". Cause: CR LF from select-system.py on Windows put a CR at the end of every link target (`/etc` -> `private/etc\r`). Fixed (f639a56) |
| **15c** | **iOS's user space runs on the S20**: init 36 checks, 0 failed; launchd's boot tasks; SpringBoard (pid 6), backboardd, containermanagerd, usermanagerd, logd, notifyd, cfprefsd, fseventsd, distnoted, wifid; HakiOS's SpringBoard library ("no GPU draws ... the lock screen's clock is white"). 90 s, returned by itself, no panic; 26-28 C |

The owner saw no iOS picture on the panel. The run log stopped at 32,767 bytes and `xl screenshot` said "the loader has
no display": one cause (9621c00). With no logdump partition the loader echoed the kernel log into its own 32 KB buffer
as under QEMU, which then cut every log short and left no room for the "getvar screen" answer. Fixed: echo under QEMU
only, and "oem log" after a run carries the last kernel log. The display driver logs each swap
("MiniIOMobileFramebuffer: swap N"), so the next log says whether iOS draws.

Run 15d: the same, 180 s, on the four Cortex-A55s (`cpus=4`: the loader's core order is 0-5, so the A76s and the
Mongooses stay off; the heat guard at 70 C as always), then `./xl screenshot logs/s.png` of what the panel shows.
One little core at the boot clock may simply be too slow to draw the lock screen in software within 90 s.

| Run (12 Nov) | What happened |
|---|---|
| **15d** | **iOS 16.5 BOOTS ON THE GALAXY S20.** `cpus=4` (the A55s), 180 s: backboardd's boot UI (the Apple logo), then SpringBoard draws the **home screen**: every app icon, the dock, the status bar clock (12:07) and battery. The owner saw it on the phone; `./xl screenshot` (1440x3200) shows it, with the `-v` kernel text drawn over it. The full log (303 KB) came back; 17 frames swapped to the panel; 26-31 C all run. No panic: the run ended on its timer |

Next: the same without `-v` (a clean picture); touch (the S20's touch controller, a kernel driver like
`drivers/input/hakios.goodix.c` is for redwood); the lock screen's own path; then longer runs with the heat guard.

### Touch

From the phone's device tree as S-BOOT hands it over (`E:\s20-g981b\logs\test1\live-fdt.dtb`) and Samsung's kernel
(github.com/ExtremeXT/android_kernel_samsung_exynos990: `drivers/input/touchscreen/sec_ts/y79a`, `i2c-exynos5.c`,
`exynos9830.dts`):

| Part | What it is |
|---|---|
| chip | `sec,sec_ts`, Samsung's Y79A (firmware `tsp_sec/y79a_x1.bin`, in the chip), I2C address 0x48, max coordinates 4096 x 4096 |
| bus | hsi2c15 at 0x108D0000 (USI07's I2C, PERIC1), `samsung,usi-i2c-v2`: USI_CON (0xC4) = 0 takes it out of reset. Its USI mode register (`usi@10721024`, "usi_7_i2c") is disabled in Samsung's own tree: an I2C-only engine, nothing to select |
| pins | GPP5-6/7 (0x10730000: CON +0, DAT +4, PUD +8, DRV +0xC), function 3 |
| interrupt | GPA1-0 (0x15850020 bank: CON, DAT +4), low while an event waits: looked at, not taken as an interrupt |
| supplies | S2DOS05 LDO2 `tsp_io` 1.8 V and LDO3 `tsp_avdd` 3.0 V, both `regulator-boot-on` |
| clocks | CMU_TOP PERIC1 BUS/IP (0x1A330000 + 0x20d4/0x20d8), CMU_PERIC1 (0x10700000) USI_I2C mux 0x06c0, div 0x182c, gates 0x2000-0x2120 |
| protocol | 0x55 boot status (0x20: application), 0x22 device ID, 0x10 sense on, 0x60 one event (16 bytes), 0x61 the rest (byte 7 bits 0-4: how many), 0x62 clear the stack. Event: byte 0 bits 0-1 kind (0 coordinate), 2-5 finger, 6-7 action (1 press, 2 move, 3 release); x = b1 << 4 \| b3 >> 4, y = b2 << 4 \| b3 & 15 |

Steps: (1) `touchcheck` in U-Boot, read only (menu item 7, commit bbc73c0; test 16 image `E:\s20-g981b\hakios-x1s-boot-test16.img`, U-Boot run 43125742384, sha256 748efabc...75377): the clocks, pins, the interrupt line and the
I2C block's registers. (2) `touchtest` in U-Boot: pins to I2C, the I2C block out of reset, read 0x55 and 0x22, sense on,
fingers on the screen for 20 s; its writes are listed for the owner's OK first. (3) the kernel driver

| Test (12 Nov) | What happened |
|---|---|
| 16 | `touchcheck`: CMU_TOP PERIC1 BUS/IP gates on (0x00200000), CMU_PERIC1 USI_I2C mux 0x10, div 1; GPP5 CON 0x00000000 and PUD 0x11...: pins 6/7 plain inputs with pull-downs, not I2C; GPA1-0 an input with a pull-down. S-BOOT leaves the touch screen alone. Then an external abort reading CMU_PERIC1 0x2068, a hole in its register map, before the I2C block was read. Fixed: only the gates Linux names |
| **run 22** | **TOUCH WORKS IN iOS ON THE S20** (12 Nov 08:07). The kernel's `sec_ts=1`: supplies on (EN 19 -> 1f), status 20, ID 53 45 37 91 00, 4096 x 4096; 115 events, 113 finger positions, 0 bad. backboardd made a touch stream for SpringBoard; the owner's swipe took the home screen to its second page (Weather, Stocks, Find My, Home...: `./xl screenshot`, no `-v`, a clean picture). 180 s, cpus=4, 30-31 C, no panic. Log and screenshot in `E:\s20-g981b\logs
un22\` |
| run 24 (ready) | the same as run 23 with the loader fixed (7fe7e86: PSCI by HVC; built here with the identity and `LOADER_CFLAGS=-DXNULOADER_USB_PID=0x4ee0`): the five other cores should now start |
| run 23 | `cpus=6`: the kernel asked for cores 4, 5, then 1, 2, 3, and **none started: "1 of 6 processors are running"**; run 22 the same ("1 of 4"). iOS has run on one Cortex-A55 all along, hence much of the lag. Cause: the device tree S-BOOT hands over says psci method **"hvc"** (to H-Arx), the loader said "smc". ui_lite=1 on, wallpaper_freeze=1 on; 31-34 C; no panic. Log and screenshot in `E:\s20-g981b\logs\run23\` |
| run 23 (as planned) | Less lag without a GPU (the owner: "very laggy"): **`cpus=6`**, the two Cortex-A76s as well as the four A55s (the loader's core order is 0-5; the Mongooses, 0x100/0x101, stay off; no CPU speed-up), and HakiOS's software-drawing settings **`ui_lite=1`** (Reduce Transparency and Reduce Motion: no blur, fades for zooms) and **`wallpaper_freeze=1`** (the wallpaper as one still picture). Same kernel, nothing to flash: `XL_CMDLINE="cpus=6 exynos_thermal=1 sec_ts=1 ui_lite=1 wallpaper_freeze=1" XL_SECS=180`. First time the A76s run on this phone: a hang means force restart |
| run 22 (as planned) | the kernel's touch driver `drivers/input/hakios.sec_ts.c` (commit 5f8d7d7; kernel run 43179211650, 27 of 27 drivers, built clean): `sec_ts=1` does in the kernel what touchtest does in U-Boot (the same writes: S2DOS05 EN \|= 0x06 by hand on GPP1-0/1 only if LDO2/LDO3 read 0x0C, GPP5-6/7 to I2C, GPA1-0 no pull, hsi2c15) and posts the touches to `/dev/hakios-touch`. Run: menu "HakiOS over USB", `./xl enter`, `./xl ramdisk userland/system.apfs`, `XL_FIRMWARE=x1s XL_HOME=fastboot XL_KERNEL=E:/s20-g981b/builds/kernel-43179211650/out/kernel-x1s XL_CMDLINE="cpus=4 exynos_thermal=1 sec_ts=1" XL_SECS=180 ./xl run`; no `-v`, so the picture is clean |
| **21** | **TOUCH WORKS (in U-Boot).** Supplies on (EN 0x19 -> 0x1f), bus idle high, boot status 0x20 (its firmware running), device ID 53 45 37 91 00, clear and sense on OK; **102 events in 20 s**: "finger 1 down/move/up" following the owner's fingers (e.g. 1991,2059; 2205,2101; 2463,1988 on the chip's grid), supplies back off at the end. The event format, finger id (bits 2-5, from 1) and coordinates are as Samsung's sec_ts.h says |
| 21 (built) | `E:\s20-g981b\hakios-x1s-boot-test21.img` (U-Boot run 43177997433, sha256 a8d12646...a24b7c): the bus's errors from INT_STATUS (bits 8-11, bit 10 = no device) as i2c-exynos5.c's interrupt handler takes them; TRANS_STATUS no longer read as errors. Same writes as tests 17-19 |
| 20 | our USI mode register (0x10721024) already 4 (I2C), hsi2c14's 0; supplies on, the bus lines high (pulled up, idle), the chip's IRQ line low (alive). Every message "failed" with TRANS_STATUS 0x000a0009: its low four bits are the master's state (9, a transfer under way), which the code took for no-ACK and gave up at once. CTL reads 0 even after writes |
| 20 (built) | `E:\s20-g981b\hakios-x1s-boot-test20.img` (U-Boot run 43177160810, sha256 a4ec778b...43dfc8): master mode set in every message; 500 ms after the supplies; both USI mode registers, USI_CON and the bus lines shown; the status read tried 5 times with the controller's state on each failure. Menu 9 `touchtest usi` also writes **0x10721024 = 4** (our USI's mode register to I2C, Samsung's value): the owner's OK first |
| 19 | the power chip by hand works: S2DOS05 DEV_ID 0xff, TOPSYS 0xff, STAT 0xf9, **EN 0x19** (LDO1, LDO4, BUCK1: the display's; the touch rails LDO2/LDO3 off), LDO2_CFG and LDO3_CFG 0x8C, each the same twice; pins back as S-BOOT left them. `touchtest`: supplies on (EN 0x1f), then the touch chip still -19 to 0x55 and 0x22; the touch bus controller's CTL read 0 after 0x8 was written; EN back to 0x19 |
| 19 (built) | `E:\s20-g981b\hakios-x1s-boot-test19.img` (U-Boot run 43129667902, sha256 53b5c878...ad972): the power chip by hand on GPP1-0 (SCL) / GPP1-1 (SDA), as S-BOOT does: open drain (low = output 0, high = input), the SoC's pull-ups on while it runs, ~50 kHz; CON/DAT/PUD saved and put back. Menu 7 reads each S2DOS05 register twice; menu 8 as test 18 |
| 18 | `touchpower`: GPP1 CON ...11 (pins 0/1 GPIO outputs), DAT 0xff, USI mode register 0, hsi2c4 all 0 with USI_CON 1 (in reset): S-BOOT reaches the S2DOS05 by driving GPP1-0/1 itself. `touchtest` stopped before any write (the controller in reset) |
| 18 (built) | `E:\s20-g981b\hakios-x1s-boot-test18.img` (U-Boot run 43128514154, sha256 c053a05e...765370): menu 7 `touchpower` (read only: the display power chip's bus, S2DOS05 ID/EN/LDO2/LDO3), menu 8 `touchtest` with the supplies switched on first: **S2DOS05 (I2C 0x60 on hsi2c4 0x10590000) EN 0x03 \|= 0x06**, only if LDO2_CFG 0x05 and LDO3_CFG 0x06 already read 0x0C (1.8 V / 3.0 V), and EN back as it was at the end. Owner's OK needed (a power-chip write) |
| 17 | `touchcheck` went through: the I2C block answers and takes its timing. `touchtest`: the chip did not answer (-19, no ACK) to 0x55 or 0x22; its bus pins had read low (pull-ups on tsp_io, which S-BOOT leaves off) |
| 17 (built) | `E:\s20-g981b\hakios-x1s-boot-test17.img` (U-Boot run 43127174405, sha256 88542faa...59f056): menu 7 `touchcheck` again (now reaches the I2C block), menu 8 `touchtest` (its writes: GPP5 CON 0x10730000 bits 24-31 = 0x33; GPP5 PUD 0x10730008 bits 24-31 = 0; GPA1 PUD 0x15850028 bits 0-3 = 0; the I2C controller at 0x108D0000; the touch chip's commands 0x62, 0x10, 0x11. No clock, power chip or storage) |
(`drivers/input/hakios.sec_ts.c`, boot-arg `sec_ts=1`) posting to `hakios_touch_post()` as the Goodix driver does.

Package: `x1s-hello.hkub` (`.github/workflows/x1s-kernel.yml`), sent with `fastboot stage` + `fastboot continue` from the menu's
"HakiOS over USB". Arguments `-v cpus=1 loader_return_after=30`.

The full power-off sequence on the S20: power key released, PMIC RTC 0x01 bits 6+7 and 0x03 bit 3 cleared (ACPM channel 2),
ACPM STOP (channel 4), PSCI SYSTEM_OFF (returns), PS_HOLD low (PMU 0x1586030C bit 8).

Earlier notes from these tests: the menu's Power off (PSCI SYSTEM_OFF) not yet seen to work; after a menu item U-Boot stayed at
the prompt (fixed in b1b7f82, not yet on the phone).

### First phone test (done: see above)

What gets written: **only the boot partition**, by the owner in TWRP, with `E:\s20-g981b\hakios-x1s-boot-test1.img`
(U-Boot only, from CI run 42926541009; an empty ramdisk; sha256 3ad74a63...7433d7b; 1,062,912 bytes).
What U-Boot writes to the hardware, all of it:

| Register | Value | Why |
|---|---|---|
| DECON_F 0x19050070 (HW_SW_TRIG_CONTROL) | 0x1281 | turn the screen's refresh back on (same as barebox, uniLoader, Linux) |
| Pin config of GPA0-3, GPA0-4, GPA2-4 (0x15850000 + bank) | function EINT, no pull, drive LV1 | the volume and power keys; the same setting Android uses |
| USB, only if "HakiOS over USB" is chosen: PMU 0x1586072C bit 0, USB PHY 0x10C00000 | PHY on, Exynos850 init + Exynos990 tuning | the USB cable link (fastboot) |
| PSCI SYSTEM_OFF (SMC 0x84000008) | - | power off: menu, or heat guard after 10 minutes |

Nothing touches storage (no flashing, no UFS/MMC driver) or the PMIC (no PMIC/regulator drivers).
Expected: U-Boot's text in big letters on the screen and its menu, moved with Volume Up/Down, chosen with Power.
If the screen stays black or the phone restarts: hold Volume Down + Power about 10 s, then Volume Up + Power for TWRP,
and flash the backup `boot.img`. Short test, lowest brightness the bootloader allows.

## Milestones

1. **Text on the S20 screen.**
   - Framebuffer: 0xF1000000, 1440x3200, a8r8g8b8 (see above). Still to check on the phone, read only: that the DECON window really points there (`/proc/iomem` on ColorOS, or the DECON window registers).
   - HakiOS's U-Boot on x1s (Plan A2, see above), then the loader. XNU's boot path writes text straight into that framebuffer.
   - GICv2 driver (next to `gic_v3_irq.c`), and the Exynos MCT / arch timer.
   - TMU reading and the 70 C cutoff (see safety rules) **before** starting more than one core.
   - Watchdog: see whether the bootloader left it on. If it did, pet it (or turn it off) so the phone does not reset by itself.
   - Done when: the kernel's log shows on the screen and the phone powers off by itself after a set time.
2. **iOS from storage.** No USB loader here, so the UFS driver comes early. The UFSHCI part may be shared with the A71's; the Samsung PHY/vendor setup is new (the bootloader has already set up the PHY, which may be enough to start). Same "iOS image at the start of userdata" idea as the A71 (`FLASHBOOT.md`). This step **wipes ColorOS's userdata**, so it needs its own approval.
3. **Touch, buttons, battery.** New chips; addresses from the device tree.
4. **USB.** DWC3 (shared core) with a Samsung PHY. Gives a cable link for logs, like the A71's.
5. **Wi-Fi.** Broadcom BCM4375 (the G986N-iOS project uses brcmfmac glue on its Broadcom phone, worth a look).
6. **GPU.** Mali-G77: software drawing for a long time.

## Open questions

- Framebuffer: known from mainline and uniLoader (0xF1000000, 1440x3200, stride 5760, a8r8g8b8); not yet checked on this phone.
- Where S-BOOT loads the boot.img kernel, and whether it checks the kernel's format.
- Does the bootloader start XNU-sized images from boot (boot partition is 59 MB)? Is there a kernel size limit?
- Is a watchdog running at hand-off?
- Does Samsung's hypervisor (`uh` partition, S2MPU) get in the way of a non-Linux kernel at EL1?
- Exact TMU register for current temperature on exynos-tmu-v2.

## GPU (Mali-G77): what it takes (12 Nov)

The owner, after touch worked: "very laggy" without a GPU; asked for at least partial GPU acceleration. The honest size:
weeks to months, in steps, each a phone test. What there is to build on:

| Part | What it is on the S20 | State elsewhere |
|---|---|---|
| GPU | Mali-G77 (Valhall v9, job-manager: no firmware to load), registers at 0x18500000 (0x5000), interrupts JOB 0x12d, MMU 0x12e, GPU 0x12c (SPI numbers) | Mesa's Panfrost lists the G57 and G68 (Valhall v9, the same generation) with conformant OpenGL ES 3.1; the G77 is **not listed**: likely a product ID and small differences, not certain |
| power | domain `pd-embedded_g3d`, PMU 0x15862200 (0x20), Samsung's `exynos-pd` with `need_smc = 0x18410204` (a secure-world call around the switch); status at PMU + 0x2204 bit 0 (`gpu_pmu_status_reg_offset`) | Samsung's kernel: drivers/soc/samsung/exynos-pd.c, cal-if |
| clock | through the APM (ACPM DVFS, `g3d_cmu_cal_id` 0xb04000a), 156 to 800 MHz (`gpu_min_clock` 0x26160 kHz) | the same ACPM queues HakiOS already uses here (TMU, PMIC, STOP) |
| heat | its own TMU zone, G3D: zone 3 (thermal-zones BIG, MID, LITTLE, **G3D**, ISP, NPU) | the heat guard reads zones 0-2 today: zone 3 must be in it before the GPU ever runs |
| HakiOS | `drivers/gpu/hakios.gpu.cpp` (the GPU service, `HakiosGPU`) with `hakios.adreno.c` behind it; Mesa (`tools/mk-mesa.sh`, softpipe + freedreno) through `tools/mesa/hakios-drm` in place of libdrm; CoreAnimation by OpenGL ES (`userland/src/eagl.c`, GL=1) | all of it Adreno-shaped: a Mali back end, Panfrost in the Mesa build and Panfrost's ioctls in hakios-drm are new |

Steps (each with the owner's OK where it writes the hardware; the GPU's clock at its minimum, short runs):
1. **G1, read only (U-Boot):** the G3D power domain's state, CMU_G3D, the G3D temperature (zone 3); the Mali's GPU_ID only if the domain is on (reading it off would abort).
2. **G2:** the domain on the way Samsung's kernel switches it on, the clock at 156 MHz through the APM; GPU_ID, shader/tiler/L2 present masks; then off again. The heat guard with zone 3 first.
3. **G3, the kernel:** a Mali back end for HakiosGPU: soft reset, cores and L2 on, one address space of the GPU's own MMU, one job (a write-value job) run and seen done. The GPU's first work.
4. **G4, Mesa:** Panfrost built in (`DRIVERS=softpipe,panfrost`), the G77's product ID if Mesa lacks it, Panfrost's ioctls in hakios-drm; `gl_test`'s triangle drawn by the GPU. This is the first "partial acceleration".
5. **G5:** CoreAnimation by OpenGL ES on the Mali (GL=1, gpu=1): the home screen drawn by the GPU.

Meanwhile the cheaper win: all six cores (runs 22-24 ran on one; `oem cores`, 9a58137, finds out why).

### G3: the kernel's Mali steps (`drivers/gpu/hakios.mali.c`, boot-arg `mali=N`, commit a7a0447)

`mali=1` only reads. `mali=2..5` write the GPU's own registers (Mali at 0x18500000) and nothing else: not the PMU, not
the clocks, not the APM, not the power chip. The G3D power domain must already be on (it is, after S-BOOT); the GPU's
clock stays as S-BOOT left it. Every write, in order (offsets from 0x18500000):

| Step | Register (offset) | Value | What it does |
|---|---|---|---|
| 2 reset | GPU_IRQ_MASK (0x028), JOB_IRQ_MASK (0x1008), MMU_IRQ_MASK (0x2008) | 0 | no interrupts from the GPU (nothing here takes them) |
| | GPU_IRQ_CLEAR (0x024) | 0xffffffff | forget old events |
| | GPU_COMMAND (0x030) | 1 | soft reset; wait for "reset completed" (bit 8 of 0x020), 100 ms at most |
| | GPU_IRQ_CLEAR, JOB_IRQ_CLEAR (0x1004), MMU_IRQ_CLEAR (0x2004); the three masks again | 0xffffffff; 0 | |
| 3 power | (first: G3D temperature read; nothing at or above 50 C) | | |
| | L2_PWRON_LO (0x1a0) | L2_PRESENT (read in step 1; 1 expected) | L2 cache on; wait L2_READY (0x160), 100 ms |
| | TILER_PWRON_LO (0x190) | TILER_PRESENT (1 expected) | tiler on; wait TILER_READY (0x150) |
| | SHADER_PWRON_LO (0x180) | SHADER_PRESENT = 0x670077 (11 cores) | shader cores on; wait SHADER_READY (0x140); temperature read again |
| 4 MMU | AS0_TRANSCFG_LO/HI (0x2430/0x2434) | 0x01000006 / 0 | AArch64 4K tables, walked uncached |
| | AS0_TRANSTAB_LO/HI (0x2400/0x2404) | our table page's address (16K of kernel memory, found at run time) | |
| | AS0_MEMATTR_LO/HI (0x2408/0x240c) | 0x4c4c4c4c / 0x4c4c4c4c | all GPU memory uncached |
| | AS0_COMMAND (0x2418) | 1 | take the new tables; wait AS0_STATUS (0x2428) bit 0 clear |
| | MMU_IRQ_CLEAR (0x2004) | 0xffffffff | |
| 5 job | JOB_IRQ_CLEAR, MMU_IRQ_CLEAR | 0xffffffff | (temperature read first) |
| | JS1_HEAD_NEXT_LO/HI (0x18c0/0x18c4) | 0x01000000 / 0 | the job, at GPU address 16 MB (our page) |
| | JS1_AFFINITY_NEXT_LO/HI (0x18d0/0x18d4) | 0x670077 / 0 | any shader core |
| | JS1_CONFIG_NEXT (0x18d8) | 0x83300 | AS 0, caches cleaned before and after, priority 8 |
| | JS1_COMMAND_NEXT (0x18e0) | 1 | start; wait up to 100 ms for "done" (bit 1 of 0x1000) or "failed" (bit 17) |
| | (only if not done) JS1_COMMAND (0x18a0) | 3 | hard stop |
| end | SHADER_PWROFF_LO (0x1c0), TILER_PWROFF_LO (0x1d0), L2_PWROFF_LO (0x1e0) | what is ready | everything off again, in that order |
| | then (after step 4 or a failure) the soft reset of step 2 again | | nothing of ours left in the GPU's hands |

**Run 31 (12 Nov 16:48, kernel run 43261382902, `mali=1`, read only):** in iOS's kernel the G3D domain is on
(configuration 1, status 1), PLL_G3D on at 260 MHz (M 160 P 4 S 2 from 26 MHz: Samsung's `gpu_dvfs_start_clock`), mux
on the PLL; G3D 27 C. GPU_ID 0x90800011, GPU_STATUS 0, L2_FEATURES 0x07120206, TILER_FEATURES 0x809, MEM_FEATURES
0x301, MMU_FEATURES 0x2830 (48 address bits in, 40 physical), shaders 0x670077, tiler 1, L2 1, address spaces 0xff, job
slots 7; nothing ready (all powered down), coherency none, raw GPU interrupt 0x100 (an old "reset completed"). iOS
came up as in run 30; log `logs/164821-run.log`.

**Run 32 (`mali=5`, owner's OK):** "reset done", but the L2 never came on (not even "in transition"); steps 4-5 not
reached; 29 C. **Run 33** (more reads): a read of CMU_G3D +0x83c (named in the device tree) is a synchronous external
abort -> kernel panic, back to the loader; removed. The APM's DVFS channel (5) did not take a FREQ_GET (queue full,
EBUSY). **Run 34:** writing GPU_IRQ_CLEAR does not clear the old RESET_COMPLETED (0x100): **no write of ours reaches
the GPU**, reads all answer. The "reset" of runs 32-34 was S-BOOT's old flag.

Why, most likely: Samsung's `pmucal_local_enable()` (cal-if/pmucal_local.c) runs the domain's power-on sequence and
then, for a domain with `need_smc` (pd-embedded_g3d: 0x18410204), `exynos_pd_tz_restore(need_smc)` =
`exynos_smc(0x82000410 SMC_CMD_PREAPRE_PD_ONOFF, 1 EXYNOS_WAKEUP_PD_DOWN, 0x18410204, 2 RUNTIME_PM_TZPC_GROUP)`
(exynos-el3_mon.c, include/linux/smc.h): the block's TZPC (who may write it) restored by EL3. S-BOOT leaves the domain
on, apparently without that. Commit "a write test, and Samsung's TZPC restore" (kernel run 43298942600): step 2 tests a
write first (AS0_TRANSTAB_LO 0x5a5a5000, read back, then 0) and only with `mali_tz=1` makes that one SMC, then tests
again. The SMC is a secure-world call: owner's OK first. **Run 35 (`mali=2`, no SMC):** the write test reads 0 back for 0x5a5a5000: proven, writes do not reach the GPU. 29 C. **Run 36 (owner OK, mali_tz=1):** the SMC answered 0 (done), but the write test still reads 0: TZPC was not (all of) it. TWRP's Linux (same firmware) probes the Mali ("GPU identified as 0x0 arch 9.0.8 r0p1", "Probed as mali0", kbase r38p1), so writes do land under Linux. **Run 37 (read only):** APM channel id 5 (acpm_dvfs in the DT) is TYPE_BUFFER (2), q_len 1, owner 4; the TMU's id 9 is TYPE_QUEUE, q_len 7. Samsung's acpm_ipc.c indexes channel[] by place in the table, not by id, and its queue code could never post to a one-slot queue: so Samsung's DVFS channel is probably another entry. **Run 38** (list the table by place): the phone's USB link hung, no log; force restart needed. Main suspect still: PLL_G3D "on, not locked" in every run = the GPU's core clock stopped; Linux sets it through the APM (FREQ_REQ) before its probe.

**Runs 39-41.** Run 39: listing the APM table with each channel's queue indices froze the bus (watchdog reset; USB
stuck, force restart). Run 40 (the table's own words only): places = ids; 11 channels; 5 (acpm_dvfs) is TYPE_BUFFER,
q_len 1, interrupt-driven. Samsung's G3D power-on, verbatim (cal-if/exynos9830/flexpmu_cal_local_exynos9830.h,
`embedded_g3d_on[]`): PLL_CON1_PLL_G3D (0x1a330104) = 0x30000003; PLL_CON0_PLL_G3D (0x1a330100) bit 4 = 1; CMU_G3D
PLL_CON0_MUX_CLKCMU_EMBEDDED_G3D_USER (0x18400600) bit 4 = 1; EMBEDDED_G3D_OUT (0x15862220) |= 2; delay; CONFIGURATION
= 1; wait STATUS. Its power-off ends with PLL_CON1_PLL_G3D = 0x10000001. **Run 41 (read only): PLL_CON1_PLL_G3D =
0x10000001, EMBEDDED_G3D_OUT = 0x24 (bit 1 clear): S-BOOT switches the domain on without starting the GPU's clock.**

Proposed (`mali_clk=1`, commit "the GPU's clock started as Samsung's G3D power-on does", kernel run 43330014281), only
when the write test fails, in this order: 0x1a330104 = 0x30000003; 0x1a330100 |= 0x10 (already set: skipped); wait up
to 10 ms for PLL_CON3 (0x1a33010c) bit 29 (locked); read 0x18400600 and set bit 4 if clear; 0x15862220 = old | 2
(0x26); write test again. At the end, in Samsung's power-off order: 0x15862220, 0x18400600, 0x1a330104 back to what
was read. The PLL's frequency (PLL_CON3, 260 MHz) is not changed.

**Run 42 (`mali_clk=1`, owner OK):** after PLL_CON1 = 0x30000003 the PLL locks (PLL_CON3 0x80a00402 -> 0xa0a00402;
PLL_CON1 reads back 0x10000001 whatever is written); CMU_G3D +0x600 reads 0x10 (already the PLL); the plain write to
EMBEDDED_G3D_OUT does nothing (Samsung sets it through the PMU's atomic alias, and its bit 2 is already set). Writes
still do not reach the GPU. **Run 43 (`mali_clk=1 mali_tz=1`):** the same, after the TZPC restore too. 29 C.

**TWRP's Linux (12 Nov 23:50), `mali0/regs_history`:** `echo on > /sys/devices/platform/18500000.mali/power/control`
(the domain had been runtime-suspended at 3 s: Samsung's off sequence with the TZPC save), then `echo 1 >
/sys/kernel/debug/mali0/reset`: kbase writes SHADER_PWRON 0x670077, PWRTRANS reads 0x670077, then SHADER_READY
0x670077; L2_READY 1, TILER_READY 1; after its reset GPU_IRQ_RAWSTAT 0x200; SHADER_CONFIG 0x20000000, JM_CONFIG
0x000f0000 (kbase's quirks for this GPU). **Under Linux, after Samsung's full off -> on cycle, the GPU takes writes.**

Proposed (`mali_cycle=1`, commit 7546978, kernel run 43332285358): Samsung's off sequence (TZPC save SMC 0x82000410
(0, 0x18410204, 2); PMU 0x15862200 bit 0 = 0, wait 0x15862204 bit 0 = 0; 0x18400600 bit 4 = 0; 0x1a330100 bit 4 = 0;
0x1a330104 = 0x10000001), 1 ms, then its on sequence (0x1a330104 = 0x30000003; 0x1a330100 bit 4 = 1; wait for the
lock; 0x18400600 bit 4 = 1; 0x1586e220 = 2 (PMU atomic set of EMBEDDED_G3D_OUT bit 2); 280 us; 0x15862200 bit 0 = 1,
wait 0x15862204 bit 0 = 1; TZPC restore SMC (1, 0x18410204, 2)); write test; steps 2-5. At the end the off sequence
again: the GPU's domain left off.

**Runs 44-53 (13 Nov, 00:00-01:00).** Run 44 (`mali_cycle=1`, owner OK): the kernel's write of CONFIGURATION = 0 is
lost too (the domain stays on); nothing changed. **From U-Boot** (`fastboot oem "run:mw.l 0x18502400 0x5a5a5000"`,
then `itest.l`), also after 90 s, **from the loader** (new `oem poke`/`oem peek`, answer in `getvar:peek`), before
and after a kernel run, and with a kernel-style TTBR1 set up in the loader (`oem ttbr1poke`): the write reads back.
**From the kernel it never does**: at EARLY_BOOT already (run 50), with cpus=1 (45), through ml_io_map (Device-nGnRnE,
AttrIndx 3) and ml_io_map_wcomb (Normal-NC), both mapping PA 0x18500000 with AP 0 (run 48), with str/str x/stlr/stp
(51). A kernel read takes ~300 ns, a kernel write ~10 ns in a loop. Kernel: EL1, SCTLR 0x3454593d, TCR 0x22a51a251a,
MAIR 0x0c08044f00bb44ff, MPIDR 0x81000000; loader: EL1, SCTLR 0xc5183d, TCR 0x18080351c, MAIR 0xff440c0400. Open:
what of the kernel's makes its stores to G3D/PMU-G3D/CMU (but not to the ACPM mailbox, I2C, PS_HOLD, GIC) vanish.
Ideas not tried: a write from the kernel through a TTBR0 (low) identity mapping; the loader writing with the kernel's
exact SCTLR/MAIR; the hypervisor's view (H-Arx) of the kernel (a Samsung kernel registers with it: RKP/uH calls).

**Runs 54-56 and loader tests (13 Nov 01:00-01:45).** Ruled out, the loader's write landing every time with: a
kernel-style TTBR1 table and a write through a high VA (`oem hipoke`), the kernel's SCTLR (`oem sctlrpoke 3454593d`),
the MMU off (`oem offpoke`), CONTEXTIDR and TTBR0's ASID set (`oem ctxpoke`), the kernel's MAIR with its device index 3
(`oem hikpoke`). Timing (`oem timepoke`, CNTVCT at 26 MHz): from the loader every device write ~400 ns (the GPU's and
the GIC's alike), reads 300-390 ns. Run 54: a marker the kernel wrote into AS 1 is not there for the loader after the
run (the kernel's writes do not land; its reads are right). Run 55: the kernel maps the GIC and the GPU the same way
(AttrIndx 3, AP 0); its GIC writes work. The kernel makes no hypervisor call before the test with cpus=1 (PSCI only).
Nothing published on the Exynos 990's hypervisor and a non-Samsung kernel. Not yet tried: from the kernel, the write in
the loader's whole context (its TTBR0/TCR/MAIR at once), and whether the kernel's writes to other pages of the PMU and
CMU_TOP land (to map which pages are refused).

Also checked (13 Nov ~02:00): XNU's BCM2837 core-timer write to 0x4000004X is already compiled out
(`BCM2837_CORE_TIMER_ROUTING`); the loader making it (`oem offwrite 40000040 f0`) hangs the phone (force restart): it
must stay out. XNU's PI3 mini-UART init needs DT nodes "gpio"/"aux", which the loader does not make. Odd: in run 55 the
kernel's own `mrs cntvct_el0` gave deltas 0, 0, 0 and then 269500416 across four loops (mach_absolute_time looked
right in run 48): the virtual counter as the kernel sees it may be frozen or emulated. Next: (1) that counter; (2) a
tiny test payload in place of XNU (MMU on like XNU, then the GPU write), bisecting XNU's early steps; (3) the kernel's
writes to other pages, to map which ones are refused.

The job is a WRITE_VALUE job (Mesa's v9 "Job Header" + "Write Value Job Payload"): it writes 0x6a0b77 at GPU address
0x1001000; the kernel then looks for that number in its own memory. The heat guard (exynos_thermal=1, needed) also
has a GPU limit now: G3D at `exynos_thermal_gpu_stop` (60 C) twice in a row -> GPU cores off, phone powered off.

### G3 DONE (13 Nov 10:50, run 70): THE MALI-G77 RAN A JOB IN iOS'S KERNEL

**Cause of the lost writes (runs 31-69):** Samsung's hypervisor (H-Arx, EL2) handles the kernel's stores to the GPU
(0x18500000), the G3D power-domain page (PMU 0x15862000) and the clock pages in software, and drops the ones made
through the kernel's own TTBR1 mappings (reads come back right; U-Boot's, the loader's and the kernel's MMU-off stores
land). Bisect: start.s markers with the MMU off landed (run 57), checkpoint 17 on not (58); the loader writes fine with
the kernel's whole context (TCR, MAIR, SCTLR, PAN, the kernel's own TTBR1 tables, the kernel's VA: runs 59-64); through a
table of our own in TTBR0 the kernel's stores land every time (60-63). **Fix (commits 96e6bdf, d269d77):** every
GPU/PMU-G3D/CMU access of `hakios.mali.c` goes through a private lower-half window (VA 0x40000000, 4K Device-nGnRnE
pages), with interrupts off, TTBR0 switched and put back and the TLB emptied around each access.

**Run 70 (`cpus=1 exynos_thermal=1 mali=5`, kernel 43407239922):** write test passes; soft reset (RESET_COMPLETED);
L2 ready 1, tiler 1, shaders 0x670077 (all 11); AS 0 set up (tables 0x9295b0000, transcfg 0x1000006, memattr 0x4c4c4c4c);
the WRITE_VALUE job on slot 1: JOB_IRQ_RAWSTAT 0x2 (slot 1 done) after 48 us, slot status 0, no MMU or GPU fault, job
status word 1, **0x6a0b77 at GPU 0x1001000, written by the GPU**; all powered off again; 27 C. No mali_clk, mali_tz or
mali_cycle needed: the domain and clock as S-BOOT leaves them do. Next: G4 (Mesa Panfrost for the G77 + Panfrost ioctls
in hakios-drm + HakiosGPU on the Mali), and the window made cheaper (an ASID of its own instead of TLB flushes).

### G4 under way (13 Nov afternoon)

- **Mesa** (`tools/mesa/zz-panfrost-darwin.patch`, CI `.github/workflows/x1s-mesa.yml`): Panfrost built for Darwin with
  the G77 (`MODEL(0x9080, 0, "G77", "TTRx", HAS_ANISO, 16384, {})`), the libdrm stand-in (tools/mesa/hakios-drm) for
  Panfrost, buffers mapped as HakiosGPU client memory, OSMesa opening "panfrost". Builds (run 43414651516); the CI also
  compiles our user-space programs with GL=1 against it (artifact x1s-userland-gl).
- **Kernel** (commit a250477): HakiosGPU with a backend table (Adreno or Mali); in Mali mode Panfrost's requests
  (SUBMIT, WAIT_BO, CREATE_BO, MMAP_BO, GET_PARAM, GET_BO_OFFSET, MADVISE, SET_LABEL_BO); `mali_keep=1` keeps the GPU on:
  AS0 with four-level tables over 1 GB of GPU addresses, jobs one at a time, state polled.
- **Run 71 (`mali=3 mali_keep=1`, rc.jobs.malitest): `mali_test` from user space: GET_PARAM (product 0x9080, rev 0x11,
  shaders 0x670077, MMU 0x2830, max threads 1024, TLS 0), CREATE_BO, the buffer mapped in the process, 5 WRITE_VALUE
  jobs submitted and waited for: 5 of 5 done by the GPU, ~110 us each.**
- **Run 76 (13 Nov 12:01): G4 DONE: `gl_test`'s triangle drawn by the Mali-G77 through Mesa's Panfrost:
  "Mesa; Mali-G77 (Panfrost); OpenGL ES 3.1 Mesa 25.0.7", 1080 x 2400 drawn in 37 ms, read back in 31 ms, error 0,
  red/green/blue corners right, 26% of the pixels inside the triangle (26% by geometry), on the panel 8 s.** The last fix:
  OSMesa gave Panfrost's screen a NULL config (run 75 crashed in panfrost_create_screen reading config->options);
  now its driconf options as freedreno's (commit cd03f58). Image: `build-images.sh <x1s-userland-gl> SYSTEM=1 GL=test
  RC_JOBS=rc.jobs.gltest`, boot-args `mali=3 mali_keep=1`. Next: G5, CoreAnimation by OpenGL ES on the Mali (GL=1).
- Runs 72/74 (GL=test, rc.jobs.gltest): `gl_test` with the Panfrost Mesa dies with signal 11 before saying anything;
  gl_test now reports where it crashes (commit 01a2d83), the CI keeps the unstripped libOSMesa for the offsets.

### G5 started (13 Nov 13:20-13:40)

- **Run 77 (GL=1 image, `mali=3 mali_keep=1`): CoreAnimation draws through Mesa on the Mali** ("an OpenGL ES 3 context for
  CoreAnimation ... OpenGL ES 3.1 Mesa 25.0.7; Mali-G77 (Panfrost)", "the server has an accelerated renderer"). Its
  first job never finished, every later one was refused, frames 5-10 s. Fix (commit 4144969): a job running 2 s is
  reported, hard-stopped, the GPU reset, powered and AS0 given back (`k_recover()`).
- **Run 78 (kernel 43433303775): no stuck job; 71 frames drawn by the GPU, median 37 ms a frame, of which 28 ms is
  copying the picture into the display's IOSurface ("copied, not shared: the library refused": osmesa_surface_resource()
  only knows freedreno). 30 C.** Next: draw in place into IOSurfaces with Panfrost (kHakiosSurface in the kernel works in
  Panfrost mode; Mesa: osmesa_surface_resource for GALLIUM_PANFROST, panfrost_kmod's import without lseek()).
- **Run 79 (13 Nov 13:45; Mesa 43435136841, commit a23eb06): CoreAnimation draws IN PLACE into the display's IOSurfaces
  on the Mali: 74 frames, all drawn in place, median 6.5 ms a frame (was 37 ms), no copies, no stuck job.** Left: small
  IOSurface textures (e.g. 194 x 237, rows not 64-byte aligned) are still copied ("the library refused"); the first frames
  spend 30-50 ms refilling textures from IOSurfaces; the register window flushes the TLB twice per access; jobs one at a
  time with polling; GPU interrupts not used.
- **Runs 80-84 (13 Nov afternoon): the frame onto the panel.** The display is an iPhone's 1125 x 2436, the panel
  1440 x 3200: MiniIOMobileFramebuffer stretched every frame on the CPU, **26 ms a frame** (bilinear, 3 threads) --
  the real limit after run 79, not the GPU. Run 80 (`fbwidth=1440 fbheight=3200`): 3 ms a frame, up to 31 frames a
  second, but iOS laid out for 480 x 1067 points (everything small): not kept. Run 81 (`fb_threads=6`): 8-14 ms, 30 a
  second moving. Run 82: loads through a TTBR1 mapping of the GPU's power-domain page come back as their own address
  (H-Arx skips them as it drops the stores): the window for loads too, but each access empties one TLB entry
  (`tlbi vaae1`) instead of the whole TLB twice (commit after 64e2039). Runs 83-84 (**fb_gpuscale**, default on):
  eagl.c blits each frame for the panel into a panel-sized IOSurface (`HakiosPanelTwinOf`), bilinear, on the GPU;
  the driver copies that as it is: **3 ms a frame** (run 84), normal sizes. A Panfrost submission now marks the
  program's surfaces drawn (the display waits for it). The image with `REST=1`: **wallpaper (PosterBoard) and
  widgets back** (both blank since 12 Nov were the missing launch angels, not the GPU); home screen ~3 min after
  start, so runs of 6 min. Run 84: 35 C at most, up to 27 frames a second -- now the CPU (S-BOOT's clocks, 6 cores) is
  what limits. Next: the Mongoose cores (`mongoose=1 cpus=8`, loader + Affinity2 in XNU's core numbers), then
  GPU interrupts and overlapping jobs.
- **Run 85 (13 Nov 16:40; kernel 43462151461, loader rebuilt with `LOADER_CFLAGS=-DXNULOADER_USB_PID=0x4ee0` -- without
  it the loader comes up under a USB ID Windows has no driver for, two force restarts): `cpus=8 mongoose=1`: **all 8
  cores online, the Mongooses 0x10000/0x10100 as cpu 6 and 7** (GIC interface masks 0x40, 0x80). Up to 48 frames a
  second (27 with six), half the frames within 19 ms; 38 C at most. Still S-BOOT's clocks on every core.
- **Run 86** (`cpu_mhz=1`, interrupts off for 20 M additions per core): the phone froze (owner was using it; no log).
  **Run 87** (interrupts on, fastest of 20 bursts of 100,000 additions): **S-BOOT's clocks: A55s ~1.05 GHz (max 2.0),
  A76s ~1.07 GHz (max 2.6), Mongooses ~0.27 GHz (max 2.73).** 39 C. A capped speed-up (A76 ~1.8, M5 ~1.5 GHz) would
  make the window server's work ~1.7x faster: est. mostly 55-60 frames a second. CPU DVFS goes through the APM (ACPM,
  cal-ids 0xb040002-4 in Samsung's tree): needs the owner's OK with the exact message first.
- **Run 88:** the one-place DVFS channel (5) answers now (it was treated as a full ring); FREQ_GET for CPU domains
  answers 0xffff (Linux never asks; it reads the CMU). **Run 89 (owner OK; `cpu_a76=1800000 cpu_a55=1400000`,
  `exynos_thermal_stop=60`): the APM accepted both FREQ_REQs (answer 0); cpu 0 then measured 1299 MHz (was 1052).
  But the measurement stopped after cpu 0 (no line for cpus 1-7), the owner saw no wallpaper and "extremely laggy";
  GPU on, 34 C max.** Suspect: the cluster change stalled the other cores. Next: A76 only (`cpu_a76=` alone), and
  measure before/after on each core; look at the full log E:\s20-g981b\logsun89.log.
- **Run 90 (`cpu_a76=1800000` alone): the same -- APM answer 0, cpu 0 measured 1299 MHz, then nothing from cpus 1-7,
  10 frames a second. So any FREQ_REQ (not the A55 one) leaves the other cores stalled. Do not use cpu_a76/cpu_a55 until
  that is understood (Samsung's DM/HIU handshake? the APM's DVFS plugin not initialised -- Linux sends
  exynos_acpm_set_init_freq, {id, kHz, DATA_INIT, SET_INIT_FREQ=3}, at boot). Default boot-args leave clocks alone.
- **Runs 108-109 and the display (13 Nov 23:30-23:55).** Panfrost SUBMIT/WAIT_BO by IOKit trap
  (HakiosGPUUserClient::getTargetAndTrapForIndex; hakios-drm IOConnectTrap1 declared as _IOConnectTrap1, the SDK
  marks it unavailable): **run 109: 46-52 frames a second while swiping, heavy frames 2.6-26 ms, 95% within 35-37 ms**;
  kernel calls fell from ~20% to ~6% of the drawing thread. Run 108 with `dpu_probe=1` (DECON/IDMA reads from the
  kernel at mali init) FROZE the phone: do not use it. From the loader (`fastboot oem peek A` / `getvar peek`), read
  only, all fine: DECON0 0x19050000 GLOBAL_CONTROL 0x133, HW_SW_TRIG_CONTROL 0x1001, DATA_PATH_CONTROL_0 0x100000
  (window 5 on), _1 0 (window 5 on channel 0), _2 0xb1, WIN_CONTROL_0(5) 0x102, WIN_END(5) 0x0c7f059f (1439,3199);
  IDMA at 0x19041000: ENABLE 5, IMG_SIZE 0x0c8005a0 (1440x3200), SRC_SIZE/IN_BASE_ADDR_Y8 (+0x40) read 0, also at
  +0x800 (shadow); channels 0x19042000-46000 ENABLE 1, unconfigured. With the owner's OK, from the loader: IN_BASE_ADDR_Y8
  = 0xF1100000 then SHADOW_REG_UPDATE_REQ (0x19050060) = 0x80000020: **no visible shift** (the base reads back 0; the
  update request reads 0 at once): the writes most likely dropped by H-Arx, as the GPU's were. Next: the same flip from
  the kernel through the TTBR0 window (as hakios.mali.c), then fb_zc (Scion's hakios.exynos_dpu.c as a model).
- **Runs 103-107 (13 Nov 20:25-21:40): the swipe lag, by glprof.** Heavy CoreAnimation frames (300-900 GL calls,
  up to 15 render passes) took 48-95 ms (run 100). Fixes, in order of effect: (1) eagl.c surface_changed() samples a
  sixth of a copied picture's memory (all of it every 32nd look) -- it was 40% of the drawing thread: 20-67 ms (run 104);
  (2) a Panfrost SUBMIT marks busy only the buffers it names (bo_handles), so Panfrost's BO cache works: **5-35 ms, up
  to 44 frames a second, 95% of frames within 34-40 ms (run 105)**; (3) cached BOs keep their CPU mapping (as the S9+
  port, danstronics/ios-s9plus-exynos HMC=1) and WAIT_BO of a never-submitted BO is answered in hakios-drm: run 107
  about the same (42 fps). Also: the Mali job queue (kernel thread, in-order) and in_syncs no longer waited on the CPU
  (run 103: no change by itself); gpu_khz=600000 (run 106, owner OK; not confirmed in the log: a 600 s run keeps only
  the log's end) changed nothing visible. Left in the profile: IOConnectCall for SUBMIT ~20% (the S9+ port uses a fast
  mach trap, hakios.mali_fast.h). References: Scion (scion-kernel, scion-starlte: Exynos 9810, fb_zc/fb_dpp display,
  cpufreq governor) and danstronics/ios-s9plus-exynos. GitHub Actions: ~1800 of 2000 free minutes used on 13 Nov;
  the "x1s" check workflow disabled by the owner; build only kernel/mesa, bundled.
- **Runs 96-102 (13 Nov 19:00-20:10): the clocks, and what else takes the CPU.** With a restart before each run, the
  APM requests work: run 97 `cpu_a76=2000000 cpu_a55=1800000` -> A55s 1.74 GHz, A76s 1.90 GHz (Mongooses rose to 1.07
  by themselves), 48-50 C; mostly 23-32 frames a second while moving, stretches at 60. Run 98 `cpu_m5=2000000`
  (Mongooses through the APM) FROZE the phone: the Mongooses' clock goes through Samsung's HIU (0x1D200000 + 0x2000,
  HIUTOPCTL1/2, SR0 requests, exynos-hiu.c) -- not done yet. `fb_beat=0` (run 99) changed little. `fb_fps=1` draws the
  frames a second on the panel. rc.jobs.s20bench / s20prof: scripted swipes (+ cputop). **Run 100-101: logd_helper
  takes 80-84% of a core all the time** (SpringBoard 37-43%, backboardd 15-18%); run 102 without it (mkjobs
  DENY_MORE=logd_helper) never left the Apple logo, so it is needed: find why it spins. WIFID=0 saves wifid's 11%.
  Screen recording: rec_secs=N rec_start=S (S after the first frame; the home screen and swipes come ~85-160 s after
  it) rec_step=6, `xl recording F.mp4` (ffmpeg is installed).
- **CORRECTION (run 95, 13 Nov 18:50): the "1 of 8 processors" was not the APM.** A second kernel in the same loader
  session (no restart between runs; xl warns "a kernel has already run in this session of the loader") never gets
  cores 1-7. Runs 89-93 and 95 all started that way; every good run (85, 87, 94) came right after a restart. The
  clock request itself may be fine. Always restart before a run. Run 96: fresh, with cpu_a76=1800000 cpu_a55=1400000.
- **Runs 91-94 (13 Nov 18:15-18:45): the real effect of a FREQ_REQ.** After run 89 every boot had **"1 of 8
  processors are running"** -- the other seven cores never started again, through loader restarts (runs 90-93,
  hence "extremely laggy" and no wallpaper), whatever the boot-args. `fastboot reboot` from the loader (a full restart)
  brought them back (run 94). So an APM clock request wedges the cores' start until a full restart; the frequency
  itself was fine (cpu 0 at 1.3 GHz). cpu_a76/cpu_a55 now need `cpu_dvfs_unsafe=1` too. Open: what Samsung's
  Linux does around cal_dfs_set_rate for CPU domains (exynos-acme.c set_freq: only the Mongooses' need-awake
  power-mode toggle) -- perhaps the APM powers the cluster down/up and our PSCI state no longer matches.

### Cores, 12 Nov afternoon (tests 25-27)

- Test 25 menu 9 (GPU, read only): G3D on, Mali-G77 r0p1 (GPU_ID 0x90800011), SHADER_PRESENT 0x670077 (11 cores), 21 C.
- The core probe: PSCI 1.1 by HVC and SMC; CPU_ON says 0 for cores 1-5, but AFFINITY_INFO says "off" at once and
  ever after, and no core runs a single instruction. Changing the entry (above 4 GB, below, at 0x80080000), the call
  (HVC/SMC) or the context id changes nothing. Test 27: the same from U-Boot's board_init, before anything else of
  ours runs: cores 1-3 the same ("0 1 1 0").
- TWRP's Linux started by S-BOOT: all 8 cores (plain PSCI by HVC, targets = Samsung's DT reg 1-5, 0x100, 0x101;
  each core's own MPIDR has its number in Affinity1: 0x100-0x500, Mongooses 0x10000/0x10100).
- **TWRP's Linux started by our U-Boot** (booti from fastboot, kernel at 0x8c080000, ramdisk 0x84000000 in /chosen,
  S-BOOT's DT; use `cp.q`, a 36 MB `cp.b` stalls USB): **5 cores: 0, 4, 5, 6, 7. Cores 1-3 (the A55s beside the
  boot core) fail, the A76s and Mongooses start.** So 4-5 can be started on this boot path; XNU missed them because
  its reset vector looked for cpu data by MPIDR 0x100.. while the tree said 1.. (fixed: the loader gives real MPIDRs,
  `/chosen/psci-target-shift` = 8). Cores 1-3 under our boot path: still open (something between S-BOOT and U-Boot's
  board_init).
- Run 28 (`cpumask=0x31`, kernel 43248105140): still 1 of 3 (4 and 5 "taken", never joined), 31 C.
- **Test 28 (U-Boot, read by `oem run:test ${x1s_...}`): a core sent to a start address below 4 GB runs (core 5 from
  0x8c080000 early, core 1 from there later); one sent above 4 GB does not and stays lost until the next restart
  (core 4 from U-Boot's heap, then from low RAM too). The firmware keeps only the low 32 bits of CPU_ON's entry.**
  That was the whole story: the loader (0xAF2A9...), XNU (high in RAM) and test 27's stub were all above 4 GB, and
  every later "low" try came after a high one had lost the core. TWRP's Linux via U-Boot missed 1-3 because test 27
  had sent them high first.
- Fix (commit "cores start from a page below 4 GB", kernel run 43254772557, U-Boot 43254772675 = test 29): the
  loader sets aside a page below 4 GB (`/chosen/psci-entry-page`), XNU writes `ldr x9, .+8; br x9; .quad <reset
  vector>` there and starts the cores at it. U-Boot no longer tries a core at boot.
- Run 29 (test 28 still in boot, cores 1 and 5 lost by the tests): `cpumask=0x2f` -> 3 of 5 (0, 2, 3).
- **Run 30 (test 29, fresh restart): `cpus=6` -> "6 processors running" (4 A55 + 2 A76), SpringBoard up, 180 s,
  32-34 C, no panic.** Log `E:\s20-g981b\logsun30.log`.

### Next phone session (prepared 12 Nov, the owner away)

1. TWRP: flash `E:\s20-g981b\hakios-x1s-boot-test25.img` (U-Boot run 43184089920, sha256 64d4a0fb...204fc5) to boot.
   New in it: menu 9 **GPU check (read only)** (step G1), `temp` names zones 3-5 G3D/ISP/NPU. Run menu 9, photo.
2. Menu 1 "HakiOS over USB", then: `./xl enter`, `fastboot oem cores`, `./xl log` (the core probe, loader 9a58137
   built here): does each of cores 1-5 run, and what MPIDR does it report?
3. Then, by what (2) says: fix the cores' numbering or the kernel's way in, and an iOS run on all six cores, with the
   kernel of run 43183913197 (`E:\s20-g981b\builds\kernel-43183913197\out\kernel-x1s`: the heat guard also watches the
   GPU's zone, G3D).

## iOS from the phone's own storage, Sileo, two menu entries (plan, 14 Nov; nothing written to the phone yet)

Goal: the owner picks **"Boot iOS from storage"** in our U-Boot menu and iOS starts from the phone's UFS, with Sileo,
without `xl ramdisk` over USB. Each phone write (rows marked **WRITE**) needs the owner's OK first.

### What exists today (checked 14 Nov)

- **No UFS driver anywhere on the S20 path yet.** Our U-Boot (v2026.07, `exynos-mobile`) has UFS glue for Qualcomm,
  MediaTek, Rockchip, Renesas, TI and AMD, none for Samsung Exynos; the x1s defconfig has no UFS at all.
- Our kernel's UFS code (`drivers/storage/hakios.ufs.c` + `hakios.ufshc.c`) is the POCO's kind: the **loader** reads
  and writes for the kernel through the firmware's block service (`ufs=2`, U-Boot's or UEFI's UFS driver underneath);
  `ufs_native` is read only and knows the Qualcomm controller only. Writes (`ufs_write=1`) go into one partition named
  `darwin` and nowhere else.
- Scion's S9 (`ufs=3 ufs_write=1 var_hfs=ufs_USERDATA_data`; local copy `E:\s20-work\scion\`) stands on Silicium's
  UEFI, which has a full Exynos 9810 UFS driver. Its kernel adds an Exynos own mode (`ufs_native_soc=exynos9810`,
  "takes the controller as UEFI left it", fences around every partition but USERDATA) and `system_apfs=ufs_SYSTEM`
  (the RAM-style system image mounted from a partition). Its `socs/exynos990` has the S20's UFS addresses
  (HCI 0x13100000, vendor 0x13101100, UniPro 0x13180000, PHY 0x13104000), marked "not booted".
- Sileo: `tools/mk-jb.sh` (Procursus rootless bootstrap + Sileo 2.5.1) and `userland/build.sh` put it in the system
  image as `/jb`; `init` grafts it onto `/private/var/jb`. Never built for the S20: mk-jb.sh needs `zstd`, `ar` and a
  tar that keeps links and set-user-ID bits, which Git Bash on Windows does not have.

### The partitions (Samsung's PIT for G981B, `PICASSO3_EUR_OPEN.pit` from the CSC tar; LUN 0, 4 KB blocks)

| Partition | Start block | Size | Role in this plan |
|---|---|---|---|
| EFS, SEC_EFS, PARAM, UP_PARAM, BOTA, KEYSTORAGE, STEADY, HARX, UH, UHCFG, DTBO, BLCMD, RADIO, CP_DEBUG, NAD_REFER, PERSISTENT, MISC, KEYREFUGE, KEYDATA, DQMDBG, VBMETA, VBMETA_SAMSUNG, METADATA | 1024-103423 | | **never written** (fenced in every driver) |
| BOOT | 31120 | 59 MiB | U-Boot + loader + kernel (15.8 MB) + root.apfs (4 MB) + command line: **WRITE** (as every test) |
| RECOVERY | 46224 | 66 MiB | TWRP, untouched |
| SUPER | 103424 | 9000 MiB | the iOS system image (2.4 GB; ~2.6 GB with Sileo): **WRITE** once from TWRP. The phone has no Android on it (owner, 14 Nov), so nothing is lost; TWRP keeps working; the stock `super.img` (AP tar in `E:\s20-g981b\stock-firmware`) would put Android's system back |
| PRISM, OPTICS, CACHE, OMR, SPU | 2407424-2850303 | | untouched |
| USERDATA | 2850304 | to the end, about 108 GiB (about 116 GB) | `/private/var` as HFS Plus, **110 GB** (110,000,000,000 bytes, owner, 14 Nov) from its start, the rest left empty as spare: **WRITE**. If the partition turns out smaller than 110 GB, the volume is the partition minus 1 GiB |
| boot LUNs (BOOTLOADER, LDFW, TZSW, ...) | | | never touched; whole LUNs fenced |

No partition table change (no GPT write): the riskiest part of the POCO's install is not needed.

### Space for iOS (answer for the owner)

- Today: `/private/var` is a memory filesystem, at most a quarter of the RAM the kernel sees (roughly 2.5 GB), empty
  again after every restart. The system itself is read only.
- After: `/private/var` = a 110 GB volume in USERDATA (the owner's choice), kept across restarts; iOS's Settings shows
  about 110 GB.

### Steps

| # | What | Written to the phone | Who |
|---|---|---|---|
| S1 | U-Boot `ufscheck` (menu item 12 "Storage check (read only)", written 14 Nov in x1s.patch): how S-BOOT leaves the UFS controller: enabled? link up, lanes, gear; its transfer list; the PRDT entry size / FMP type; the protection unit (SMU); whether U-Boot at EL1 under H-Arx reads these registers at all | BOOT (test image) | Claude builds; boot flash with owner's OK |
| S2 | U-Boot UFS driver for the Exynos 990 that **adopts S-BOOT's link** (no reset, no PHY setup: those need Samsung's calibration tables) and gives LUN 0 to the loader as a block device. Read only first: `part list`, the GPT and the heads of SUPER and USERDATA, compared with the PIT. A fence in the driver itself: any write outside USERDATA's blocks is refused | BOOT | same |
| S3 | **WRITE:** the system image into SUPER from TWRP (`dd` to `/dev/block/by-name/super`; TWRP's /tmp is RAM, so streamed or in pieces), SHA-256 read back | SUPER | owner's OK first |
| S4 | **"Boot iOS from storage"** (read only): the loader reads the system image from SUPER into RAM where `xl ramdisk` puts it (its `resident_rd` path), then boots the payload in BOOT. `/private/var` in memory as today. No UFS use while iOS runs, so all 8 cores stay iOS's. Same boot-args as run 109 | BOOT | owner's OK for the boot flash |
| S5 | **WRITE:** an empty 110 GB HFS Plus volume at the start of USERDATA (`tools/mkdatavol.py`, root owned by root), only its metadata blocks, from TWRP; loader + kernel: `ufs_write=1` allowed for USERDATA (today only `darwin`), `var_hfs=ufs_USERDATA_data` | USERDATA | owner's OK first (wipes Android's data) |
| S6 | **"Boot iOS from storage (writable)"**: as S4 plus `ufs=2 ufs_write=1 var_hfs=ufs_USERDATA_data`. The loader keeps core 0 to serve the writes, so iOS gets 7 cores | BOOT | |
| S7 | Sileo: mk-jb.sh made to work on Windows (a python extractor for .tar.zst and .deb that records links and set-ID bits into mkapfs's manifest); `JB=1` system image; `init`'s graft must accept an existing `/private/var/jb` (it makes every directory with mkdir and stops when one is there: fine in memory, wrong on a kept volume). Read-only entry: Sileo opens, installs vanish at restart; writable entry: installs kept | SUPER again (new image) | |
| S8 (later) | Root straight from SUPER (`system_apfs=ufs_SUPER`, Scion's init/kernel code) to free the 2.4 GB of RAM; the kernel's own Exynos UFS driver (Scion's `ufs=3`, plus `exynos990`) to give core 0 back and speed writes up | BOOT | |

Menu after S6 (`hakios-x1s.env`): `Boot iOS from storage` and `Boot iOS from storage (writable)` at the top; the 5 s
auto-pick stays on "HakiOS over USB" until the owner says otherwise. Both entries carry the heat guard's boot-args
(`exynos_thermal=1 exynos_thermal_stop=60`).

### S1 result (14 Nov, test 31, `fastboot oem run:ufscheck`): S-BOOT leaves UFS running -- the best case

Test 31 (U-Boot CI 43534447488, the new graphical menu + `ufscheck`) flashed to boot (old boot backed up in
`E:\s20-g981b\boot-backups\boot-before-test31.img`). The menu came up and took "HakiOS over USB" after 5 s.

| Register | Value | Meaning |
|---|---|---|
| HCE | 1 | the controller is on |
| HCS | 0x10f | flash present, transfer and task lists ready, UIC ready; last power-mode change: done (local) |
| CAP | 0x1303ff1f | 32 slots, 64-bit addressing; UFSHCI 3.0 (VER 0x300) |
| UTRLRSR / UTMRLRSR | 1 / 1 | both lists running |
| UTRLBA | 0xe8cf4000 (high 0) | S-BOOT's list in low RAM; slot 0: a finished command (OCS 0), its command descriptor at 0xe8cf1000 |
| UTRLDBR | 0 | nothing in flight |
| VS TX/RXPRDT_ENTRY_SIZE | 12 / 12 | plain 16-byte PRDT entries (no FMP crypto descriptors) |
| VS GPIO_OUT | 1 | the flash is out of reset |
| VS CLKSTOP_CTRL | 0 | no clock stopped |
| SYSREG 0x13020700 | 0x00c00011 | IO coherency bits (22-23) set |
| VS UTRL_NEXUS_TYPE | 0xffffffff | every slot a SCSI command |

So S2 can **adopt the link**: point UTRLBA at U-Boot's own list (with nothing in flight, list stopped, base moved,
list started, as hakios.ufshc.c does on the POCO), no reset, no PHY or UniPro setup. U-Boot read every register at
EL1 under H-Arx without a fault.

### S2 result (14 Nov, tests 35-37): U-Boot reads the phone's storage

`board/samsung/exynos-mobile/x1s-ufs.c` (U-Boot SCSI host, read only by construction), `x1sufs [wake|stats]`, menu
More 13. On the phone:
- test 35: the controller takes this driver's own list (UTRLRSR/UTRLBA written, read back right; the first answer lands
  in our descriptor). A CHECK CONDITION comes with OCS 7 and response 1 on this controller (as Scion's S9 notes).
- test 36: every unit answers NOT READY, "initializing command required" (sense 2, 04/02), even to INQUIRY: **S-BOOT
  leaves the flash asleep.** LUNs 0-4 exist, 5-7 do not (sense 5, 25/00).
- test 37, owner's OK (14 Nov, "go ahead"): `x1sufs wake` = START STOP UNIT, POWER CONDITION 1 (active), to the UFS
  Device well-known unit (UPIU LUN 0xD0), CDB 1B 00 00 00 10 00, as Linux at every resume. Then the scan: LUN 0
  **SAMSUNG KLUDG4UHDB-B2D1 rev 0089, 31234048 x 4096 (119.1 GiB)**; `part list scsi 0` = 32 partitions, every start
  as the PIT says (super 0x19400 = 103424, userdata 0x2b7e00 = 2850304). x1sufs stats: 3 reads, 0 refused.
- USERDATA: blocks 2850304-31232511 = 28382208 blocks = 116.25 GB (108.27 GiB): the owner's 110 GB volume fits with
  6.25 GB spare.
- the driver's memory: 512 KB at 0xfd880000 (LMB, below 4 GB).

Next: the wake becomes part of taking the controller (no more a separate step); S3 (system image into SUPER, needs the
owner's OK) and S4 (the loader reads it into RAM).

### S3/S4 (14 Nov): iOS image in SUPER; the storage's protection unit blocks reads past the partition table

- S3 done (owner's OK): `userland/system.apfs` with Sileo (tools/mk-jb-win.py; `build-images.sh <mesa-43527321141 gl>
  SYSTEM=1 GL=1 REST=1 WIFID=0`), 2,527,813,632 bytes, sha256 0e6fe193...1832ee, written to SUPER from TWRP and read
  back identical.
- Loader `rd_part=super` finds the partition through U-Boot's UEFI disks, but its first read fails; from the boot
  image the failed run resets the phone (tests 38-39).
- In U-Boot: after the wake, LBA 0-5 (GPT) read fine; every read further in fails with OCS f and IS bit 17 (SBFES,
  system bus fatal error), after which the controller answers nothing (sticky, needs a host reset). Not the address of
  our buffers (the same buffers serve the GPT reads). UTRLCNR does not clear when written (fixed: no longer a done sign).
- The phone's tree: `ufs@13100000` reg includes the UFS protector at **0x132a0000** (0x100), `fmp-id = 0`,
  `smu-id = 0`, `hw-rev = 6`. Samsung's ufs-exynos.c (sm-g980f) at host init: `crypto_sec_cfg(true)` =
  exynos_fmp_sec_cfg(fmp, smu, init): SMC_CMD_FMP_SECURITY 0xC2001810 and SMC_CMD_SMU 0xC2001850 (include/linux/smc.h);
  without FMP it writes the protector itself: UFSPSBEGIN0 0, UFSPSEND0 0xffffffff, UFSPSLUN0 0xff, UFSPSCTRL0 0xf1
  (one region: every block of every unit). Likely S-BOOT leaves the SMU allowing only what it reads.
- **Reading 0x132a0000 from U-Boot (EL1, non-secure) hangs the phone** (force restart): the protector is secure-only.
- Next: SMC_CMD_SMU (0xC2001850) with SMU_INIT (0), SMU id 0 from U-Boot, as Samsung's kernel does at every boot
  (needs exynos_fmp_sec_cfg's exact arguments from Samsung's FMP driver first), then the reads again.

### S4 DONE (14 Nov 13:04): "Boot iOS from storage" works from the menu, no PC -- home screen and Sileo (owner)

- Test 40 (U-Boot built in WSL, devices/x1s/u-boot/build-wsl.sh; local loader with `rd_part`; payload kernel
  43526339546 + root.apfs + E:\s20-g981b\builds\boot39-cmdline.txt; boot image sha256 ecfb09b9...194e66).
- The menu item: boot screen, `x1sufs wake` (START STOP UNIT active + the protector's two secure calls), `hakios load`,
  `hakios boot`; the loader reads SUPER (2410 MB in 4.4 s, about 550 MB/s; over USB it took over a minute) into the
  resident address and iOS runs as from a USB upload. Same run from the PC (logs/125449-run.log): home screen at
  12:58, 10 frames a second, 29-31 C, Sileo grafted on /var/jb (431 directories, 6365 files and links).
- Owner, 13:04: home screen, Sileo on page 2.
- Not yet: S5/S6 (the 110 GB data volume in USERDATA and the writable entry; Sileo's installs vanish at restart until
  then); the progress bar following the loader and the kernel; init's graft on a kept /var.

### Nothing may hard-brick the phone (owner, 14 Nov)

A hard brick is a phone that no longer reaches Download mode (Odin), so nothing can put firmware back. On this phone
Download mode is part of S-BOOT, on the boot LUNs (BOOTLOADER, BOOTLOADER2, LDFW, TZSW, ...). As long as those are never
written, any mistake anywhere else is undone with Odin and the stock firmware (`E:\s20-g981b\stock-firmware`). So:

- **Never written, by construction in every driver (U-Boot's, the loader's, the kernel's):** every LUN but LUN 0 (the
  boot LUNs, RPMB, the well-known units), LUN 0's partition tables (LBA 0-5, the backup GPT), and every partition
  of LUN 0 but USERDATA. The window is USERDATA's own blocks, found by name in the GPT and checked against the PIT's
  numbers (start 2850304) before anything is written; the two disagree and nothing is written.
- **Never sent to the flash at all:** UFS queries that write (descriptors, attributes, flags: the boot-LUN choice,
  the provisioning, the permanent write-protect flag), WRITE BUFFER (the flash's own firmware update), FORMAT UNIT,
  UNMAP, PURGE, SECURITY PROTOCOL OUT, task management, vendor commands. Only READ, WRITE and SYNCHRONIZE CACHE to
  USERDATA, plus reads anywhere. The same whitelist as the POCO's and Scion's drivers.
- **No PMIC writes, no change to the controller's power, clocks, PHY or link** (the driver adopts S-BOOT's link).
- **TWRP (RECOVERY) is never written**, so there is always a way in to look and to undo, besides Odin.
- **EFS / SEC_EFS** (the IMEI; not a brick, but not in Samsung's firmware) are never written, and are backed up in
  `E:\s20-g981b\backup-20261110`.
- The phone writes (S3 SUPER, S5 USERDATA) are done from TWRP by partition name (`/dev/block/by-name/super`,
  `.../userdata`), never by a raw offset, each shown to the owner first.
- The worst a test boot image can do is hang or crash U-Boot: a force restart (Volume Down + Power) and TWRP or
  Download mode as always.

### Risks

- **S-BOOT may switch UFS off before starting the boot image** (Linux resets it anyway). Then adopting is impossible
  and S2 becomes a full Exynos UFS bring-up (PHY calibration from Samsung's GPL kernel, `ufs-cal-9830`): weeks, not
  days. S1 answers this before anything else is built.
- **The hypervisor (H-Arx) may drop stores to UFS registers**, as it dropped the kernel's GPU stores through its normal
  mappings. U-Boot runs with its own identity mapping (where the GPU check worked); a later kernel driver would need
  the Mali-style private window.
- **The UFS protection unit (SMU/FMP)**: Samsung's kernel sets it up with secure-monitor calls. If S-BOOT leaves it set
  for its own use, our reads may be refused or come back scrambled. S1/S2 compare against known blocks (the GPT).
- **A wrong block can brick the phone** (EFS/SEC_EFS hold the IMEI; PARAM/UP_PARAM and the boot LUNs the boot chain):
  installs only from TWRP by `dd` to `/dev/block/by-name/...`, never by raw offset; U-Boot's driver and the loader
  both refuse any write outside USERDATA; fastboot flashing stays off in U-Boot.
- **Writes without a clean shutdown**: HFS Plus is journaled (tried on the POCO and in QEMU); powering off from iOS
  should flush first.
- Heat: storage work adds nothing notable; the heat guard stays on in every entry.

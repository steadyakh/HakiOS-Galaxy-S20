// SPDX-License-Identifier: GPL-2.0+
/*
 * The Galaxy S20's menu and boot screen, drawn straight into the framebuffer (devices/x1s/PLAN.md, 14 Nov):
 *
 *   - a logo in the middle of the upper screen. It is not in this source and never on GitHub: the owner's computer
 *     packs it into the boot image's ramdisk as the HKUB entry "logo" (devices/x1s/mkbootimg.py --logo), a header
 *     "HKLG", u32 width, u32 height, then one byte of coverage (0 black .. 255 white) per pixel. Without it the
 *     screen has no logo and everything else works the same;
 *   - "x1smenu": the items of the environment's x1smenu_0 .. x1smenu_9 ("Title=command"), centred as a block, the
 *     chosen one on a grey bar; Volume Up/Down move, Power picks;
 *     nothing happens by itself: the computer can pick an item over USB meanwhile (x1spick, below);
 *   - "x1sbar N": the boot screen: the logo and a progress bar N percent full, nothing else (the console is sent to
 *     the serial line only by the menu items that boot, so no text is drawn over it).
 *
 * Nothing here writes anything but the framebuffer.
 */
#include <button.h>
#include <command.h>
#include <console.h>
#include <dm.h>
#include <env.h>
#include <fdtdec.h>
#include <linux/delay.h>
#include <linux/libfdt.h>
#include <time.h>
#include <video.h>
#include <video_font.h>
#include <cpu_func.h>
#include <fastboot.h>
#include <g_dnl.h>
#include <linux/usb/gadget.h>
#include <lmb.h>
#include <asm/unaligned.h>
#include <init.h>
#include <vsprintf.h>

#define LOGO_MAGIC	0x474c4b48	/* "HKLG" */
#define HKUB_MAGIC	0x42554b48	/* "HKUB" */
#define MAX_ITEMS	10

struct hkub_head { u32 magic, version, count, header_size; };
struct hkub_ent { char name[16]; u32 offset, size, crc32, reserved; };

static struct udevice *vdev;
static struct video_priv *vp;
static const struct video_fontdata *font;

static int screen_init(void)
{
	int i;

	if (!vdev && uclass_first_device_err(UCLASS_VIDEO, &vdev))
		return -ENODEV;
	vp = dev_get_uclass_priv(vdev);
	if (vp->bpix != VIDEO_BPP32)
		return -EINVAL;
	for (i = 0; fonts[i].name; i++)
		if (fonts[i].width == 16 && fonts[i].height == 32)
			font = &fonts[i];
	if (!font)
		font = &fonts[0];
	return 0;
}

/*
 * Out to the panel. Everything here is drawn straight into U-Boot's framebuffer, past the console, so the video
 * layer (CONFIG_VIDEO_DAMAGE) does not know it changed: the whole screen is marked changed and the data cache
 * written out over it. Without this (test 32) only the parts the console had touched reached the panel, and the
 * rest arrived later, a cache line at a time: a striped logo, scrambled text, "glitching".
 */
static void show(void)
{
	video_damage(vdev, 0, 0, vp->xsize, vp->ysize);
	video_sync(vdev, true);
	flush_dcache_range((ulong)vp->fb, (ulong)vp->fb + (ulong)vp->line_length * vp->ysize);
}

static void fill(int x0, int y0, int x1, int y1, u32 c)
{
	int x, y;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > vp->xsize) x1 = vp->xsize;
	if (y1 > vp->ysize) y1 = vp->ysize;
	for (y = y0; y < y1; y++) {
		u32 *row = (u32 *)((u8 *)vp->fb + (ulong)y * vp->line_length);

		for (x = x0; x < x1; x++)
			row[x] = c;
	}
}

/* a rectangle with round ends (a pill), filled or as an outline of width w */
static void pill(int x0, int y0, int x1, int y1, u32 c, int w)
{
	int r = (y1 - y0) / 2, cy = y0 + r, y, x;

	for (y = y0; y < y1; y++) {
		int dy = y - cy, dx = 0, inner = -1;

		while ((dx + 1) * (dx + 1) + dy * dy <= r * r)
			dx++;
		if (w) {
			int ri = r - w, di = 0;

			if (dy * dy < ri * ri) {
				while ((di + 1) * (di + 1) + dy * dy <= ri * ri)
					di++;
				inner = di;
			}
		}
		for (x = x0 + r - dx; x < x1 - r + dx; x++) {
			if (w && inner >= 0 && x > x0 + r - inner && x < x1 - r + inner && y >= y0 + w && y < y1 - w)
				continue;
			((u32 *)((u8 *)vp->fb + (ulong)y * vp->line_length))[x] = c;
		}
	}
}

static void text(int x, int y, const char *s, int scale, u32 c)
{
	for (; *s; s++, x += font->width * scale) {
		const u8 *g = font->video_fontdata + (u8)*s * font->char_pixel_bytes;
		int row, col;

		for (row = 0; row < font->height; row++)
			for (col = 0; col < font->width; col++)
				if (g[row * font->byte_width + col / 8] & (0x80 >> (col % 8)))
					fill(x + col * scale, y + row * scale, x + (col + 1) * scale,
					     y + (row + 1) * scale, c);
	}
}

static void text_centred(int y, const char *s, int scale, u32 c)
{
	text((vp->xsize - (int)strlen(s) * font->width * scale) / 2, y, s, scale, c);
}

/* the logo from the boot image's ramdisk (an HKUB entry "logo"), or NULL */
static const u32 *find_logo(void)
{
	const void *fdt = (const void *)get_prev_bl_fdt_addr();
	const struct hkub_head *h;
	const struct hkub_ent *e;
	int node, i;
	u64 lo, hi;

	if (!fdt || fdt_check_header(fdt) || (node = fdt_path_offset(fdt, "/chosen")) < 0)
		return NULL;
	lo = fdtdec_get_uint64(fdt, node, "linux,initrd-start", 0);
	if (!lo)
		lo = fdtdec_get_uint(fdt, node, "linux,initrd-start", 0);
	hi = fdtdec_get_uint64(fdt, node, "linux,initrd-end", 0);
	if (!hi)
		hi = fdtdec_get_uint(fdt, node, "linux,initrd-end", 0);
	if (!lo || hi <= lo + sizeof(*h))
		return NULL;
	h = (const void *)(ulong)lo;
	if (h->magic != HKUB_MAGIC || h->count > 16)
		return NULL;
	e = (const void *)(h + 1);
	for (i = 0; i < h->count; i++, e++) {
		const u32 *l = (const void *)(ulong)(lo + e->offset);

		if (strncmp(e->name, "logo", sizeof(e->name)) || lo + e->offset + e->size > hi || e->size < 12)
			continue;
		if (l[0] != LOGO_MAGIC || (u64)l[1] * l[2] + 12 > e->size || l[1] > 1440 || l[2] > 1440)
			return NULL;
		return l;
	}
	return NULL;
}

/* the logo, its top at y; returns the y under it */
static int draw_logo(int y)
{
	const u32 *l = find_logo();
	const u8 *a;
	int w, hgt, x0, x, r;

	if (!l)
		return y + 360;
	w = l[1];
	hgt = l[2];
	a = (const u8 *)(l + 3);
	x0 = (vp->xsize - w) / 2;
	for (r = 0; r < hgt; r++) {
		u32 *row = (u32 *)((u8 *)vp->fb + (ulong)(y + r) * vp->line_length);

		for (x = 0; x < w; x++) {
			u32 v = a[r * w + x];

			row[x0 + x] = 0xff000000 | v << 16 | v << 8 | v;
		}
	}
	return y + hgt;
}

#define BAR_W	560
#define BAR_H	40
#define LOGO_Y	600
#define BAR_Y	1060

static void draw_bar(int percent)
{
	int x0 = (vp->xsize - BAR_W) / 2;

	fill(x0 - 4, BAR_Y - 4, x0 + BAR_W + 4, BAR_Y + BAR_H + 4, 0xff000000);
	pill(x0, BAR_Y, x0 + BAR_W, BAR_Y + BAR_H, 0xffffffff, 5);
	if (percent > 0)
		pill(x0, BAR_Y, x0 + BAR_H + (BAR_W - BAR_H) * (percent > 100 ? 100 : percent) / 100,
		     BAR_Y + BAR_H, 0xffffffff, 0);
}

static int do_x1sbar(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	static bool drawn;

	if (screen_init())
		return CMD_RET_FAILURE;
	if (!drawn || (argc > 2 && !strcmp(argv[2], "new"))) {
		fill(0, 0, vp->xsize, vp->ysize, 0xff000000);
		draw_logo(LOGO_Y);
		drawn = true;
	}
	/* a percent below 0: the logo alone (the USB wait), no bar */
	if (argc < 2 || simple_strtol(argv[1], NULL, 10) >= 0)
		draw_bar(argc > 1 ? (int)simple_strtol(argv[1], NULL, 10) : 0);
	if (argc > 3)
		text_centred(BAR_Y + 160, argv[3], 1, 0xffc8c8c8);
	show();
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(x1sbar, 4, 0, do_x1sbar,
	   "the boot screen: the logo and a progress bar (percent < 0: no bar)", "[percent [new [line]]]");

/* the menu */
static char titles[MAX_ITEMS][40];
static char vars[MAX_ITEMS][16];
static int nitems;

#define ITEM_SCALE	2
#define ITEM_Y		1225
#define ITEM_STEP	132
#define GREY		0xff3c3c3c

/* the whole screen once (full), afterwards only the items: no flash of black on every key */
static void draw_menu(int sel, bool full)
{
	int cw = font->width * ITEM_SCALE, ch = font->height * ITEM_SCALE, widest = 0, x0, i;

	if (full) {
		fill(0, 0, vp->xsize, vp->ysize, 0xff000000);
		draw_logo(LOGO_Y);
		text_centred(2800, "USE VOL UP/DOWN BUTTONS TO NAV", 1, 0xffc8c8c8);
		text_centred(2850, "AND POWER TO SELECT", 1, 0xffc8c8c8);
		pill((vp->xsize - 400) / 2, 3000, (vp->xsize + 400) / 2, 3016, 0xffc8c8c8, 0);
	}
	for (i = 0; i < nitems; i++)
		if ((int)strlen(titles[i]) > widest)
			widest = strlen(titles[i]);
	x0 = (vp->xsize - widest * cw) / 2;
	fill(0, ITEM_Y - 40, vp->xsize, ITEM_Y + nitems * ITEM_STEP + 40, 0xff000000);
	for (i = 0; i < nitems; i++) {
		int y = ITEM_Y + i * ITEM_STEP;

		if (i == sel)
			pill(x0 - 48, y - 18, x0 + widest * cw + 48, y + ch + 18, GREY, 0);
		text(x0, y, titles[i], ITEM_SCALE, 0xffffffff);
	}
	show();
}

static struct udevice *key_dev[3];          /* up, down, power */

static int keys_down(void)
{
	static const char *const labels[] = { "Volume Up", "Volume Down", "Power" };
	int i, m = 0;

	for (i = 0; i < 3; i++) {
		if (!key_dev[i])
			button_get_by_label(labels[i], &key_dev[i]);
		if (key_dev[i] && button_get_state(key_dev[i]) == BUTTON_ON)
			m |= 1 << i;
	}
	return m;
}

/*
 * The computer's say (owner, 14 Nov: the phone must not go into USB mode by itself, but stay on the menu, and when
 * Claude is working on it hands-off, the computer picks the mode). While the menu is up, U-Boot's fastboot listens on
 * USB in the background, as "fastboot" alone would:
 *   fastboot oem run:x1spick N     runs menu item N (0 = the first), as if picked with the keys
 *   fastboot stage x.hkub; fastboot oem run:hakios load fastboot; fastboot continue
 *                                  the loader from the computer, as "HakiOS over USB" did (xl enter)
 * Nothing on the phone happens by itself: with no key and no computer the menu stays until the heat guard's time.
 */
static int picked = -1;

static int do_x1spick(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	if (argc < 2)
		return CMD_RET_USAGE;
	picked = simple_strtol(argv[1], NULL, 10);
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(x1spick, 2, 0, do_x1spick,
	   "pick a menu item from the computer (fastboot oem run:x1spick N)", "N");

static int do_x1smenu(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct udevice *udc = NULL;
	int sel, last, now, i, ret;
	bool usb_continue = false;
	ulong keys_at;
	const char *s;
	char *eq;

	if (screen_init())
		return CMD_RET_FAILURE;
	/*
	 * U-Boot's own text never on the menu or the screens its items draw: the console goes to the serial line only
	 * (fastboot's "oem console" still has all of it: the record is kept whatever the devices). "More..." puts the
	 * screen back for the text menu and its tools.
	 */
	env_set("stdout", "serial");
	env_set("stderr", "serial");
	for (nitems = 0, i = 0; i < MAX_ITEMS; i++) {
		char name[16];

		snprintf(name, sizeof(name), "x1smenu_%d", i);
		s = env_get(name);
		if (!s || !(eq = strchr(s, '=')))
			continue;
		snprintf(titles[nitems], sizeof(titles[0]), "%.*s", (int)(eq - s), s);
		strcpy(vars[nitems], name);
		nitems++;
	}
	if (!nitems)
		return CMD_RET_FAILURE;
	sel = env_get_ulong("x1smenu_default", 10, 0);
	if (sel >= nitems)
		sel = 0;

	/* fastboot in the background, at the address "HakiOS over USB" uses */
	fastboot_init((void *)env_get_hex("fastboot_addr_r", 0), 0);
	if (!udc_device_get_by_index(0, &udc)) {
		g_dnl_clear_detach();
		if (g_dnl_register("usb_dnl_fastboot")) {
			udc_device_put(udc);
			udc = NULL;
		}
	} else {
		udc = NULL;
	}

	draw_menu(sel, true);
	picked = -1;
	last = keys_down();                         /* a key held from before (the restart) is no press */
	keys_at = get_timer(0);
	for (;;) {
		if (udc) {
			dm_usb_gadget_handle_interrupts(udc);
			if (g_dnl_detach()) {               /* fastboot continue */
				usb_continue = true;
				break;
			}
		}
		schedule();
		if (picked >= 0 && picked < nitems) {
			ulong t = get_timer(0);

			/* the answer to the computer's "oem run" goes out before USB is let go */
			while (udc && get_timer(t) < 200)
				dm_usb_gadget_handle_interrupts(udc);
			sel = picked;
			break;
		}
		if (get_timer(keys_at) < 20)
			continue;
		keys_at = get_timer(0);
		now = keys_down();
		if (now & ~last) {
			if (now & ~last & 1)
				sel = (sel + nitems - 1) % nitems;
			else if (now & ~last & 2)
				sel = (sel + 1) % nitems;
			else
				break;
			draw_menu(sel, false);
		}
		last = now;
		if (ctrlc())
			break;
	}
	if (udc) {
		udc_device_put(udc);
		g_dnl_unregister();
		g_dnl_clear_detach();
	}
	if (usb_continue) {
		run_command("x1sbar -1 new", 0);
		return run_command("hakios load fastboot && x1sguard off && hakios boot; pause", 0);
	}
	/* run the item: "Title=command" */
	s = env_get(vars[sel]);
	eq = strchr(s, '=');
	ret = run_command(eq + 1, 0);
	return ret;
}

U_BOOT_CMD(x1smenu, 1, 0, do_x1smenu,
	   "the S20's menu: the logo and the items of x1smenu_0..9 (Title=command)", "");

/*
 * "x1sargs ARGS": the payload "hakios load" took (an XNUP at CONFIG_HAKIOS_PAYLOAD_BASE) again, its command line with
 * ARGS added at the end, through "hakios load ADDR SIZE": one boot image, two ways to start (the menu's "Boot iOS
 * (writable)" adds the storage service and the data volume to the same kernel and image). The copy is made in RAM
 * U-Boot's LMB gives out; nothing else changes.
 */
static int do_x1sargs(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	const u8 *src = (const u8 *)CONFIG_HAKIOS_PAYLOAD_BASE;
	u32 size, at = 8, out = 8;
	phys_addr_t buf = 0;
	u8 *dst;
	char cmd[64];
	int found = 0;

	if (argc < 2)
		return CMD_RET_USAGE;
	if (get_unaligned_le32(src) != 0x50554e58) {
		printf("x1sargs: no payload at %#x (hakios load first)\n", CONFIG_HAKIOS_PAYLOAD_BASE);
		return CMD_RET_FAILURE;
	}
	size = get_unaligned_le32(src + 4);
	if (size > (60u << 20) || lmb_alloc_mem(LMB_MEM_ALLOC_ANY, 0x1000, &buf, size + 8 + 4096, LMB_NONE)) {
		printf("x1sargs: no room for a copy of %u bytes\n", size);
		return CMD_RET_FAILURE;
	}
	dst = (u8 *)buf;
	while (at + 24 <= size + 8) {
		const u8 *e = src + at;
		u32 len = get_unaligned_le32(e + 16), padded = (len + 15) & ~15u;

		if (e[0] == 0)
			break;
		if (at + 24 + padded > size + 8) {
			printf("x1sargs: the payload is damaged\n");
			return CMD_RET_FAILURE;
		}
		memcpy(dst + out, e, 24);
		if (!strncmp((const char *)e, "cmdline", 16)) {
			u32 old = strnlen((const char *)e + 24, len), add = strlen(argv[1]), n = old + 1 + add + 1;

			if (n > 4096) {
				printf("x1sargs: the command line would be too long\n");
				return CMD_RET_FAILURE;
			}
			memcpy(dst + out + 24, e + 24, old);
			dst[out + 24 + old] = ' ';
			memcpy(dst + out + 24 + old + 1, argv[1], add + 1);
			put_unaligned_le32(n, dst + out + 16);
			memset(dst + out + 24 + n, 0, ((n + 15) & ~15u) - n);
			out += 24 + ((n + 15) & ~15u);
			printf("x1sargs: command line: %s\n", (const char *)dst + out - ((n + 15) & ~15u));
			found = 1;
		} else {
			memcpy(dst + out + 24, e + 24, padded);
			out += 24 + padded;
		}
		at += 24 + padded;
	}
	if (!found) {
		printf("x1sargs: the payload has no command line\n");
		return CMD_RET_FAILURE;
	}
	memset(dst + out, 0, 24);
	out += 24;
	put_unaligned_le32(0x50554e58, dst);
	put_unaligned_le32(out - 8, dst + 4);
	snprintf(cmd, sizeof(cmd), "hakios load %lx %x", (ulong)buf, out);
	return run_command(cmd, 0);
}

U_BOOT_CMD(x1sargs, 2, 0, do_x1sargs,
	   "the loaded payload again, ARGS added to its command line", "\"ARGS\"");

/*
 * "x1sguard off": U-Boot's own periodic work (the heat guard of board/samsung/exynos-mobile, every second: the
 * temperatures from the APM, and the power off after hakios_max_minutes) stopped at the hand-over to iOS. With the
 * loader serving the storage (ufs=2) U-Boot's code runs on under the kernel, and with it the guard: it then spoke to
 * the APM alongside the kernel's own heat guard (exynos_thermal), and would power the phone off 30 minutes after
 * U-Boot started (14 Nov: a writable run stopped answering after about 150 s). From the hand-over the kernel's
 * guard watches the heat (exynos_thermal_stop).
 */
#include <cyclic.h>

static int do_x1sguard(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	if (argc < 2 || strcmp(argv[1], "off"))
		return CMD_RET_USAGE;
	cyclic_unregister_all();
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(x1sguard, 2, 0, do_x1sguard,
	   "U-Boot's heat guard and other periodic work off, at the hand-over to iOS", "off");

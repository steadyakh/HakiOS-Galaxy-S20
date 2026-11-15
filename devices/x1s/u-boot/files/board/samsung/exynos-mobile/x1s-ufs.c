// SPDX-License-Identifier: GPL-2.0+
/*
 * The Galaxy S20's storage (UFS 3.0 on the Exynos 990), READ ONLY, for HakiOS's U-Boot (devices/x1s/PLAN.md "iOS from the
 * phone's own storage", step S2).
 *
 * S-BOOT leaves the controller running when it starts the boot image (S1, test 31: HCE 1, the link up, both lists
 * running, plain PRDT size registers, the flash out of reset, no clock stopped, IO coherency set). This driver ADOPTS
 * that state and sets nothing up: no reset, no link startup, no UIC or DME command, no PHY, clock, power or interrupt
 * setting, no query (descriptor, attribute, flag), no task management. The only controller registers written: the
 * transfer list's run/stop and base (UTRLRSR, UTRLBA, UTRLBAU) to point it at this driver's own list, with nothing in
 * flight, as hakios.ufshc.c does on the POCO and Scion's on the S9; the doorbell (UTRLDBR) of this list's slot 0; the
 * completion bits it sets (IS bit 0, UTRLCNR bit 0, a one clears); UTRLCLR bit 0 for a request that never ends.
 *
 * READ ONLY BY CONSTRUCTION: exec() refuses every SCSI operation but TEST UNIT READY, REQUEST SENSE, INQUIRY, READ
 * CAPACITY(10/16), READ(10/16), and any data direction but from the flash; a request descriptor is only ever built
 * with "device to host" or "none", and the command UPIU's flags never carry W. Only logical units 0-7 (never a
 * well-known one: the device, BOOT, RPMB, REPORT LUNS). Nothing can be written to the flash through this driver.
 * The one exception to "reads only" is no write either: "x1sufs wake" (below), the flash's power state.
 *
 * The Exynos layout (as S-BOOT's own last request in its list shows it, and as Samsung's and Scion's drivers make it):
 * the response UPIU's and PRDT's offsets and lengths in the descriptor in BYTES (UFSHCD_QUIRK_PRDT_BYTE_GRAN), command
 * UPIU 1024 bytes, response 1024, PRDT at 2048; a PRDT entry of 128 bytes (the flash protector's descriptor type 3: the
 * standard four words, then its IVs and keys, here zero = no encryption) -- which is also right for type 0, where the
 * spare entries are never filled (the data ends in the first ones); at most 4 KB in one entry; command type 0 and the
 * interrupt bit as S-BOOT's (the interrupt itself is off: IE 0, this driver polls); every slot's nexus type is already
 * SCSI (vendor UTRL_NEXUS_TYPE all ones) and only SCSI is sent. DMA below 4 GB (Samsung's driver's 32-bit mask): the
 * list, the command descriptor and a bounce buffer for the data in 512 KB of RAM below 4 GB, taken from U-Boot's LMB
 * (and with it out of the UEFI memory map's free memory, so the loader and the kernel leave it alone).
 */
#include <blk.h>
#include <command.h>
#include <cpu_func.h>
#include <dm.h>
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <dm/root.h>
#include <dm/uclass-internal.h>
#include <lmb.h>
#include <log.h>
#include <part.h>
#include <scsi.h>
#include <time.h>
#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <linux/delay.h>

#define HCI		0x13100000UL
#define VS		0x13101100UL

#define CAP		0x00
#define VER		0x08
#define IS		0x20
#define IE		0x24
#define HCS		0x30
#define HCE		0x34
#define UTRLBA		0x50
#define UTRLBAU		0x54
#define UTRLDBR		0x58
#define UTRLCLR		0x5c
#define UTRLRSR		0x60
#define UTRLCNR		0x64
#define VS_TXPRDT	0x00
#define VS_RXPRDT	0x04
#define VS_NEXUS	0x40
#define VS_SW_RST	0x50

#define HCS_DP		BIT(0)
#define HCS_UTRLRDY	BIT(1)
#define IS_UTRCS	BIT(0)
#define IS_FATAL	(BIT(2) | BIT(11) | BIT(12) | BIT(16) | BIT(17))

#define DD_NONE		(0u << 25)
#define DD_IN		(2u << 25)		/* device to host; host to device (1) is never built */
#define UTRD_I		BIT(24)
#define OCS_INVALID	0xf

#define UPIU_COMMAND	0x01
#define UPIU_RESPONSE	0x21
#define UPIU_FLAG_READ	0x40

#define CMD_UPIU	0			/* in the command descriptor (UCD) */
#define RSP_UPIU	1024
#define RSP_LEN		1024
#define PRDT_OFF	2048
#define PRD_SIZE	128
#define PRD_MAX		4096			/* bytes in one entry */

#define MEM_SIZE	(512u << 10)
#define LIST_OFF	0			/* 32 descriptors of 32 bytes (1 KB aligned) */
#define UCD_OFF		4096			/* one command descriptor: 2 KB + the PRDT */
#define BOUNCE_OFF	(64u << 10)
#define BOUNCE_SIZE	(256u << 10)		/* 64 entries of 4 KB: 2048 + 64 * 128 = 10 KB of descriptor */
#define SLOT		0

static ulong mem;				/* the 512 KB below 4 GB, 0 until taken */
static u32 sboot_lo, sboot_hi;			/* S-BOOT's list, for the log */
static ulong reads, refused, writes;

/*
 * The one place a write may go (devices/x1s/PLAN.md S5, the owner's 110 GB for iOS): the first VOL_BLOCKS blocks of
 * USERDATA on LUN 0, the HFS Plus volume iOS keeps /private/var in. Off until "x1sufs writes" (the writable menu
 * entry) has found USERDATA by name in LUN 0's GPT where Samsung's PIT puts it (block 2850304, at least VOL_BLOCKS
 * long); then wr_first..wr_first + wr_count is the window, and every WRITE outside it is refused. Every write goes
 * with FUA (straight to the flash, not its cache): a power cut loses nothing that was answered.
 */
#define USERDATA_PIT_START	2850304ULL
#define VOL_BLOCKS		26855468ULL	/* 110,000,000,000 bytes in blocks of 4096, rounded down */
static u64 wr_first, wr_count;

static u64 cdb_lba(const u8 *c)
{
	if (c[0] == 0x8a)
		return (u64)c[2] << 56 | (u64)c[3] << 48 | (u64)c[4] << 40 | (u64)c[5] << 32 |
		       (u64)c[6] << 24 | (u64)c[7] << 16 | (u64)c[8] << 8 | c[9];
	return (u64)c[2] << 24 | (u64)c[3] << 16 | (u64)c[4] << 8 | c[5];
}

static u64 cdb_blocks(const u8 *c)
{
	if (c[0] == 0x8a)
		return (u64)c[10] << 24 | (u64)c[11] << 16 | (u64)c[12] << 8 | c[13];
	return (u64)c[7] << 8 | c[8];
}

/* a WRITE(10/16) or SYNCHRONIZE CACHE(10) that stays inside the window */
static bool write_permitted(const struct scsi_cmd *c)
{
	u64 lba, n;

	if (!wr_count || c->lun != 0 || c->target != 0)
		return false;
	if (c->cmd[0] == 0x35)			/* SYNCHRONIZE CACHE(10): no data, the flash's cache out */
		return c->dma_dir == DMA_NONE;
	if ((c->cmd[0] != 0x2a && c->cmd[0] != 0x8a) || c->dma_dir != DMA_TO_DEVICE)
		return false;
	lba = cdb_lba(c->cmd);
	n = cdb_blocks(c->cmd);
	return n != 0 && c->datalen == n * 4096 && lba >= wr_first && lba < wr_first + wr_count &&
	       n <= wr_first + wr_count - lba;
}

static u32 hr(ulong off) { return readl(HCI + off); }
static void hw(ulong off, u32 v) { writel(v, HCI + off); }

static void sync_out(ulong a, ulong len)
{
	flush_dcache_range(rounddown(a, ARCH_DMA_MINALIGN), roundup(a + len, ARCH_DMA_MINALIGN));
}

static void sync_in(ulong a, ulong len)
{
	invalidate_dcache_range(rounddown(a, ARCH_DMA_MINALIGN), roundup(a + len, ARCH_DMA_MINALIGN));
}

/* the operations this driver lets through: reads and the looks the SCSI layer needs, nothing that writes */
static bool permitted(const struct scsi_cmd *c)
{
	if (c->lun > 7 || c->target != 0 || c->cmdlen == 0 || c->cmdlen > 16)
		return false;
	if (c->dma_dir == DMA_TO_DEVICE || c->dma_dir == DMA_BIDIRECTIONAL)
		return false;
	switch (c->cmd[0]) {
	case 0x00:				/* TEST UNIT READY */
	case 0x03:				/* REQUEST SENSE */
	case 0x12:				/* INQUIRY */
	case 0x25:				/* READ CAPACITY(10) */
	case 0x28:				/* READ(10) */
	case 0x88:				/* READ(16) */
		return true;
	case 0x9e:				/* SERVICE ACTION IN(16): READ CAPACITY(16) alone */
		return (c->cmd[1] & 0x1f) == 0x10;
	default:
		return false;
	}
}

/* one request on slot 0 of this driver's list; data (if any) lands in the bounce buffer. 0, or -ve */
static int request_dir(u8 lun, const u8 *cdb, int cdblen, ulong len, bool out, u8 *status, u8 *sense,
		       int sense_len)
{
	u32 *utrd = (u32 *)(mem + LIST_OFF + SLOT * 32);
	u8 *ucd = (u8 *)(mem + UCD_OFF), *rsp = ucd + RSP_UPIU;
	ulong ucd_pa = mem + UCD_OFF, data = mem + BOUNCE_OFF, start;
	u32 n = 0, at, ocs, bit = BIT(SLOT);

	if (len > BOUNCE_SIZE)
		return -E2BIG;
	if (hr(UTRLDBR) & bit)
		return -EBUSY;
	memset(ucd, 0, PRDT_OFF + (BOUNCE_SIZE / PRD_MAX) * PRD_SIZE);
	/* the command UPIU: transaction, flags (read, write or none), LUN, task tag, expected length, CDB */
	ucd[0] = UPIU_COMMAND;
	ucd[1] = len ? (out ? 0x20 : UPIU_FLAG_READ) : 0;
	ucd[2] = lun;
	ucd[3] = SLOT;
	ucd[12] = len >> 24;
	ucd[13] = len >> 16;
	ucd[14] = len >> 8;
	ucd[15] = len;
	memcpy(ucd + 16, cdb, cdblen);
	for (at = 0; at < len; at += PRD_MAX, n++) {
		u32 *prd = (u32 *)(ucd + PRDT_OFF + n * PRD_SIZE);
		u32 piece = len - at < PRD_MAX ? len - at : PRD_MAX;

		prd[0] = (u32)(data + at);
		prd[1] = 0;
		prd[2] = 0;
		prd[3] = piece - 1;		/* bits 26-31 (key length, encryption) 0: none */
	}
	utrd[0] = (len ? (out ? (1u << 25) : DD_IN) : DD_NONE) | UTRD_I;	/* command type 0, as S-BOOT's */
	utrd[1] = 0;
	utrd[2] = OCS_INVALID;
	utrd[3] = 0;
	utrd[4] = (u32)ucd_pa;
	utrd[5] = 0;
	utrd[6] = (RSP_UPIU << 16) | RSP_LEN;
	utrd[7] = (PRDT_OFF << 16) | (n * PRD_SIZE);
	/*
	 * The last look before the doorbell: a write only as the window allows it (checked again here, on what the
	 * descriptor holds), and only LUN 0
	 */
	if (((utrd[0] & (3u << 25)) == (1u << 25) || (ucd[1] & 0x20)) &&
	    (!out || lun != 0 || !wr_count || (cdb[0] != 0x2a && cdb[0] != 0x8a) || cdb_lba(cdb) < wr_first ||
	     cdb_lba(cdb) + cdb_blocks(cdb) > wr_first + wr_count))
		return -EPERM;
	sync_out(mem + LIST_OFF, 32 * 32);
	sync_out(ucd_pa, PRDT_OFF + n * PRD_SIZE);
	if (len && out)
		sync_out(data, len);
	else if (len)
		sync_in(data, len);
	hw(UTRLCNR, bit);			/* a stale completion bit is no sign of this request */
	hw(IS, IS_UTRCS);
	__asm__ volatile("dsb sy" ::: "memory");
	hw(UTRLDBR, bit);
	start = get_timer(0);
	for (;;) {
		sync_in(mem + LIST_OFF, 32);
		sync_in(ucd_pa + RSP_UPIU, 64);
		ocs = utrd[2] & 0xff;
		/*
		 * Done: the doorbell bit down, or the descriptor and the answer written back. NOT the UTRLCNR bit alone:
		 * on this controller it stays up after a one is written to it (test 38: a 4 KB read right after the wake
		 * was taken for done at once, cleared with UTRLCLR before it finished, and failed with OCS f).
		 */
		if (!(hr(UTRLDBR) & bit) ||
		    (ocs != OCS_INVALID && (rsp[0] & 0x3f) == UPIU_RESPONSE && rsp[3] == SLOT))
			break;
		if (hr(IS) & IS_FATAL) {
			printf("x1s-ufs: the controller reports an error (IS %08x, SMU abort read %08x write %08x)\n", hr(IS), readl(VS + 0x118), readl(VS + 0x11c));
			break;
		}
		if (get_timer(start) > 2000) {
			printf("x1s-ufs: no answer in 2 s (doorbell %08x, completion %08x, IS %08x, OCS %x)\n",
			       hr(UTRLDBR), hr(UTRLCNR), hr(IS), ocs);
			break;
		}
	}
	/* the descriptor's write-back may come a moment after the doorbell bit goes down */
	for (start = get_timer(0); get_timer(start) < 100;) {
		sync_in(mem + LIST_OFF, 32);
		sync_in(ucd_pa + RSP_UPIU, 64);
		if ((utrd[2] & 0xff) != OCS_INVALID && (rsp[0] & 0x3f) == UPIU_RESPONSE && rsp[3] == SLOT)
			break;
	}
	/* a doorbell bit still up once the answer is there is cleared (on this controller a one clears it) */
	for (start = get_timer(0); (hr(UTRLDBR) & bit) && get_timer(start) < 10;)
		;
	if (hr(UTRLDBR) & bit) {
		printf("x1s-ufs: slot still busy after its answer: cleared (UTRLCNR %08x)\n", hr(UTRLCNR));
		hw(UTRLCLR, bit);
	}
	hw(UTRLCNR, bit);
	hw(IS, IS_UTRCS);
	sync_in(mem + LIST_OFF, 32);
	sync_in(ucd_pa + RSP_UPIU, RSP_LEN);
	ocs = utrd[2] & 0xff;
	if ((rsp[0] & 0x3f) != UPIU_RESPONSE || rsp[3] != SLOT) {
		printf("x1s-ufs: request %02x on LUN %u: no answer in this list (OCS %x, %02x %02x)\n", cdb[0], lun, ocs,
		       rsp[0], rsp[3]);
		return -EIO;
	}
	/*
	 * The flash's answer is here. On this controller a CHECK CONDITION comes with OCS 7 (as on the S9:
	 * UFSHCD_QUIRK_BROKEN_OCS_FATAL_ERROR) and the response byte 1 -- seen on the phone (test 35): the first TEST
	 * UNIT READY after S-BOOT. The SCSI status decides; the sense data says why.
	 */
	*status = rsp[7];
	if (*status == 2) {			/* CHECK CONDITION: the sense data after the 32-byte header */
		int sl = rsp[32] << 8 | rsp[33];

		printf("x1s-ufs: %02x on LUN %u: check condition, sense key %x, code %02x/%02x\n", cdb[0], lun,
		       rsp[34 + 2] & 0xf, rsp[34 + 12], rsp[34 + 13]);
		if (sense)
			memcpy(sense, rsp + 34, sl < sense_len ? sl : sense_len);
		return -EIO;
	}
	if (ocs != 0 || rsp[6] != 0) {
		printf("x1s-ufs: request %02x on LUN %u failed: OCS %x, response %02x, status %02x\n", cdb[0], lun, ocs,
		       rsp[6], rsp[7]);
		return -EIO;
	}
	if (len)
		sync_in(data, len);
	return *status == 0 ? 0 : -EIO;
}

static int request(u8 lun, const u8 *cdb, int cdblen, ulong len, u8 *status, u8 *sense, int sense_len)
{
	return request_dir(lun, cdb, cdblen, len, false, status, sense, sense_len);
}

static int x1s_ufs_exec(struct udevice *dev, struct scsi_cmd *c)
{
	u8 status = 0xff;
	int ret;

	if (!mem)
		return -ENODEV;
	if (write_permitted(c)) {
		u8 cdb[16];

		memcpy(cdb, c->cmd, c->cmdlen);
		if (cdb[0] == 0x2a || cdb[0] == 0x8a) {
			cdb[1] |= 0x08;			/* FUA */
			memcpy((void *)(mem + BOUNCE_OFF), c->pdata, c->datalen);
		}
		ret = request_dir(0, cdb, c->cmdlen, cdb[0] == 0x35 ? 0 : c->datalen, cdb[0] != 0x35, &status,
				  c->sense_buf, sizeof(c->sense_buf));
		c->status = status;
		c->contr_stat = ret ? 1 : 0;
		if (!ret && cdb[0] != 0x35) {
			c->trans_bytes = c->datalen;
			writes++;
		}
		return ret;
	}
	if (!permitted(c)) {
		refused++;
		printf("x1s-ufs: REFUSED operation %02x (LUN %u, direction %d): this driver only reads\n", c->cmd[0],
		       c->lun, c->dma_dir);
		return -EPERM;
	}
	ret = request(c->lun, c->cmd, c->cmdlen, c->dma_dir == DMA_FROM_DEVICE ? c->datalen : 0, &status,
		      c->sense_buf, sizeof(c->sense_buf));
	c->status = status;
	c->contr_stat = ret ? 1 : 0;
	if (!ret && c->dma_dir == DMA_FROM_DEVICE && c->datalen) {
		memcpy(c->pdata, (void *)(mem + BOUNCE_OFF), c->datalen);
		c->trans_bytes = c->datalen;
		if (c->cmd[0] == 0x28 || c->cmd[0] == 0x88)
			reads++;
	}
	return ret;
}

static int x1s_ufs_bus_reset(struct udevice *dev)
{
	return 0;				/* never: a reset is the one thing this driver must not do */
}

/* take the controller from S-BOOT: checks first, then this driver's list in, then a TEST UNIT READY as the proof */
static int x1s_ufs_probe(struct udevice *dev)
{
	struct scsi_plat *plat = dev_get_uclass_plat(dev);
	phys_addr_t at = 0xffffffffUL;		/* the highest address the memory may reach: 4 GB */
	ulong start;
	u8 status = 0xff, tur[6] = { 0 };
	int ret;

	plat->max_id = 1;
	plat->max_lun = 8;
	plat->max_bytes_per_req = BOUNCE_SIZE;
	if (!(hr(HCE) & 1) || (hr(HCS) & (HCS_DP | HCS_UTRLRDY)) != (HCS_DP | HCS_UTRLRDY)) {
		printf("x1s-ufs: the controller is not as S-BOOT leaves it (HCE %08x, HCS %08x): not taken\n", hr(HCE),
		       hr(HCS));
		return -ENODEV;
	}
	if ((readl(VS + VS_TXPRDT) & 0x1f) != 12 || (readl(VS + VS_RXPRDT) & 0x1f) != 12 || (readl(VS + VS_SW_RST) & 3)) {
		printf("x1s-ufs: the vendor registers are not as expected (PRDT %x %x, SW_RST %x): not taken\n",
		       readl(VS + VS_TXPRDT), readl(VS + VS_RXPRDT), readl(VS + VS_SW_RST));
		return -ENODEV;
	}
	if (!mem) {
		ret = lmb_alloc_mem(LMB_MEM_ALLOC_MAX, 0x10000, &at, MEM_SIZE, LMB_NONE);
		if (ret || at + MEM_SIZE > 0x100000000ULL) {
			printf("x1s-ufs: no 512 KB below 4 GB (%d)\n", ret);
			return -ENOMEM;
		}
		mem = at;
		memset((void *)mem, 0, MEM_SIZE);
		sync_out(mem, MEM_SIZE);
	}
	/* nothing in flight, then the list stopped, pointed here, started */
	for (start = get_timer(0); hr(UTRLDBR) && get_timer(start) < 100;)
		;
	if (hr(UTRLDBR)) {
		printf("x1s-ufs: S-BOOT's list still has requests in flight (%08x): not taken\n", hr(UTRLDBR));
		return -EBUSY;
	}
	if (hr(UTRLBA) != (u32)mem || hr(UTRLBAU) != 0) {
		sboot_lo = hr(UTRLBA);
		sboot_hi = hr(UTRLBAU);
		hw(UTRLRSR, 0);
		hw(UTRLBA, (u32)mem);
		hw(UTRLBAU, 0);
		hw(UTRLRSR, 1);
	}
	if (hr(UTRLBA) != (u32)mem || hr(UTRLBAU) != 0 || !(hr(UTRLRSR) & 1)) {
		printf("x1s-ufs: the list base did not take (UTRLBA %08x UTRLBAU %08x UTRLRSR %x): not taken\n",
		       hr(UTRLBA), hr(UTRLBAU), hr(UTRLRSR));
		return -EIO;
	}
	/* the proof that the controller reads this list: the answer must land in this driver's descriptor */
	ret = request(0, tur, 6, 0, &status, NULL, 0);
	if (ret && status != 2) {
		printf("x1s-ufs: the first request did not come back here (%d)\n", ret);
		return ret;
	}
	/* a unit attention (the flash's "I was reset") is answered by asking again */
	for (int i = 0; ret && i < 3; i++) {
		status = 0xff;
		ret = request(0, tur, 6, 0, &status, NULL, 0);
	}
	if (ret)
		printf("x1s-ufs: LUN 0 not ready after 4 tries (status %02x): taken, the scan will try again\n", status);
	printf("x1s-ufs: taken from S-BOOT (its list was at %08x%08x; this one at %08lx, read only); UFSHCI %x\n",
	       sboot_hi, sboot_lo, mem, hr(VER));
	return 0;
}

static const struct scsi_ops x1s_ufs_ops = {
	.exec		= x1s_ufs_exec,
	.bus_reset	= x1s_ufs_bus_reset,
};

U_BOOT_DRIVER(x1s_ufs) = {
	.name		= "x1s_ufs",
	.id		= UCLASS_SCSI,
	.ops		= &x1s_ufs_ops,
	.probe		= x1s_ufs_probe,
};

/*
 * The storage protector's secure setup (test 39: past the GPT every read ends in a system bus fatal error, IS bit 17,
 * and the controller answers nothing more: S-BOOT leaves the protector's SMU letting through only the blocks it reads
 * itself). Samsung's kernel for the S20 makes these two calls at every UFS host init (drivers/scsi/ufs/ufs-exynos.c
 * exynos_ufs_init -> crypto_sec_cfg -> drivers/crypto/fmp/fmp.c exynos_fmp_sec_cfg; IDs include/linux/smc.h and
 * include/crypto/fmp.h; the phone's tree: fmp-id 0, smu-id 0), through exynos_smc, "smc #0":
 *   SMC_CMD_FMP_SECURITY 0xC2001810, 0, fmp-id 0, CFG_DESCTYPE_3 (the PRDT entries of 128 bytes this driver uses)
 *   SMC_CMD_SMU          0xC2001850, SMU_INIT 0, SMU_EMBEDDED 0, 0
 * Secure-world settings of the protector only, made afresh at every boot; nothing on the flash is written.
 */
static int secure_open(void)
{
	static const ulong calls[2][4] = {
		{ 0xC2001810UL, 0, 0, 3 },	/* FMP security: descriptor type 3 */
		{ 0xC2001850UL, 0, 0, 0 },	/* SMU init, the embedded UFS */
	};
	static int done;
	struct pt_regs regs;
	int i;

	if (done)
		return 0;
	for (i = 0; i < 2; i++) {
		memset(&regs, 0, sizeof(regs));
		regs.regs[0] = calls[i][0];
		regs.regs[1] = calls[i][1];
		regs.regs[2] = calls[i][2];
		regs.regs[3] = calls[i][3];
		smc_call(&regs);
		printf("x1s-ufs: secure call %lx (%lx, %lx, %lx): %ld\n", calls[i][0], calls[i][1], calls[i][2],
		       calls[i][3], (long)(int)regs.regs[0]);
	}
	done = 1;
	return 0;
}

/* "x1sufs": bind and take the controller once, find the units (scsi scan), list LUN 0's partitions */
static int do_x1sufs(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct udevice *dev;
	int ret;

	if (uclass_find_device_by_name(UCLASS_SCSI, "x1s_ufs", &dev)) {
		ret = device_bind_driver(dm_root(), "x1s_ufs", "x1s_ufs", &dev);
		if (ret) {
			printf("x1s-ufs: bind failed (%d)\n", ret);
			return CMD_RET_FAILURE;
		}
	}
	ret = device_probe(dev);
	if (ret)
		return CMD_RET_FAILURE;
	/*
	 * "x1sufs wake": S-BOOT leaves the flash asleep (test 36: every unit answers NOT READY, "initializing command
	 * required", 04/02, even to INQUIRY). The one command that wakes it, as Linux sends at every resume and boot
	 * (ufshcd_set_dev_pwr_mode): START STOP UNIT with POWER CONDITION 1 (active) to the UFS Device well-known unit
	 * (UPIU LUN 0xD0), CDB 1B 00 00 00 10 00, no data. It changes a power state only; nothing on the flash is written.
	 * Run only when asked for (the owner's rule for power steps); asked again on a unit attention, at most 4 times.
	 */
	if (argc > 1 && !strcmp(argv[1], "wake")) {
		static const u8 ssu[6] = { 0x1b, 0, 0, 0, 0x10, 0 };
		u8 status = 0xff;

		for (int i = 0; i < 4; i++) {
			status = 0xff;
			printf("x1s-ufs: START STOP UNIT (active) to the UFS Device unit, try %d\n", i + 1);
			ret = request(0xd0, ssu, sizeof(ssu), 0, &status, NULL, 0);
			if (!ret)
				break;
		}
		printf("x1s-ufs: wake: %s (status %02x)\n", ret ? "FAILED" : "the flash is active", status);
		if (ret)
			return CMD_RET_FAILURE;
		mdelay(10);
		secure_open();
	} else if (argc > 1 && !strcmp(argv[1], "writes")) {
		/*
		 * "x1sufs writes": the write window on, after the units are found (a scan before, by "x1sufs wake"):
		 * USERDATA by name in LUN 0's GPT, where the PIT puts it, at least VOL_BLOCKS long, blocks of 4096.
		 */
		struct blk_desc *desc = blk_get_dev("scsi", 0);
		struct disk_partition info;

		if (!desc || desc->blksz != 4096 || desc->lun != 0 ||
		    part_get_info_by_name(desc, "userdata", &info) < 0) {
			printf("x1s-ufs: writes stay off: no userdata on LUN 0\n");
			return CMD_RET_FAILURE;
		}
		if (info.start != USERDATA_PIT_START || info.size < VOL_BLOCKS || info.blksz != 4096) {
			printf("x1s-ufs: writes stay off: userdata is at %llu, %llu blocks of %lu (the PIT says %llu, at "
			       "least %llu of 4096)\n", (u64)info.start, (u64)info.size, info.blksz, USERDATA_PIT_START,
			       VOL_BLOCKS);
			return CMD_RET_FAILURE;
		}
		wr_first = info.start;
		wr_count = VOL_BLOCKS;
		printf("x1s-ufs: writes ON for LUN 0 blocks %llu..%llu (the first 110 GB of userdata), with FUA; "
		       "everything else read only\n", wr_first, wr_first + wr_count - 1);
		return CMD_RET_SUCCESS;
	} else if (argc > 1 && !strcmp(argv[1], "stats")) {
		printf("x1s-ufs: %lu reads, %lu writes, %lu refused\n", reads, writes, refused);
		return CMD_RET_SUCCESS;
	}
	ret = scsi_scan(true);
	if (ret) {
		printf("x1s-ufs: scan failed (%d)\n", ret);
		return CMD_RET_FAILURE;
	}
	return run_command("part list scsi 0", 0) ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

U_BOOT_CMD(x1sufs, 2, 0, do_x1sufs,
	   "the phone's storage, read only: take it from S-BOOT, find its units, list LUN 0's partitions",
	   "[wake | writes | stats]  (wake: the flash active and the protector open, then the scan; writes: the 110 GB window of userdata writable)");

/*
 * Milestone 5 probe: can we talk to the internal UFS that ABL booted from?
 *
 * Runs with the MMU off (all memory Device, so DMA is coherent with no cache
 * maintenance). Dumps the UFSHCI registers ABL left behind and, if the host
 * is enabled and the link is up, points the transfer request list at our own
 * buffers and issues READ-ONLY SCSI commands on LUN 0. Nothing is ever
 * written to the device: there is no SCSI WRITE in this file.
 *
 * Register layout and descriptor formats: JEDEC UFSHCI (JESD223).
 */
#include <stdint.h>
/* Adapted from dipa-studio/ios-sweet (MIT), see licenses/ios-sweet-MIT.txt. */
extern void begonia_emit_text(const char *);
extern void begonia_emit_hex(const char *,uint64_t);


#define UFS_BASE	0x11270000UL	/* ufshc@1d84000, sweet live FDT */

#define REG_CAP		0x00
#define REG_VER		0x08
#define REG_IS		0x20
#define REG_HCS		0x30
#define REG_HCE		0x34
#define REG_UTRLBA	0x50
#define REG_UTRLBAU	0x54
#define REG_UTRLDBR	0x58
#define REG_UTRLCLR	0x5c
#define REG_UTRLRSR	0x60

#define HCS_DP		(1u << 0)
#define HCS_UTRLRDY	(1u << 1)
#define HCS_UCRDY	(1u << 3)

#define BLOCK		4096u

static inline uint32_t ufs_rd(uint32_t off) { return *(volatile uint32_t *)(UFS_BASE + off); }
static inline void ufs_wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(UFS_BASE + off) = v; }

/* UTP transfer request descriptor (32 bytes). */
struct utrd {
	uint32_t dw0;		/* [31:28] CT, [26:25] DD, [24] I */
	uint32_t dw1;
	uint32_t dw2;		/* [7:0] OCS */
	uint32_t dw3;
	uint32_t ucdba;		/* command descriptor base, 128-byte aligned */
	uint32_t ucdbau;
	uint32_t resp;		/* [31:16] response UPIU offset, [15:0] length (DWORDs) */
	uint32_t prdt;		/* [31:16] PRDT offset, [15:0] entries (DWORDs/entries) */
};

/* UTP command descriptor: command UPIU, response UPIU, PRDT. */
struct ucd {
	uint8_t cmd[512];
	uint8_t resp[512];
	struct {
		uint32_t addr_lo, addr_hi, rsvd, dbc;	/* dbc = byte count - 1 */
	} prdt[1];
};

static struct utrd utrl[32] __attribute__((aligned(1024)));
static struct ucd ucd __attribute__((aligned(128)));
static uint8_t data[2 * BLOCK] __attribute__((aligned(4096)));

static uint64_t ticks(void)
{
	uint64_t t;
	__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(t));
	return t;
}

static void zero(void *p, unsigned n)
{
	volatile uint8_t *b = p;
	for (unsigned i = 0; i < n; i++)
		b[i] = 0;
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/*
 * Issue one SCSI command (read direction or no data) in slot 0.
 * Returns 0 on success, otherwise a code describing the failure stage.
 */
static uint32_t scsi(uint8_t lun, const uint8_t *cdb, unsigned cdb_len, void *buf, uint32_t len,
    uint32_t *ocs_out, uint32_t *status_out)
{
	static uint8_t tag;
    if (lun!=2 || (cdb[0]!=0 && cdb[0]!=0x25 && cdb[0]!=0x28) || len>8192) return 9;
	zero(&utrl[0], sizeof(utrl[0]));
	zero(&ucd, sizeof(ucd));

	/* Command UPIU: header (12 bytes), expected length, CDB. */
	ucd.cmd[0] = 0x01;			/* transaction type: COMMAND */
	ucd.cmd[1] = len ? 0x40 : 0x00;		/* flags: R (data in) */
	ucd.cmd[2] = lun;
	ucd.cmd[3] = ++tag;
	ucd.cmd[12] = len >> 24;
	ucd.cmd[13] = len >> 16;
	ucd.cmd[14] = len >> 8;
	ucd.cmd[15] = len;
	for (unsigned i = 0; i < cdb_len; i++)
		ucd.cmd[16 + i] = cdb[i];

	uintptr_t ucd_pa = (uintptr_t)&ucd;
	utrl[0].dw0 = (1u << 28) | ((len ? 2u : 0u) << 25) | (1u << 24);	/* UFS storage, dev->host */
	utrl[0].dw2 = 0x0f;			/* OCS: invalid until the host updates it */
	utrl[0].ucdba = (uint32_t)ucd_pa;
	utrl[0].ucdbau = (uint32_t)(ucd_pa >> 32);
	utrl[0].resp = (512u / 4) << 16 | (512u / 4);
	if (len) {
		uintptr_t buf_pa = (uintptr_t)buf;
		ucd.prdt[0].addr_lo = (uint32_t)buf_pa;
		ucd.prdt[0].addr_hi = (uint32_t)(buf_pa >> 32);
		ucd.prdt[0].dbc = len - 1;
		utrl[0].prdt = (1024u / 4) << 16 | 1;
	}
	__asm__ volatile("dsb sy" ::: "memory");

	ufs_wr(REG_UTRLDBR, 1);
	uint64_t start = ticks();
	while (ufs_rd(REG_UTRLDBR) & 1) {
		if (ticks() - start > 13000000) {	/* 1 s */
			ufs_wr(REG_UTRLCLR, ~1u);	/* clear slot 0 (write 0 to the bit) */
			return 1;			/* timeout */
		}
	}
	__asm__ volatile("dsb sy" ::: "memory");
	*ocs_out = utrl[0].dw2 & 0xff;
	*status_out = ucd.resp[7];		/* SCSI status in the response UPIU */
	if (*ocs_out)
		return 2;
	if (ucd.resp[0] != 0x21)		/* RESPONSE UPIU */
		return 3;
	return *status_out ? 4 : 0;
}


/* Read-only LUN2 probe. Preserves the inherited list when no request is active. */
void ufs_read_probe(void) {
    uint32_t hcs=ufs_rd(REG_HCS),base=ufs_rd(REG_UTRLBA),hi=ufs_rd(REG_UTRLBAU),run=ufs_rd(REG_UTRLRSR);
    begonia_emit_text("BEGONIA UFS: LUN2 read-only command probe\n");
    if (!(ufs_rd(REG_HCE)&1) || (hcs&3)!=3 || ufs_rd(REG_UTRLDBR)) {
        begonia_emit_text("BEGONIA UFS: not idle/ready; no takeover\n");return;
    }
    ufs_wr(REG_UTRLRSR,0);__asm__ volatile("dsb sy" ::: "memory");
    uintptr_t ours=(uintptr_t)utrl;
    ufs_wr(REG_UTRLBA,(uint32_t)ours);ufs_wr(REG_UTRLBAU,(uint32_t)(ours>>32));ufs_wr(REG_UTRLRSR,1);
    __asm__ volatile("dsb sy" ::: "memory");
    if(ufs_rd(REG_UTRLBA)!=(uint32_t)ours || ufs_rd(REG_UTRLBAU)!=(uint32_t)(ours>>32) || !(ufs_rd(REG_UTRLRSR)&1)) goto restore;
    uint32_t ocs=0,status=0,rc;
    uint8_t tur[6]={0};
    for(int i=0;i<3;i++){rc=scsi(2,tur,6,0,0,&ocs,&status);if(!rc)break;}
    begonia_emit_hex("UFS_TUR_RC=",rc);begonia_emit_hex("UFS_TUR_OCS=",ocs);begonia_emit_hex("UFS_TUR_STATUS=",status);
    uint8_t capacity[10]={0x25};rc=scsi(2,capacity,10,data,8,&ocs,&status);
    begonia_emit_hex("UFS_CAP_RC=",rc);
    if(rc || be32(data+4)!=4096)goto restore;
    begonia_emit_hex("UFS_LAST_LBA=",be32(data));begonia_emit_hex("UFS_BLOCK_BYTES=",be32(data+4));
    uint8_t read[10]={0x28,0,0,0,0,0,0,0,2,0};rc=scsi(2,read,10,data,8192,&ocs,&status);
    begonia_emit_hex("UFS_READ_RC=",rc);begonia_emit_hex("UFS_READ_OCS=",ocs);begonia_emit_hex("UFS_READ_STATUS=",status);
    if(!rc) {
        uint64_t hash=0xcbf29ce484222325ULL;
        for(unsigned i=0;i<8192;i++)hash=(hash^data[i])*0x100000001b3ULL;
        begonia_emit_hex("UFS_FIRST_8K_FNV1A=",hash);
        const char *gpt="EFI PART";int valid=1;
        for(int i=0;i<8;i++)if(data[4096+i]!=(uint8_t)gpt[i])valid=0;
        begonia_emit_hex("UFS_GPT_SIGNATURE_VALID=",valid);
    }
restore:
    if(ufs_rd(REG_UTRLDBR)&1){ufs_wr(REG_UTRLCLR,~1u);__asm__ volatile("dsb sy" ::: "memory");}
    ufs_wr(REG_UTRLRSR,0);__asm__ volatile("dsb sy" ::: "memory");
    ufs_wr(REG_UTRLBA,base);ufs_wr(REG_UTRLBAU,hi);ufs_wr(REG_UTRLRSR,run);__asm__ volatile("dsb sy" ::: "memory");
    begonia_emit_text("BEGONIA UFS: inherited request list restored; no media writes\n");
}

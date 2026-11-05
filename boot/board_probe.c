/* SPDX-License-Identifier: MIT
 * Read-only inherited-state inventory. Addresses from the owner's live DTB.
 * No controller commands, clock changes, doorbells or display writes.
 */
#include <stdint.h>
extern void begonia_emit_text(const char *);
extern void begonia_emit_hex(const char *,uint64_t);
static uint32_t rd(uint64_t base,uint32_t offset) { return *(volatile uint32_t *)(base+offset); }
void board_probe(void) {
    const uint64_t ufs=0x11270000,ovl=0x14008000;
    begonia_emit_text("BEGONIA BOARD: read-only inherited UFS/OVL registers\n");
    begonia_emit_hex("UFS_CAP=",rd(ufs,0));
    begonia_emit_hex("UFS_VER=",rd(ufs,8));
    begonia_emit_hex("UFS_HCS=",rd(ufs,0x30));
    begonia_emit_hex("UFS_HCE=",rd(ufs,0x34));
    begonia_emit_hex("UFS_UTRLBA=",rd(ufs,0x50));
    begonia_emit_hex("UFS_UTRLBAU=",rd(ufs,0x54));
    begonia_emit_hex("UFS_UTRLDBR=",rd(ufs,0x58));
    begonia_emit_hex("UFS_UTRLRSR=",rd(ufs,0x60));
    begonia_emit_hex("OVL_SRC_CON=",rd(ovl,0x2c));
    begonia_emit_hex("OVL_L0_CON=",rd(ovl,0x30));
    begonia_emit_hex("OVL_L0_PITCH=",rd(ovl,0x44));
    begonia_emit_hex("OVL_L0_ADDR=",rd(ovl,0xf40));
    for (uint32_t layer=1;layer<4;layer++) {
        begonia_emit_hex("OVL_LAYER_INDEX=",layer);
        begonia_emit_hex("OVL_LAYER_CON=",rd(ovl,0x30+layer*0x20));
        begonia_emit_hex("OVL_LAYER_SIZE=",rd(ovl,0x38+layer*0x20));
        begonia_emit_hex("OVL_LAYER_PITCH=",rd(ovl,0x44+layer*0x20));
        begonia_emit_hex("OVL_LAYER_ADDR=",rd(ovl,0xf40+layer*0x20));
    }
#ifdef UFS_READ_PROBE
    extern void ufs_read_probe(void);
    ufs_read_probe();
#endif
    begonia_emit_text("BEGONIA BOARD: inventory complete\n");
}

void board_fault_report(uint64_t esr,uint64_t far) {
    begonia_emit_text("BEGONIA BOARD: exception; aborting inventory and returning to Linux\n");
    begonia_emit_hex("BOARD_ESR=",esr);begonia_emit_hex("BOARD_FAR=",far);
}

#ifndef HW_REGS_H
#define HW_REGS_H

/**
 * @file hw_regs.h
 * @brief Named Nintendo DS ARM9 hardware registers used during final handover.
 *
 * Only low-level boot code should include this header. These volatile lvalues
 * intentionally expose raw hardware side effects and must not be used from UI or
 * filesystem code.
 */

#include "types.h"

#define HW_REG8(addr)   (*(volatile u8*)(addr))
#define HW_REG16(addr)  (*(volatile u16*)(addr))
#define HW_REG32(addr)  (*(volatile u32*)(addr))

#define BOOT_REG_IME          HW_REG32(0x04000208u)
#define BOOT_REG_IE           HW_REG32(0x04000210u)
#define BOOT_REG_IF           HW_REG32(0x04000214u)

#define BOOT_REG_DMA0_CR      HW_REG16(0x040000bau)
#define BOOT_REG_DMA1_CR      HW_REG16(0x040000c6u)
#define BOOT_REG_DMA2_CR      HW_REG16(0x040000d2u)
#define BOOT_REG_DMA3_CR      HW_REG16(0x040000deu)
#define BOOT_REG_TIMER0_CR    HW_REG16(0x04000102u)
#define BOOT_REG_TIMER1_CR    HW_REG16(0x04000106u)
#define BOOT_REG_TIMER2_CR    HW_REG16(0x0400010au)
#define BOOT_REG_TIMER3_CR    HW_REG16(0x0400010eu)
#define BOOT_REG_EXMEMCNT     HW_REG32(0x04001000u)

#define BOOT_REG_IPCSYNC      HW_REG16(0x04000180u)
#define BOOT_REG_IPCFIFOCNT   HW_REG32(0x04000184u)
#define BOOT_REG_IPCFIFORECV  HW_REG32(0x04100000u)

#define BOOT_REG_SCFG_MC      HW_REG8(0x04000247u)
#define BOOT_REG_SCFG_CLK     HW_REG16(0x04004004u)
#define BOOT_REG_SCFG_EXT     HW_REG32(0x04004008u)
#define BOOT_REG_TWL_CLEAR_BEGIN ((volatile u32*)0x04004104u)
#define BOOT_REG_TWL_CLEAR_END   ((volatile u32*)0x04004170u)

#define EXMEMCNT_CART_IRQ_ENABLE     0x00010000u
#define IPCFIFOCNT_CLEAR_AND_DISABLE 0x0000c008u
#define IPCFIFOCNT_RECV_EMPTY        0x00000100u
#define SCFG_MC_NTR_MODE             3u
#define SCFG_EXT_NTR_PREPARE         (0x83000000u | (1u << 13))
#define SCFG_EXT_NTR_ARM9_MASK       ((1u << 13) | (1u << 31))
#define IPCSYNC_ARM9_RELEASE         0x0200u
#define IPCSYNC_ARM9_ACK             0x0100u
#define IPCSYNC_REMOTE_MASK          0x000fu
#define IPCSYNC_REMOTE_ARM7_ACK      1u

#endif

    @ ARM7 boot handover stub for launching a saved DS Download Play child.
    @
    @ The interrupt/PXI cleanup and final ARM7/ARM9 handoff sequence are
    @ Pico-Loader-derived boot knowledge, rewritten for this dumper's fixed
    @ memory contract and status words. See LICENSE and THIRD_PARTY_NOTICES.md
    @ for the GPL project license and the Pico-Loader notice.
    .section .text.boot_stub, "ax", %progbits
    .arm
    .align 2

    .global arm7BootStubStart
    .global arm7BootStubEnd

    .equ A7_REG_IME,                 0x04000208
    .equ A7_REG_IE,                  0x04000210
    .equ A7_REG_IF,                  0x04000214
    .equ A7_REG_DMA0_CR,             0x040000ba
    .equ A7_REG_DMA1_CR,             0x040000c6
    .equ A7_REG_DMA2_CR,             0x040000d2
    .equ A7_REG_DMA3_CR,             0x040000de
    .equ A7_REG_TIMER0_CR,           0x04000102
    .equ A7_REG_TIMER1_CR,           0x04000106
    .equ A7_REG_TIMER2_CR,           0x0400010a
    .equ A7_REG_TIMER3_CR,           0x0400010e
    .equ A7_REG_IPCSYNC,             0x04000180
    .equ A7_REG_IPCFIFOCNT,          0x04000184
    .equ A7_REG_IPCFIFORECV,         0x04100000
    .equ A7_REG_EXMEM_BASE,          0x04000000
    .equ A7_REG_WRAMCNT,             0x04004000
    .equ A7_REG_SCFG_EXT,            0x04004008
    .equ A7_REG_SCFG_A9_ROM,         0x04004c04

    .equ A7_STATUS_COPIED,           0x44504137
    .equ A7_STATUS_NTR_SWITCH,       0x44504e54
    .equ A7_STATUS_RELEASE,          0x44504254
    .equ A7_STATUS_LAUNCH,           0x44504c41
    .equ A7_STATUS_NTR_READY,        0x44504e37

    .equ A7_IPCSYNC_RELEASE,         0x0200
    .equ A7_IPCSYNC_ACK,             0x0100
    .equ A7_IPCSYNC_REMOTE_MASK,     0x000f
    .equ A7_IPCSYNC_ARM9_RELEASE,    2
    .equ A7_IPCSYNC_ARM9_ACK,        1
    .equ A7_IPCFIFOCNT_CLEAR,        0x0000c008
    .equ A7_IPCFIFOCNT_RECV_EMPTY,   0x100

    .equ A7_CLEAR_MAIN_LO,           0x02300000
    .equ A7_CLEAR_MAIN_HI,           0x023fe800
    .equ A7_CLEAR_WRAM_LO,           0x037f8000
    .equ A7_CLEAR_WRAM_HI,           0x0380f600
    .equ A7_STACK_IRQ,               0x0380ffc0
    .equ A7_STACK_SVC,               0x0380ff80
    .equ A7_STACK_USER_TOP,          0x0380ff80
    .equ A7_STACK_USER_SIZE,         0x200
    .equ A7_CLEAR_USER_LO,           0x0380fa00
    .equ A7_CLEAR_USER_HI,           0x03810000
    .equ A7_CLEAR_STUB_LO,           0x0380f600
    .equ A7_CLEAR_STUB_HI,           0x0380f6f8
    .equ A7_CLEAR_PARAM_LO,          0x027ff000
    .equ A7_CLEAR_PARAM_HI,          0x027ff800
    .equ A7_CLEAR_HEADER_LO,         0x027ffd80
    .equ A7_CLEAR_HEADER_HI,         0x027ffe00
    .equ A7_CLEAR_END_LO,            0x027fff80
    .equ A7_CLEAR_END_HI,            0x02800000
    .equ A7_CPU_MODE_IRQ,            0xd2
    .equ A7_CPU_MODE_SVC,            0xd3
    .equ A7_CPU_MODE_SYSTEM,         0xdf
    .equ A7_SCFG_EXT_NTR_VALUE,      0x12a03000
    .equ A7_CONTROL_SECTION0_OFF,      12
    .equ A7_CONTROL_SECTION_BYTES,     16
    .equ A7_LOOP_DECREMENT,            1
    .equ A7_SCFG_A9_ROM_ENABLE,        0x0100
    .equ A7_WRAMCNT_ARM7_OFF,          1

    @ Entry point copied to ARM7 WRAM and called by ARM9.
    @ Inputs: r0 = public DownloadRsaFrame at fixed control address,
    @         r1 = shared ARM7 status word.
    @ Clobbers: all general-purpose registers before jumping to child ARM7.
    @ Side effects: masks IRQs, disables DMA/timers and signals COPIED.
arm7BootStubStart:
    ldr r2, =A7_REG_IME
    mov r3, #0
    str r3, [r2]
    ldr r2, =A7_REG_IE
    str r3, [r2]
    ldr r2, =A7_REG_IF
    mvn r3, #0
    str r3, [r2]
    mov r3, #0
    ldr r2, =A7_REG_DMA0_CR
    strh r3, [r2]
    ldr r2, =A7_REG_DMA1_CR
    strh r3, [r2]
    ldr r2, =A7_REG_DMA2_CR
    strh r3, [r2]
    ldr r2, =A7_REG_DMA3_CR
    strh r3, [r2]
    ldr r2, =A7_REG_TIMER0_CR
    strh r3, [r2]
    ldr r2, =A7_REG_TIMER1_CR
    strh r3, [r2]
    ldr r2, =A7_REG_TIMER2_CR
    strh r3, [r2]
    ldr r2, =A7_REG_TIMER3_CR
    strh r3, [r2]

    mov r8, r0
    mov r10, r1
    ldr r2, =A7_STATUS_COPIED
    str r2, [r10]

    @ Waits for ARM9 to choose either direct release or NTR-mode switch.
    @ Inputs: r8 = control frame, r10 = shared status word.
    @ Clobbers: r2-r3.
    @ Side effects: branches to SCFG setup on NTR request.
arm7BootWaitRelease:
    ldr r3, [r10]
    ldr r2, =A7_STATUS_NTR_SWITCH
    cmp r3, r2
    beq arm7BootHandleNtrSwitch
    ldr r2, =A7_STATUS_RELEASE
    cmp r3, r2
    bne arm7BootWaitRelease
    ldr r2, =A7_REG_IPCSYNC
    mov r3, #A7_IPCSYNC_RELEASE
    strh r3, [r2]

    @ Waits until ARM9 exposes IPCSYNC phase 2 after release.
    @ Inputs: r2 = IPCSYNC register address.
    @ Clobbers: r3.
    @ Side effects: none until the remote phase is observed.
arm7BootWaitArm9Phase2:
    ldrh r3, [r2]
    and r3, r3, #A7_IPCSYNC_REMOTE_MASK
    cmp r3, #A7_IPCSYNC_ARM9_RELEASE
    bne arm7BootWaitArm9Phase2

    bl arm7BootClearRanges
    bl arm7BootClearPxi
    ldr r2, =A7_REG_IPCSYNC
    mov r3, #A7_IPCSYNC_ACK
    strh r3, [r2]
    ldr r2, =A7_STATUS_LAUNCH
    str r2, [r10]

    @ Waits until ARM9 acknowledges that PXI and memory cleanup are complete.
    @ Inputs: none beyond fixed IPCSYNC register.
    @ Clobbers: r2-r3.
    @ Side effects: none until the remote ACK phase is observed.
arm7BootWaitArm9Phase1:
    ldr r2, =A7_REG_IPCSYNC
    ldrh r3, [r2]
    and r3, r3, #A7_IPCSYNC_REMOTE_MASK
    cmp r3, #A7_IPCSYNC_ARM9_ACK
    bne arm7BootWaitArm9Phase1

    bl arm7BootPrepareCpuForChild
    ldr r2, =A7_REG_IPCSYNC
    mov r3, #0
    strh r3, [r2]

    @ Waits for ARM9 to clear its ACK before jumping into the child image.
    @ Inputs: r2 = IPCSYNC register address, r8 = control frame.
    @ Clobbers: r0-r12 and lr during final register restore.
    @ Side effects: restores EXMEM-backed registers and branches to ARM7 entry.
arm7BootWaitArm9Clear:
    ldrh r3, [r2]
    and r3, r3, #A7_IPCSYNC_REMOTE_MASK
    cmp r3, #A7_IPCSYNC_ARM9_ACK
    beq arm7BootWaitArm9Clear

    ldr r12, [r8, #4]
    mov lr, r12
    mov r11, #A7_REG_EXMEM_BASE
    ldmdb r11, {r0-r10}
    mov r11, #0
    bx r12

    @ Clears PXI FIFO state so the child starts with an empty IPC channel.
    @ Inputs: none.
    @ Clobbers: r2-r4.
    @ Side effects: disables/clears IPC FIFO control and drains pending words.
arm7BootClearPxi:
    ldr r2, =A7_REG_IPCFIFOCNT
    ldr r3, =A7_IPCFIFOCNT_CLEAR
    str r3, [r2]
    ldr r4, =A7_REG_IPCFIFORECV

    @ Drains IPC receive FIFO until the hardware empty bit is set.
    @ Inputs: r2 = IPCFIFOCNT, r4 = IPCFIFORECV.
    @ Clobbers: r3.
    @ Side effects: consumes all queued IPC words.
arm7BootClearPxiLoop:
    ldr r3, [r2]
    tst r3, #A7_IPCFIFOCNT_RECV_EMPTY
    bne arm7BootClearPxiDone
    ldr r3, [r4]
    b arm7BootClearPxiLoop

    @ Finishes PXI cleanup by clearing IPCSYNC and returning to caller.
    @ Inputs: lr = caller return address.
    @ Clobbers: r2-r3.
    @ Side effects: IPCSYNC local bits become zero.
arm7BootClearPxiDone:
    ldr r2, =A7_REG_IPCSYNC
    mov r3, #0
    strh r3, [r2]
    bx lr

    @ Clears ARM7-visible RAM ranges while preserving loaded child sections.
    @ Inputs: r8 = control frame with section descriptors, lr = return address.
    @ Clobbers: r0-r7, r11.
    @ Side effects: zero-fills main RAM and WRAM gaps outside child sections.
arm7BootClearRanges:
    mov r11, lr
    ldr r2, =A7_CLEAR_MAIN_LO
    ldr r3, =A7_CLEAR_MAIN_HI
    bl arm7BootZeroWordsExceptSections
    ldr r2, =A7_CLEAR_WRAM_LO
    ldr r3, =A7_CLEAR_WRAM_HI
    bl arm7BootZeroWordsExceptSections
    bx r11

    @ Reinitializes ARM7 CPU modes, stacks and memory before child entry.
    @ Inputs: r8 = control frame, lr = return address.
    @ Clobbers: r0-r7, r11, CPSR/SPSR for IRQ/SVC/System modes.
    @ Side effects: clears user/parameter/header regions outside child sections.
arm7BootPrepareCpuForChild:
    mov r11, lr
    mov r0, #A7_CPU_MODE_SVC
    msr CPSR_fsxc, r0
    ldr sp, =A7_STACK_IRQ
    mov lr, #0
    msr SPSR_fsxc, lr
    mov r0, #A7_CPU_MODE_IRQ
    msr CPSR_fsxc, r0
    ldr sp, =A7_STACK_SVC
    mov lr, #0
    msr SPSR_fsxc, lr
    mov r0, #A7_CPU_MODE_SYSTEM
    msr CPSR_fsxc, r0
    ldr r2, =A7_STACK_SVC
    sub sp, r2, #A7_STACK_USER_SIZE
    ldr r2, =A7_CLEAR_USER_LO
    ldr r3, =A7_CLEAR_USER_HI
    bl arm7BootZeroWordsExceptSections
    ldr r2, =A7_CLEAR_WRAM_HI
    ldr r3, =A7_CLEAR_STUB_HI
    bl arm7BootZeroWordsExceptSections
    ldr r2, =A7_CLEAR_PARAM_LO
    ldr r3, =A7_CLEAR_PARAM_HI
    bl arm7BootZeroWordsExceptSections
    ldr r2, =A7_CLEAR_HEADER_LO
    ldr r3, =A7_CLEAR_HEADER_HI
    bl arm7BootZeroWordsExceptSections
    ldr r2, =A7_CLEAR_END_LO
    ldr r3, =A7_CLEAR_END_HI
    bl arm7BootZeroWordsExceptSections
    bx r11

    @ Zero-fills a word range, skipping all RSA-described child sections.
    @ Inputs: r2 = start, r3 = end, r8 = control frame.
    @ Clobbers: r0-r7.
    @ Side effects: stores zero to every non-section word in [r2, r3).
arm7BootZeroWordsExceptSections:
    mov r4, #0

    @ Advances through the current clear range one word at a time.
    @ Inputs: r2 = current address, r3 = end address.
    @ Clobbers: r0-r7.
    @ Side effects: delegates each address to section-overlap checks.
arm7BootZeroLoop:
    cmp r2, r3
    bhs arm7BootZeroDone
    add r7, r8, #A7_CONTROL_SECTION0_OFF
    mov r6, #3

    @ Compares the current clear address against each child section range.
    @ Inputs: r2 = current address, r7 = section cursor, r6 = remaining count.
    @ Clobbers: r0-r1, r6-r7.
    @ Side effects: branches to skip or zero the current word.
arm7BootZeroCheckLoop:
    ldr r0, [r7, #4]
    ldr r1, [r7, #8]
    add r1, r0, r1
    cmp r2, r0
    blo arm7BootZeroCheckNext
    cmp r2, r1
    blo arm7BootZeroSkipStore

    @ Moves the section cursor to the next RSA section descriptor.
    @ Inputs: r7 = current descriptor, r6 = remaining descriptors.
    @ Clobbers: r6-r7.
    @ Side effects: falls through to zero when no section contains r2.
arm7BootZeroCheckNext:
    add r7, r7, #A7_CONTROL_SECTION_BYTES
    subs r6, r6, #A7_LOOP_DECREMENT
    bne arm7BootZeroCheckLoop
    str r4, [r2], #4
    b arm7BootZeroLoop

    @ Skips zeroing because the current address belongs to a child section.
    @ Inputs: r2 = current address.
    @ Clobbers: r2.
    @ Side effects: advances to the next word without modifying memory.
arm7BootZeroSkipStore:
    add r2, r2, #4
    b arm7BootZeroLoop

    @ Returns after an entire clear range has been processed.
    @ Inputs: lr = caller return address.
    @ Clobbers: none beyond helper scratch registers already documented.
    @ Side effects: none.
arm7BootZeroDone:
    bx lr

    @ Switches DSi-capable hardware into the NTR-compatible child layout.
    @ Inputs: r10 = shared status word.
    @ Clobbers: r2-r3.
    @ Side effects: changes SCFG/WRAMCNT, signals NTR_READY, then waits again.
arm7BootHandleNtrSwitch:
    ldr r2, =A7_REG_SCFG_A9_ROM
    ldrh r3, [r2]
    orr r3, r3, #A7_SCFG_A9_ROM_ENABLE
    strh r3, [r2]

    ldr r2, =A7_REG_WRAMCNT
    mov r3, #3
    strb r3, [r2]
    mov r3, #7
    strb r3, [r2, #A7_WRAMCNT_ARM7_OFF]

    ldr r2, =A7_REG_SCFG_EXT
    ldr r3, =A7_SCFG_EXT_NTR_VALUE
    str r3, [r2]

    ldr r2, =A7_STATUS_NTR_READY
    str r2, [r10]
    b arm7BootWaitRelease

    .align 2
    .ltorg

    @ End marker used by ARM9 to copy exactly the stub byte range.
    @ Inputs: none.
    @ Clobbers: none.
    @ Side effects: none; this label is never executed.
arm7BootStubEnd:

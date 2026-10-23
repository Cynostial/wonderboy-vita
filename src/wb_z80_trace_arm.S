/* ApplyHacksMain resets trace metadata for both patched ROMs every frame.
 * Original: trace[i] &= 0x80 for all 49,152 bytes. Keep exactly that state
 * transition, including the already executed bit, using sixteen-byte loads.
 * r5 is CHacks; r12 is ROM slot (0/1). Restore all vector registers, r0,
 * r4-r12, lr, sp and the original loop's final flags. r1-r3 deliberately
 * match the values left by the replaced loop at its continuation. */
.syntax unified
.arch armv7-a
.fpu neon
.thumb
.text
.align 2
.global wb_z80_trace_entry
.type wb_z80_trace_entry, %function
.thumb_func
wb_z80_trace_entry:
    push {r0, lr}
    vpush {d0-d3}
    add r1, r12, r12, lsl #1
    add r1, r5, r1, lsl #14
    adds r1, #16
    mov r2, r1
    add r3, r1, #0xc000
    vmov.i8 q0, #0x80
1:
    vld1.8 {d2-d3}, [r2]
    vand q1, q1, q0
    vst1.8 {d2-d3}, [r2]!
    cmp r2, r3
    bne 1b
    ldrb r3, [r2, #-1]
    movw r2, #0xc000
    vpop {d0-d3}
    cmp r2, #0xc000
    /* LR was never changed. Reuse its stack slot for the continuation PC. */
    ldr r0, 2f
    ldr r0, [r0]
    str r0, [sp, #4]
    pop {r0, pc}
.align 2
2: .word wb_z80_trace_resume
.size wb_z80_trace_entry, .-wb_z80_trace_entry
.section .note.GNU-stack,"",%progbits

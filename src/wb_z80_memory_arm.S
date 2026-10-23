/* Hot byte fetch used for every emulated opcode and operand. Preserve the
 * original mapping exactly; this saves generic Thumb pointer arithmetic and
 * double comparisons on fixed-ROM addresses. Only caller-saved r0-r3 change. */
.syntax unified
.arch armv7-a
.thumb
.text
.align 2
.global wb_z80_memory_read
.type wb_z80_memory_read, %function
.thumb_func
wb_z80_memory_read:
    cmp r1, #0x8000
    bhs 1f
    ldr r2, 3f
    ldr r2, [r2]
    ldr r2, [r2]
    ldrb r0, [r2, r1]
    bx lr
1:
    cmp r1, #0xc000
    bhs 2f
    add r0, r0, #0xa4000
    ldrb r3, [r0, #0x20c]
    ubfx r1, r1, #0, #14
    orr r1, r1, r3, lsl #14
    ldr r2, 3f
    ldr r2, [r2]
    ldr r2, [r2]
    ldrb r0, [r2, r1]
    bx lr
2:
    add r0, r0, #0x18000
    ldr r2, [r0, #0xd0]
    ubfx r1, r1, #0, #13
    ldrb r0, [r2, r1]
    bx lr
.align 2
3: .word wb_z80_memory_rom
.size wb_z80_memory_read, .-wb_z80_memory_read
.section .note.GNU-stack,"",%progbits

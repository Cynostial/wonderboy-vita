.syntax unified
.fpu neon
.text
.align 2
.global wb_water_entry
.type wb_water_entry, %function
wb_water_entry:
    /* Original frame: grid pointer at sp+0x30, time*speed in s18,
     * scaled time in s20. Preserve the original function's live state. */
    push {r0-r12, lr}
    vpush {d0-d7}
    ldr r0, [sp, #168]
    vmov r1, s18
    vmov r2, s20
    mov r3, r11
    bl wb_water_fill
    vpop {d0-d7}
    pop {r0-r12, lr}
    ldr r12, .Lwater_resume
    ldr pc, [r12]
.align 2
.Lwater_resume:
    .word wb_water_resume
.size wb_water_entry, .-wb_water_entry
.section .note.GNU-stack,"",%progbits

/* Leave the original afterPaint accounting and swap intact. With a working
 * native VBlank wait, do not also sleep toward the guest's 63 Hz deadline.
 * Without VSync replay the displaced instructions and original wait loop. */
.syntax unified
.arch armv7-a
.thumb
.text
.align 2
.global wb_pacing_entry
.type wb_pacing_entry, %function
.thumb_func
wb_pacing_entry:
    push {r2,lr}
    ldr r2, 1f
    ldr r2, [r2]
    dmb ish
    cbz r2, 3f
    /* Keep the fallback's previous deadline fresh if VSync is disabled. */
    str r11, [r6,#20]
    ldr r2, 2f
    ldr r2, [r2]
    str r2, [sp,#4]
    pop {r2,pc}
3:
    ldr r0, [r6,#20]
    movw r1, #15833
    adds r4,r0,r1
    ldr r2, 4f
    ldr r2, [r2]
    str r2, [sp,#4]
    pop {r2,pc}
.align 2
1: .word wb_pacing_vblank
2: .word wb_pacing_skip
4: .word wb_pacing_resume
.size wb_pacing_entry, .-wb_pacing_entry
.section .note.GNU-stack,"",%progbits

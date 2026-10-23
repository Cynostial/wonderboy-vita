.syntax unified
.cpu cortex-a9
.fpu neon
.arm
.text
.align 2
.global wb_guest_setjmp
.type wb_guest_setjmp,%function
wb_guest_setjmp:
    add r2,r0,#76
    stmia r2,{r4-r11,sp,lr}
    add r2,r0,#8
    vstmia r2,{d8-d15}
    vmrs r2,fpscr
    str r2,[r0,#72]
    movw r2,#0x4a4d
    movt r2,#0x5742
    str r2,[r0]
    mov r2,#0
    str r2,[r0,#4]
    mov r0,#0
    bx lr
.size wb_guest_setjmp,.-wb_guest_setjmp
.global wb_guest_longjmp
.type wb_guest_longjmp,%function
wb_guest_longjmp:
    ldr r2,[r0]
    movw r3,#0x4a4d
    movt r3,#0x5742
    cmp r2,r3
    bne abort
    add r2,r0,#8
    vldmia r2,{d8-d15}
    ldr r2,[r0,#72]
    vmsr fpscr,r2
    add r2,r0,#76
    ldmia r2,{r4-r11,sp,lr}
    cmp r1,#0
    moveq r0,#1
    movne r0,r1
    bx lr
.size wb_guest_longjmp,.-wb_guest_longjmp
.section .note.GNU-stack,"",%progbits

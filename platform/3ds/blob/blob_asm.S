@ Bloque del juego (ARM big-endian BE8) para 3DS: entrada, cambio de contexto y trap de codigo MIPS.
    .arm
    .section .text.start, "ax"
    .global _blob_start
_blob_start:
    b       ps_blob_entry

    .text
@ ---- Fibras: PsArmCtx = r4-r11 (0..31), sp (32), lr (36), d8-d15 (40..103), fpscr (104) ----
    .global ps_arm_ctx_switch
    .type ps_arm_ctx_switch, %function
@ void ps_arm_ctx_switch(PsArmCtx* save, PsArmCtx* load)
ps_arm_ctx_switch:
    stmia   r0!, {r4-r11}
    str     sp, [r0], #4
    str     lr, [r0], #4
    vstmia  r0!, {d8-d15}
    vmrs    r2, fpscr
    str     r2, [r0]
    ldmia   r1!, {r4-r11}
    ldr     sp, [r1], #4
    ldr     lr, [r1], #4
    vldmia  r1!, {d8-d15}
    ldr     r2, [r1]
    vmsr    fpscr, r2
    bx      lr

    .global ps_arm_fiber_trampoline
    .type ps_arm_fiber_trampoline, %function
@ Primer "retorno" de una fibra nueva: r4 = PsFiber*
ps_arm_fiber_trampoline:
    mov     r0, r4
    bl      ps_arm_fiber_main
1:  b       1b

    .global ps_arm_get_fpscr
ps_arm_get_fpscr:
    vmrs    r0, fpscr
    bx      lr

    .global ps_arm_set_fpscr
ps_arm_set_fpscr:
    vmsr    fpscr, r0
    bx      lr

@ ---- Trap: el juego llamo a codigo MIPS dentro de un asset (pool sin permiso de ejecucion).
@ El host atrapa el prefetch abort y reanuda aqui con r12 = direccion llamada; r0/r1 = argumentos,
@ lr intacto (regreso al llamador original).
    .global ps_mips_trap_shim
ps_mips_trap_shim:
    setend  be              @ el host reanuda en modo LE: los datos del juego son big-endian
    push    {r4, lr}
    mov     r2, r1
    mov     r1, r0
    mov     r0, r12
    bl      ps_call_fragment_func
    pop     {r4, pc}

@ ---- Division de 64 bits (ARMv6 no tiene division por hardware) ----
@ __aeabi_uldivmod / __aeabi_ldivmod: cociente en r0:r1, residuo en r2:r3
    .global __aeabi_uldivmod
__aeabi_uldivmod:
    push    {r4, lr}
    sub     sp, sp, #24
    add     r4, sp, #8
    str     r4, [sp]
    add     r4, sp, #16
    str     r4, [sp, #4]
    bl      ps_uldivmod
    ldrd    r0, r1, [sp, #8]
    ldrd    r2, r3, [sp, #16]
    add     sp, sp, #24
    pop     {r4, pc}

    .global __aeabi_ldivmod
__aeabi_ldivmod:
    push    {r4, lr}
    sub     sp, sp, #24
    add     r4, sp, #8
    str     r4, [sp]
    add     r4, sp, #16
    str     r4, [sp, #4]
    bl      ps_ldivmod
    ldrd    r0, r1, [sp, #8]
    ldrd    r2, r3, [sp, #16]
    add     sp, sp, #24
    pop     {r4, pc}

@ Banco de pruebas del bloque del 3DS: corre el MISMO codigo ARM BE8 bajo qemu-armeb (Linux, sin libc).
    .arm
    .text
    .global _hstart
_hstart:
    mov     r0, sp              @ argc, argv...
    bic     sp, sp, #7
    bl      harness_main
    mov     r7, #248            @ exit_group
    svc     0

@ long hsys(long n, a, b, c, d, e, f)
    .global hsys
hsys:
    push    {r4-r7, lr}
    mov     r7, r0
    mov     r0, r1
    mov     r1, r2
    mov     r2, r3
    ldr     r3, [sp, #20]
    ldr     r4, [sp, #24]
    ldr     r5, [sp, #28]
    ldr     r6, [sp, #32]
    svc     0
    pop     {r4-r7, pc}

    .global hsig_restorer
hsig_restorer:
    mov     r7, #173            @ rt_sigreturn
    svc     0

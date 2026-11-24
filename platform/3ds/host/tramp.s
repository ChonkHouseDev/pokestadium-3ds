@ Puentes entre el host (little-endian) y el bloque del juego (big-endian BE8).
    .arch armv6k
    .fpu vfpv2
    .arm
    .text
    .global ps_call_blob
@ void ps_call_blob(u32 entry, u32 arg0): entra al juego en modo BE (normalmente no regresa)
ps_call_blob:
    push    {r4, lr}
    mov     r4, r0
    mov     r0, r1
    setend  be
    blx     r4
    setend  le
    pop     {r4, pc}

@ El juego llama aqui en modo BE; se pasa a LE, se llama la funcion real y se regresa a BE.
.macro LE_TRAMP name, target
    .global \name
\name:
    setend  le
    push    {r4, r5, lr}
    mov     r5, sp              @ las pilas de las fibras del juego son chicas (64 KB):
    ldr     r4, =ps_last_game_sp    @ (para el vigilante: donde estaba el juego)
    str     r5, [r4]
    ldr     r4, =ps_last_game_lr
    str     lr, [r4]
    ldr     sp, =ps_host_stack_top  @ el host (interprete, citro3d, C++) corre en su propia pila
    ldr     r4, =\target
    blx     r4
    mov     sp, r5
    pop     {r4, r5, lr}
    setend  be
    bx      lr
.endm
    LE_TRAMP tr_print,     host_print
    LE_TRAMP tr_ticks_lo,  host_ticks_lo
    LE_TRAMP tr_ticks_hi,  host_ticks_hi
    LE_TRAMP tr_sleep_us,  host_sleep_us
    LE_TRAMP tr_pump,      host_pump
    LE_TRAMP tr_read_pad,  host_read_pad
    LE_TRAMP tr_gfx_task,  host_gfx_task
    LE_TRAMP tr_exit_game, host_exit_game
    LE_TRAMP tr_res_get,   host_res_get
    LE_TRAMP tr_rom_read,  host_rom_read_game
    LE_TRAMP tr_audio_push, host_audio_push
    LE_TRAMP tr_save_load, host_save_load
    LE_TRAMP tr_save_store, host_save_store
    LE_TRAMP tr_audio_task, host_audio_task
    LE_TRAMP tr_audio_task_state, host_audio_task_state
    LE_TRAMP tr_audio_pending, host_audio_pending
    LE_TRAMP tr_flag, host_flag
    LE_TRAMP tr_mem_event, host_mem_event
    LE_TRAMP tr_gb_present, host_gb_present
    LE_TRAMP tr_gb_rw, host_gb_rw
    LE_TRAMP tr_gb_power, host_gb_power
    .ltorg

@ ---- Manejador de excepciones: el kernel entra aqui; asegurar modo little-endian antes de C ----
    .global ps_exc_entry
ps_exc_entry:
    setend  le
    ldr     r2, =ps_exc_vfp_save    @ el juego puede tener flotantes vivos: guardarlos para reanudar
    vstmia  r2!, {d0-d15}
    vmrs    r3, fpscr
    str     r3, [r2]
    b       ps_exc_handler

@ void ps_exc_resume(CpuRegisters* r): reanuda el hilo con los registros dados
@ CpuRegisters: r[13] (0..51), sp (52), lr (56), pc (60), cpsr (64)
@ Igual, pero regresando a codigo del juego en modo big-endian (r0/pc se apilan ya invertidos)
    .global ps_exc_resume_be
ps_exc_resume_be:
    ldr     r2, =ps_exc_vfp_save
    vldmia  r2!, {d0-d15}
    ldr     r3, [r2]
    vmsr    fpscr, r3
    ldr     r1, [r0, #64]
    msr     APSR_nzcvq, r1
    ldr     sp, [r0, #52]
    ldr     lr, [r0, #56]
    ldr     r2, [r0]
    ldr     r3, [r0, #60]
    rev     r2, r2
    rev     r3, r3
    push    {r2, r3}
    add     r1, r0, #4
    ldm     r1, {r1-r12}
    setend  be
    pop     {r0, pc}

    .global ps_exc_resume
ps_exc_resume:
    ldr     r2, =ps_exc_vfp_save
    vldmia  r2!, {d0-d15}
    ldr     r3, [r2]
    vmsr    fpscr, r3
    ldr     r1, [r0, #64]
    msr     APSR_nzcvq, r1
    ldr     sp, [r0, #52]
    ldr     lr, [r0, #56]
    ldr     r2, [r0]
    ldr     r3, [r0, #60]
    push    {r2, r3}
    add     r1, r0, #4
    ldm     r1, {r1-r12}
    pop     {r0, pc}
    .ltorg

    .bss
    .align 3
ps_exc_vfp_save:
    .space 132
    .global ps_last_game_sp
ps_last_game_sp:
    .space 4
    .global ps_last_game_lr
ps_last_game_lr:
    .space 4
    .align 4
ps_host_stack:
    .space 512 * 1024
ps_host_stack_top:

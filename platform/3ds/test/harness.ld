/* Bloque del juego (ARM big-endian BE8) en direccion fija dentro del proceso del 3DS */
ENTRY(_hstart)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
  . = 0x0A000000;
  __ps_text_start = .;
  .text : { *(.text.start) *(.text*) } :text
  . = ALIGN(4096);
  __ps_text_end = .;   /* paginas de codigo: R-X; lo demas: RW- */
  .rodata : { *(.rodata*) } :data
  .data : { *(.data*) }
  . = ALIGN(16);
  __ps_bss_start = .;
  .bss : { *(.bss*) *(COMMON) }
  . = ALIGN(16);
  __ps_bss_end = .;
  __ps_end = .;
  ASSERT(__ps_end <= 0x0A000000 + 0x01400000, "el juego no cabe en los 20 MB reservados")
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) *(.reginfo) *(.MIPS.*) *(.pdr) *(.mdebug*) *(.gnu.attributes) *(.note*) }
}

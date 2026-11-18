/* Bloque del juego (ARM big-endian BE8) en direccion fija dentro del proceso del 3DS */
SECTIONS {
  . = 0x0A000000;
  __ps_text_start = .;
  .text : { *(.text.start) *(.text*) }
  . = ALIGN(4096);
  __ps_text_end = .;   /* paginas de codigo: R-X; lo demas: RW- */
  .rodata : { *(.rodata*) }
  /* Los datos con valor inicial de cada fragment van juntos y con simbolos: al "cargar" un fragment se devuelven a su
     valor inicial (en el N64 cada carga los trae de nuevo de la ROM). frag_data.ld lo genera build_blob_3ds.sh. */
  .data : {
    __ps_fdata_start = .;
    INCLUDE frag_data.ld
    __ps_fdata_end = .;
    *(.data*)
  }
  . = ALIGN(16);
  __ps_bss_start = .;
  .bss : {
    INCLUDE frag_bss.ld      /* por fragment: sus variables sin valor inicial (se ponen en cero en cada carga) */
    *(.bss*) *(COMMON)
  }
  . = ALIGN(16);
  __ps_bss_end = .;
  __ps_fsnap = .;   /* copia de los datos iniciales de los fragments (se llena al arrancar) */
  . = . + (__ps_fdata_end - __ps_fdata_start);
  . = ALIGN(16);
  __ps_end = .;
  ASSERT(__ps_end <= 0x0A000000 + 0x01400000, "el juego no cabe en los 20 MB reservados")
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) *(.reginfo) *(.MIPS.*) *(.pdr) *(.mdebug*) *(.gnu.attributes) *(.note*) }
}

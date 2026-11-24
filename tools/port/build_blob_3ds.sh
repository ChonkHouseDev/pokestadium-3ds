#!/usr/bin/env bash
# Compila el juego como bloque ARM big-endian (BE8) para el host del 3DS (ruta hibrida).
# Uso: tools/port/build_blob_3ds.sh <DECOMP_DIR con asm/us extraido y build/ del decomp> <SALIDA>
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DECOMP=$(cd "$1" && pwd)
OUT=$(mkdir -p "$2" && cd "$2" && pwd)
JOBS=${JOBS:-4}
ARCH="-mbig-endian -march=armv6k+fp -mfloat-abi=hard -mfpu=vfp -marm -fno-short-enums"
CC="arm-none-eabi-gcc $ARCH"
GAMEFLAGS="${OPT:--O2} -g -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -nostdinc -fno-pic -fno-strict-aliasing -fwrapv -fno-aggressive-loop-optimizations -funsigned-char -fno-zero-initialized-in-bss -fno-toplevel-reorder \
 -std=gnu89 -w -DNON_MATCHING -DAVOID_UB -DVERSION_US=1 -DN_MICRO -D_MIPS_SZPTR=32 -DF3DEX_GBI_2 -DLANGUAGE_C -D_LANGUAGE_C \
 -D_MIPS_SZLONG=32 -D_MIPS_SZINT=32 -DNDEBUG -D_FINALROM -DBUILD_VERSION=VERSION_J -DPORT -isystem $($CC -print-file-name=include) \
 -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=int-conversion -Wno-error=incompatible-pointer-types"
# (GCC 14+ vuelve error esas cuatro cosas; el codigo del decomp las tiene. Con GCC 13 no cambia nada)
# PS_BOUNDS=1 (solo para buscar errores, en otra carpeta de salida): cada indice fuera de un arreglo de tamano declarado
# se anota en el registro con archivo y linea ([limites], port/ps_core.c). El codigo original a veces escribe mas
# alla de un arreglo contando con lo que el N64 tenia despues; aqui las variables quedan en otro orden.
[ -n "${PS_BOUNDS:-}" ] && GAMEFLAGS="$GAMEFLAGS -fsanitize=bounds-strict"
GAMEINC="-I$ROOT/include -I$ROOT/src -I$ROOT/src/libnaudio -I$ROOT -I$ROOT/lib/ultralib/include -I$ROOT/lib/ultralib/include/PR \
 -I$ROOT/lib/ultralib/include/ido -I$ROOT/lib/ultralib/src -I$ROOT/port"
mkdir -p $OUT/obj $OUT/data $OUT/port

list() {
  (cd $ROOT && find src -name '*.c' ! -path 'src/libleo/*' ! -path 'src/fragments/1/*' | sort
   cd $ROOT && find lib/ultralib/src/gu lib/ultralib/src/audio lib/ultralib/src/libc -name '*.c' \
       ! -name loadtextureblockmipmap.c ! -name string.c ! -name us2dex_emu.c | sort)
}
compile_game() {
  local f=$1 o=$OUT/obj/$(echo $1 | tr / _).o
  if [ ! -f $o ] || [ $ROOT/$f -nt $o ]; then
    # Las variables static sin referencias por nombre se conservan (los datos las apuntan con direcciones del N64)
    (cd $ROOT && { echo "#line 1 \"$f\""; sed 's/^static /__attribute__((used)) static /' $f; } | $CC $GAMEFLAGS $GAMEINC -iquote $(dirname $ROOT/$f) -x c -c - -o $o) || { echo "FALLO $f"; return 1; }
  fi
}
export -f compile_game; export ROOT OUT CC GAMEFLAGS GAMEINC
list | xargs -P $JOBS -I{} bash -c 'compile_game {}'

for s in $(cd $DECOMP && find asm/us/data -name '*.s' ! -path '*/fragments/1/*'); do
  o=$OUT/data/$(echo $s | tr / _).o
  [ -f $o ] && continue
  sed -E 's/\.section \.late_rodata/.section .rodata/' $DECOMP/$s > $OUT/data/tmp.s
  arm-none-eabi-as -mbig-endian -march=armv6k -I $ROOT/port/asminc -I $DECOMP $OUT/data/tmp.s -o $o
done

arm-none-eabi-nm --defined-only $OUT/obj/*.o $OUT/data/*.o 2>/dev/null | awk 'NF==3{print $3}' | sort -u > $OUT/defined.txt
python3 $ROOT/tools/port/gen_frag_table.py $DECOMP/build/pokestadium-us.map $OUT/defined.txt $ROOT/port/generated/ps_frag_table.h > /dev/null

# Datos iniciales por fragment (ver blob.ld y port/ps_frag.c)
# (-fno-zero-initialized-in-bss: las variables que el juego inicia en 0 tambien son datos iniciales, no .bss)
mkdir -p $OUT/gen
: > $OUT/gen/frag_data.ld; : > $OUT/gen/frag_bss.ld; echo "/* Generado por build_blob_3ds.sh */" > $OUT/gen/ps_frag_data.h
for n in $(cd $ROOT/src/fragments && ls | sort -n); do
  [ "$n" = 1 ] && continue
  echo "    __ps_fd_${n}_start = .; *src_fragments_${n}_*(.data*) __ps_fd_${n}_end = .;" >> $OUT/gen/frag_data.ld
  echo "    __ps_fb_${n}_start = .; *src_fragments_${n}_*(.bss* COMMON) __ps_fb_${n}_end = .;" >> $OUT/gen/frag_bss.ld
  echo "extern u8 __ps_fd_${n}_start[], __ps_fd_${n}_end[], __ps_fb_${n}_start[], __ps_fb_${n}_end[];" >> $OUT/gen/ps_frag_data.h
done
{ echo "static const struct { int dir; u8* a; u8* b; u8* za; u8* zb; } gPsFragData[] = {"
  for n in $(cd $ROOT/src/fragments && ls | sort -n); do [ "$n" = 1 ] || echo "    { $n, __ps_fd_${n}_start, __ps_fd_${n}_end, __ps_fb_${n}_start, __ps_fb_${n}_end },"; done
  echo "};"; } >> $OUT/gen/ps_frag_data.h
GAMEFLAGS_PORT="$GAMEFLAGS -I$OUT/gen"; [ -n "${PS_NOFRAGRESET:-}" ] || GAMEFLAGS_PORT="$GAMEFLAGS_PORT -DPS_FRAG_DATA"   # PS_NOFRAGRESET=1: sin devolver los datos (para comparar)
for f in port/ps_assets.c port/ps_frag.c port/ps_core.c port/ps_mips_stub.c port/generated/ps_asset_index.c port/generated/ps_frag_syms.c \
         port/os/os_thread.c port/os/os_events.c port/os/os_misc.c \
         platform/3ds/blob/blob_host.c platform/3ds/blob/fiber_arm.c platform/3ds/blob/blob_libc.c; do
  $CC $GAMEFLAGS_PORT -std=gnu99 $GAMEINC -I$ROOT/port/os -c $ROOT/$f -o $OUT/port/$(basename $f).o
done
# Decodificador de las voces (gritos de los Pokemon y narrador; es GSM): el decomp lo deja en ensamblador. Se traduce a C,
# instruccion por instruccion, del ELF del decomp (tools/port/m2c.py; comprobado contra el original con tools/port/verif).
mkdir -p $OUT/gen
PS_ELF=$DECOMP/build/pokestadium-us.elf python3 $ROOT/tools/port/m2c.py ps46680 0x80045FF0 0x80046EF0 0x80045FF0=func_80045FF0 > $OUT/gen/ps_gsm_46680.c
PS_ELF=$DECOMP/build/pokestadium-us.elf python3 $ROOT/tools/port/m2c.py ps49190 0x80048590 0x80048B90 0x80048590=func_80048590 0x80048684=func_80048684 \
  0x80048904=func_80048904 --datos=0x8004867C:0x80048684 --datos=0x80048738:0x80048740 > $OUT/gen/ps_gsm_49190.c
for f in ps_gsm_46680 ps_gsm_49190; do
  $CC $GAMEFLAGS -std=gnu99 -c $OUT/gen/$f.c -o $OUT/port/$f.c.o
done
# HLE del RSP (audio naudio y JPEG), de mupen64plus-rsp-hle, en big-endian
HLE=$ROOT/third_party/mupen64plus-rsp-hle
for f in alist.c alist_naudio.c audio.c jpeg.c memory.c; do
  $CC ${HLEOPT:--O2} ${PS_ACMD_PROF:+-DPS_ACMD_PROF=1} -mtune=mpcore -g -ffreestanding -fno-pic -DM64P_BIG_ENDIAN -DNDEBUG -w -I$HLE -c $HLE/$f -o $OUT/port/hle_$f.o
done
$CC $GAMEFLAGS -std=gnu99 $GAMEINC -I$ROOT/port/os -I$HLE -c $ROOT/port/ps_rsp_hle.c -o $OUT/port/ps_rsp_hle.c.o
$CC -c $ROOT/platform/3ds/blob/blob_asm.S -o $OUT/port/blob_asm.o

# Statics: primero la tabla en ceros, luego con las direcciones reales (mismo tamano: nada se mueve)
link() { arm-none-eabi-ld -EB --be8 -L $OUT/gen -T $ROOT/platform/3ds/blob/blob.ld -Map $OUT/blob.map -o $OUT/blob.elf \
  $OUT/obj/*.o $OUT/data/*.o $OUT/port/*.o $OUT/static/ps_static_syms.o $ROOT/port/generated/syms.ld 2> $OUT/link.err || { grep -m30 "undefined\|multiple" $OUT/link.err; exit 1; }; }
mkdir -p $OUT/static
arm-none-eabi-nm $OUT/obj/*.o > $OUT/static/nm_obj.txt
python3 $ROOT/tools/port/gen_static_syms.py $OUT/static/nm_obj.txt $OUT/static/ps_static_syms.c --placeholder > /dev/null
$CC $GAMEFLAGS -std=gnu99 -c $OUT/static/ps_static_syms.c -o $OUT/static/ps_static_syms.o
link
arm-none-eabi-nm $OUT/blob.elf > $OUT/static/nm_exe.txt
python3 $ROOT/tools/port/gen_static_syms.py $OUT/static/nm_exe.txt $OUT/static/ps_static_syms.c
$CC $GAMEFLAGS -std=gnu99 -c $OUT/static/ps_static_syms.c -o $OUT/static/ps_static_syms.o
link
python3 $ROOT/tools/port/fix_vcvt.py $OUT/blob.elf
arm-none-eabi-objcopy -O binary -R .bss $OUT/blob.elf $OUT/blob.bin
arm-none-eabi-nm $OUT/blob.elf | grep -E ' (__ps_text_end|__ps_end|_blob_start|ps_mips_trap_shim)$'
ls -la $OUT/blob.bin

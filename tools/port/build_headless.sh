#!/usr/bin/env bash
# Compila el juego como ejecutable nativo de 32 bits (Linux) sin ventana, para depurar el arranque.
#
# Requiere:
#   - DECOMP_ASM: carpeta asm/us del decomp extraida de tu ROM (make extract en pret/pokestadium);
#                 de ahi salen los datos que el decomp aun tiene en ensamblador.
#   - SYSROOT:    sysroot i386 con libzip y zlib (ver docs del port).
# Uso: tools/port/build_headless.sh <DECOMP_DIR> <SALIDA>
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DECOMP=$(cd "$1" && pwd)
OUT=$(mkdir -p "$2" && cd "$2" && pwd)
SYSROOT=${SYSROOT:-/opt/i386root}
CC="gcc -m32"
JOBS=${JOBS:-4}

GAMEFLAGS="-O1 -g -ffreestanding -fno-builtin -nostdinc -fno-pic -fno-strict-aliasing -fwrapv -funsigned-char -fno-short-enums \
 -std=gnu89 -w -DNON_MATCHING -DAVOID_UB -DVERSION_US=1 -DN_MICRO -D_MIPS_SZPTR=32 -DF3DEX_GBI_2 -DLANGUAGE_C -D_LANGUAGE_C \
 -D_MIPS_SZLONG=32 -D_MIPS_SZINT=32 -DNDEBUG -D_FINALROM -DBUILD_VERSION=VERSION_J -DPORT -isystem $($CC -print-file-name=include)"
GAMEINC="-I$ROOT/include -I$ROOT/src -I$ROOT/src/libnaudio -I$ROOT -I$ROOT/lib/ultralib/include -I$ROOT/lib/ultralib/include/PR \
 -I$ROOT/lib/ultralib/include/ido -I$ROOT/lib/ultralib/src -I$ROOT/port"
HOSTFLAGS="-O1 -g -std=gnu11 -Wall -Wno-unused -DPORT -D_OS_LIBC_H_ -D_MIPS_SZLONG=32 -D_MIPS_SZINT=32 -DF3DEX_GBI_2 -D_LANGUAGE_C -DLANGUAGE_C \
 -I$ROOT/port -I$ROOT/lib/ultralib/include -I$ROOT/lib/ultralib/include/PR -I$SYSROOT/usr/include"

mkdir -p $OUT/obj $OUT/data $OUT/port

# 1) Codigo del juego (sin GB Tower ni 64DD) y la parte en C de libultra (gu, audio, libc)
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

# 2) Datos que el decomp aun tiene en ensamblador (extraidos de tu ROM). En x86 .word es de 2 bytes: usar .4byte
for s in $(cd $DECOMP && find asm/us/data -name '*.s' ! -path '*/fragments/1/*'); do
  o=$OUT/data/$(echo $s | tr / _).o
  [ -f $o ] && continue
  sed -E 's/^(\s*(\/\*[^*]*\*\/)?\s*)\.word\b/\1.4byte/; s/^(\s*(\/\*[^*]*\*\/)?\s*)\.(half|short)\b/\1.2byte/; s/\.section \.late_rodata/.section .rodata/' \
    $DECOMP/$s > $OUT/data/tmp.s
  as --32 -I $ROOT/port/asminc -I $DECOMP $OUT/data/tmp.s -o $o
done

# 3) Tablas de fragments: id -> entrada nativa y direccion del N64 -> simbolo nativo
nm --defined-only $OUT/obj/*.o $OUT/data/*.o 2>/dev/null | awk 'NF==3{print $3}' | sort -u > $OUT/defined.txt
python3 $ROOT/tools/port/gen_frag_table.py $DECOMP/build/pokestadium-us.map $OUT/defined.txt $ROOT/port/generated/ps_frag_table.h

# 4) Capa del port
for f in ps_assets.c ps_frag.c ps_core.c ps_mips_stub.c generated/ps_asset_index.c generated/ps_frag_syms.c; do
  $CC $GAMEFLAGS -std=gnu99 $GAMEINC -c $ROOT/port/$f -o $OUT/port/$(basename $f).o
done
for f in os/os_thread.c os/os_events.c os/os_misc.c os/fiber_ucontext.c host/host_headless.c; do
  $CC $HOSTFLAGS -c $ROOT/port/$f -o $OUT/port/$(basename $f).o
done

# 5) Enlazar (simbolos absolutos del N64: segmentos, rangos de ROM)
$CC -o $OUT/ps_headless $OUT/obj/*.o $OUT/data/*.o $OUT/port/*.o $ROOT/port/generated/syms.ld \
  -L$SYSROOT/usr/lib -L$SYSROOT/usr/lib/i386-linux-gnu -l:libzip.a -lz -lm \
  -Wl,-rpath-link,$SYSROOT/usr/lib/i386-linux-gnu 2> $OUT/link.err || { grep -m30 "undefined\|multiple" $OUT/link.err; exit 1; }
echo "Listo: $OUT/ps_headless"

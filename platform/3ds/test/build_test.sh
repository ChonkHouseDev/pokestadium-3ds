#!/usr/bin/env bash
# Banco de pruebas: el bloque del 3DS (ya compilado por build_blob_3ds.sh) + un host minimo para qemu-armeb.
# Uso: platform/3ds/test/build_test.sh <SALIDA de build_blob_3ds.sh>
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
B=$(cd "$1" && pwd)
ARCH="-mbig-endian -march=armv6k+fp -mfloat-abi=hard -mfpu=vfp -marm -fno-short-enums"
CC="arm-none-eabi-gcc $ARCH"
FLAGS="-O2 -g -ffreestanding -fno-builtin -nostdinc -fno-pic -funsigned-char -w -DF3DEX_GBI_2 -D_LANGUAGE_C -DLANGUAGE_C -D_MIPS_SZLONG=32 -D_MIPS_SZINT=32 -DNDEBUG -D_FINALROM -DPORT -DVERSION_US=1 -isystem $($CC -print-file-name=include)"
INC="-I$ROOT/include -I$ROOT/src -I$ROOT -I$ROOT/lib/ultralib/include -I$ROOT/lib/ultralib/include/PR -I$ROOT/port -I$ROOT/port/os"
mkdir -p $B/test
$CC $FLAGS -std=gnu99 $INC -c $ROOT/platform/3ds/test/harness.c -o $B/test/harness.o
$CC -c $ROOT/platform/3ds/test/harness_start.S -o $B/test/harness_start.o
# Los static se ubican en otra direccion que en el .3dsx: tabla propia en dos pasadas
link() { arm-none-eabi-ld -EB --be8 -T $ROOT/platform/3ds/test/harness.ld -o $B/test/ps_blob_test \
  $B/test/harness.o $B/test/harness_start.o $B/obj/*.o $B/data/*.o $B/port/*.o $B/test/ps_static_syms.o $ROOT/port/generated/syms.ld 2> $B/test/link.err \
  || { grep -m20 "undefined\|multiple\|error" $B/test/link.err; exit 1; }; }
GF="-O2 -ffreestanding -nostdinc -fno-pic -w"
python3 $ROOT/tools/port/gen_static_syms.py $B/static/nm_obj.txt $B/test/ps_static_syms.c --placeholder > /dev/null
$CC $GF -c $B/test/ps_static_syms.c -o $B/test/ps_static_syms.o
link
arm-none-eabi-nm $B/test/ps_blob_test > $B/test/nm_exe.txt
python3 $ROOT/tools/port/gen_static_syms.py $B/test/nm_exe.txt $B/test/ps_static_syms.c
$CC $GF -c $B/test/ps_static_syms.c -o $B/test/ps_static_syms.o
link
python3 $ROOT/tools/port/fix_vcvt.py $B/test/ps_blob_test
echo "Listo: $B/test/ps_blob_test"

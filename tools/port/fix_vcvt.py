#!/usr/bin/env python3
"""Float -> entero sin signo como en el N64.

El juego (compilado para MIPS con IDO) convierte float a u8/u16/u32 con cvt.w.s / trunc.w.s: un valor
negativo da un entero negativo y al guardarlo en u8/u16 "da la vuelta". GCC en ARM usa VCVT.U32, que
SATURA a 0. Aqui se cambian todas las VCVT.U32.F32/F64 (redondeo a cero) del codigo del juego por
VCVT.S32 (bit 16 del opc2): para negativos el resultado es el mismo que en MIPS.

Uso: fix_vcvt.py archivo.elf   (modifica la seccion .text en el lugar; instrucciones BE8 = little-endian)
"""
import struct
import sys

path = sys.argv[1]
data = bytearray(open(path, "rb").read())
assert data[:4] == b"\x7fELF" and data[4] == 1, "solo ELF32"
big = data[5] == 2
E = ">" if big else "<"
shoff = struct.unpack_from(E + "I", data, 0x20)[0]
shentsize, shnum, shstrndx = struct.unpack_from(E + "HHH", data, 0x2E)


def sh(i):
    return struct.unpack_from(E + "IIIIIIIIII", data, shoff + i * shentsize)


strtab = sh(shstrndx)
names = data[strtab[4]: strtab[4] + strtab[5]]
patched = 0
for i in range(shnum):
    name_off, typ, flags, addr, off, size = sh(i)[:6]
    name = names[name_off: names.index(b"\0", name_off)].decode()
    if typ != 1 or not (flags & 4):  # PROGBITS ejecutable
        continue
    for p in range(off, off + size - 3, 4):
        ins = struct.unpack_from("<I", data, p)[0]  # BE8: instrucciones en little-endian
        # VCVT (float -> entero), op=1 (hacia cero): 11101D111 opc2 Vd 101 sz 1 1 M 0 Vm
        if (ins & 0x0FB80ED0) == 0x0EB80AC0 and ((ins >> 16) & 7) == 4 and (ins >> 28) != 0xF:
            struct.pack_into("<I", data, p, ins | (1 << 16))
            patched += 1
open(path, "wb").write(data)
print(f"fix_vcvt: {patched} conversiones float->sin signo cambiadas a con signo en {path}")

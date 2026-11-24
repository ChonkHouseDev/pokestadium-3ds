#!/usr/bin/env python3
"""Tabla de simbolos static del juego: direccion del N64 (sale del nombre, D_86B0CF38) -> direccion nativa.

Los static no aparecen en el mapa del decomp ni se pueden referenciar desde otro archivo, asi que la
direccion nativa se toma del ejecutable ya enlazado. Se enlaza dos veces: la primera con la tabla en
ceros (mismo tamano) y la segunda con los valores reales; como el tamano no cambia, nada se mueve.

Uso: gen_static_syms.py <nm del ejecutable o de los objetos> <salida.c> [--placeholder]"""
import re, sys

PAT = re.compile(r'^([0-9a-fA-F]+)\s+([bBdDrRtT])\s+((?:D|func)_(8[0-9A-F]{7}))$')

def main():
    nm_out, out = sys.argv[1], sys.argv[2]
    placeholder = '--placeholder' in sys.argv
    rows = {}
    for line in open(nm_out):
        m = PAT.match(line.strip())
        if not m or m.group(2).isupper():  # solo locales (static); los globales ya estan en gPsFragSyms
            continue
        n64 = int(m.group(4), 16)
        if not (0x80000000 <= n64 < 0x90000000):
            continue
        rows[n64] = 0 if placeholder else int(m.group(1), 16)
    with open(out, 'w') as f:
        f.write('/* Generado por tools/port/gen_static_syms.py: no editar. */\n')
        f.write('const struct { unsigned int vram; unsigned int native; } gPsStaticSyms[] = {\n')
        for n64 in sorted(rows):
            f.write(f'    {{ 0x{n64:08X}u, 0x{rows[n64]:08X}u }},\n')
        f.write('    { 0xFFFFFFFFu, 0 }\n};\n')
        f.write(f'const unsigned int gPsStaticSymsCount = {len(rows)};\n')
    print(f'{len(rows)} simbolos static' + (' (en ceros)' if placeholder else ''))

if __name__ == '__main__':
    main()

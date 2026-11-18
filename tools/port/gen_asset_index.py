#!/usr/bin/env python3
"""Genera port/generated/ps_asset_index.c: rango de ROM -> recurso del .o2r.

El juego pide assets por rango de ROM (romStart, romEnd); el port los sirve desde pokestadium.o2r.
Uso: gen_asset_index.py assets-config/us port/generated/ps_asset_index.c"""
import os, re, sys

# Los recursos convertidos conservan el tamano de la ROM: se sirven igual que los crudos
TYPES = {'STADIUM:RAW': 'PS_ASSET_RAW', 'STADIUM:TEXT': 'PS_ASSET_RAW', 'STADIUM:PERS_SZP': 'PS_ASSET_PERS_SZP'}

def main():
    src, out = sys.argv[1], sys.argv[2]
    rows = []
    for fn in sorted(os.listdir(src)):
        if not fn.endswith('.yml'): continue
        group = fn[:-4]
        text = open(os.path.join(src, fn), encoding='utf-8').read()
        for m in re.finditer(r'^(\w+):\n  symbol: \w+\n  type: ([\w:]+)\n  offset: (0x[0-9A-Fa-f]+)\n  size: (0x[0-9A-Fa-f]+)', text, re.M):
            name, typ, off, size = m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4), 16)
            rows.append((off, size, TYPES[typ], f'{group}/{name}'))
    rows.sort()
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w', encoding='utf-8') as f:
        f.write('/* Generado por tools/port/gen_asset_index.py: no editar. */\n#include "ps_assets.h"\n\n')
        f.write('const PsAssetEntry gPsAssetIndex[] = {\n')
        for off, size, typ, path in rows:
            f.write(f'    {{ 0x{off:07X}, 0x{size:X}, {typ}, "{path}" }},\n')
        f.write('};\nconst unsigned int gPsAssetIndexCount = sizeof(gPsAssetIndex) / sizeof(gPsAssetIndex[0]);\n')
    print(f'{len(rows)} assets indexados')

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Genera la configuracion de Torch (config.yml + assets-config/us/*.yml) para Pokemon Stadium US 1.0.

Cada bin de yamls/us/rom.yaml se vuelve un recurso:
  - PERS-SZP          -> STADIUM:PERS_SZP (descomprimido al extraer)
  - archivo BinArchive -> <nombre>/header (tabla cruda) + <nombre>/NNN por archivo interno
  - resto (Yay0, JPEG, crudo) -> STADIUM:RAW (el juego lo procesa como hoy)
Uso: generar_yamls_torch.py baserom.z64 yamls/us/rom.yaml <dir_salida>"""
import sys, os, re, hashlib, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stadium_fmt as fmt
from inventario_assets import kind, as_archive

# Tipo de recurso para los archivos internos de ciertos BinArchive (formatos ya convertidos a little-endian)
ARCHIVE_FILE_TYPES = {'textdata': 'STADIUM:TEXT'}

def entry(sym, typ, off, size):
    return f"{sym}:\n  symbol: {sym}\n  type: {typ}\n  offset: 0x{off:X}\n  size: 0x{size:X}\n\n"

def main():
    rom = open(sys.argv[1], 'rb').read()
    yaml_text = open(sys.argv[2], encoding='utf-8').read()
    out = sys.argv[3]
    os.makedirs(os.path.join(out, 'us'), exist_ok=True)
    starts = sorted(int(x, 16) for x in re.findall(r'- \[(0x[0-9A-Fa-f]+)', yaml_text))
    bins = []
    for line in yaml_text.splitlines():
        m = re.match(r'\s*- \[(0x[0-9A-Fa-f]+),\s*bin(?:,\s*([\w/]+))?\]', line)
        if m: bins.append((int(m.group(1), 16), m.group(2) or f'unk_{m.group(1)[2:].upper()}'))
    n_res = 0
    groups = {}
    for start, name in bins:
        end = min([a for a in starts if a > start] or [len(rom)])
        group = name.split('/')[0] if '/' in name else ('rom_parts' if name.startswith('rom_part_') else name)
        sym = name.replace('/', '_')
        k = kind(rom[start:start+16])
        text = ''
        files = None if k else as_archive(rom, start, end)
        if files:
            n = len(files)
            text += entry(f'{sym}_header', 'STADIUM:RAW', start, 0x10 + n * 0x10)
            for i, (o, s) in enumerate(files):
                t = 'STADIUM:PERS_SZP' if rom[start+o:start+o+8] == b'PERS-SZP' else ARCHIVE_FILE_TYPES.get(name, 'STADIUM:RAW')
                text += entry(f'{sym}_{i:03d}', t, start + o, s)
            n_res += n + 1
        else:
            t = 'STADIUM:PERS_SZP' if k == 'PERS-SZP' else 'STADIUM:RAW'
            text += entry(sym, t, start, end - start)
            n_res += 1
        groups.setdefault(group, []).append(text)
    for g, parts in groups.items():
        with open(os.path.join(out, 'us', f'{g}.yml'), 'w', encoding='utf-8') as f:
            f.write(''.join(parts))
    sha1 = hashlib.sha1(rom).hexdigest()
    with open(os.path.join(out, 'config.yml'), 'w', encoding='utf-8') as f:
        f.write(f"""{sha1}:
  name: Pokemon Stadium [US 1.0]
  path: assets-config/us
  config:
    gbi: F3DEX2
    sort: OFFSET
    output:
      binary: pokestadium.o2r
      headers: include/assets
      code: assets/code
""")
    print(f'{len(groups)} yml, {n_res} recursos, sha1 {sha1}')

if __name__ == '__main__':
    main()

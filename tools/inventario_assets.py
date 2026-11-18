#!/usr/bin/env python3
"""Inventario de assets de Pokemon Stadium (US 1.0).
Clasifica cada bin de rom.yaml: PERS-SZP (Yay0 + parches), PRES-JPEG, FRAGMENT,
BinArchive (tabla de archivos) o crudo. Uso: inventario_assets.py baserom.z64 rom.yaml [salida.csv]"""
import sys, re, struct, csv

def magic(b): return b[:8]

def kind(b):
    m = magic(b)
    if m == b'PERS-SZP': return 'PERS-SZP'
    if m == b'PRESJPEG': return 'PRES-JPEG'
    if m[:4] == b'Yay0': return 'Yay0'
    if b[8:16] == b'FRAGMENT': return 'FRAGMENT'
    return None

def as_archive(rom, start, end):
    """BinArchive: u16 flags, u16 seg, u32, u32 total_size, u32 num_files; luego n x {off,size,u32,u32}."""
    if end - start < 0x20: return None
    _, _, total, n = struct.unpack('>HHII', rom[start+4-4:start+0x10][:12].rjust(12, b'\0')) if False else (0,0,*struct.unpack('>II', rom[start+8:start+0x10]))
    if n == 0 or n > 4096 or 0x10 + n*0x10 > end - start: return None
    files, prev = [], 0
    for i in range(n):
        off, size = struct.unpack('>II', rom[start+0x10+i*0x10:start+0x18+i*0x10])
        if off < 0x10 + n*0x10 or off + size > end - start or off < prev: return None
        files.append((off, size)); prev = off
    return files

def main():
    rom = open(sys.argv[1], 'rb').read()
    ents = []
    for line in open(sys.argv[2], encoding='utf-8'):
        m = re.match(r'\s*- \[(0x[0-9A-Fa-f]+),\s*bin(?:,\s*([\w/]+))?\]\s*(#.*)?', line)
        if m: ents.append((int(m.group(1), 16), m.group(2) or f'unk_{m.group(1)[2:]}', (m.group(3) or '').lstrip('# ')))
    m_all = [int(x, 16) for x in re.findall(r'- \[(0x[0-9A-Fa-f]+)', open(sys.argv[2], encoding='utf-8').read())]
    rows, tot = [], {}
    for start, name, note in ents:
        end = min([a for a in m_all if a > start] or [len(rom)])
        b = rom[start:end]
        k = kind(b)
        sub = ''
        if not k:
            files = as_archive(rom, start, end)
            if files:
                k = 'ARCHIVE'
                c = {}
                for off, size in files:
                    kk = kind(rom[start+off:start+off+16]) or 'raw'
                    c[kk] = c.get(kk, 0) + 1
                sub = ' '.join(f'{v}x{kk}' for kk, v in sorted(c.items()))
                for kk, v in c.items(): tot['arch:'+kk] = tot.get('arch:'+kk, 0) + v
            else:
                k = 'raw'
        tot[k] = tot.get(k, 0) + 1
        rows.append((hex(start), hex(end - start), name, k, sub, note))
    w = csv.writer(open(sys.argv[3], 'w', newline='', encoding='utf-8')) if len(sys.argv) > 3 else None
    if w: w.writerow(['rom', 'tam', 'nombre', 'tipo', 'contenido', 'nota'])
    for r in rows:
        if w: w.writerow(r)
    print('bins:', len(rows))
    for k, v in sorted(tot.items()): print(f'  {k}: {v}')
    return rows

if __name__ == '__main__':
    for r in main(): print(*r[:5], sep='\t')

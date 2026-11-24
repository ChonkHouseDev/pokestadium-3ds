#!/usr/bin/env python3
"""Genera ps_frag_table.h: id de fragment -> funcion de entrada (simbolo en TEXT_START + 0x20)."""
import re, sys
mapf, defs, out = sys.argv[1], sys.argv[2], sys.argv[3]
import os, struct
decomp = os.path.dirname(os.path.dirname(os.path.abspath(mapf)))
def header_target(frag, base):
    """El header del fragment empieza con 'j main': decodificar el destino real."""
    n = frag[8:]
    for cand in (f'assets/us/fragments/{n}/{frag}_header.textbin.bin',):
        path = os.path.join(decomp, cand)
        if os.path.exists(path):
            w = struct.unpack('>I', open(path, 'rb').read(4))[0]
            if (w >> 26) == 2: return (base & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
    return base + 0x20
defined = set(open(defs).read().split())
starts, addr2sym = {}, {}
for line in open(mapf):
    m = re.match(r'\s+0x([0-9a-f]+)\s+(fragment\d+)_TEXT_START = ', line)
    if m: starts[m.group(2)] = int(m.group(1), 16); continue
    m = re.match(r'\s+0x([0-9a-f]+)\s+([A-Za-z_]\w*)\s*$', line)
    if m: addr2sym.setdefault(int(m.group(1), 16), []).append(m.group(2))
rows, missing = [], []
for frag, base in sorted(starts.items(), key=lambda x: int(x[0][8:])):
    if frag == 'fragment1': continue  # GB Tower: fuera del port
    fid = ((base & 0x0FF00000) >> 20) - 0x10
    target = header_target(frag, base)
    cands = [s for s in addr2sym.get(target, []) if s in defined] or [s for s in addr2sym.get(target, []) if s.startswith('func_')]
    if cands: rows.append((fid, cands[0], frag))
    else: missing.append(frag); rows.append((fid, None, frag))  # codigo nativo sin funcion de entrada (biblioteca)
with open(out, 'w') as f:
    f.write('/* Generado por gen_frag_table.py */\n')
    for _, s, _ in rows:
        if s: f.write(f'extern int {s}();\n')
    f.write('int ps_frag_no_entry(void);\n')
    f.write('static const struct { int id; void* entry; int dir; } gPsFragTable[] = { /* dir: carpeta src/fragments/<dir> */\n')
    for fid, s, frag in rows: f.write(f'    {{ {fid}, (void*){s or "ps_frag_no_entry"}, {frag[8:]} }}, /* {frag} */\n')
    f.write('};\n')
print(f'{len(rows)} fragments de codigo; sin entrada nativa: {missing}')

# ---- Tabla de simbolos de los fragments de codigo: direccion MIPS (0x8XXXXXXX) -> simbolo nativo ----
# Los datos del juego guardan punteros a funciones/variables de los fragments con su direccion
# original; el port los traduce a la direccion nativa con esta tabla (busqueda binaria).
code_bases = sorted(base for frag, base in starts.items() if any(f == frag for _, _, f in rows))
entries = []
for addr, syms in addr2sym.items():
    in_main = 0x80000000 <= addr < 0x80400000   # segmento principal (no relocalizable)
    if not in_main and not (0x81000000 <= addr < 0x90000000): continue
    if not in_main and not any(b <= addr < b + 0x100000 for b in code_bases): continue
    good = [s for s in syms if s in defined and not s.startswith('.')]
    if good: entries.append((addr, good[0]))
entries.sort()
symtab = out.replace('ps_frag_table.h', 'ps_frag_syms.c')
with open(symtab, 'w') as f:
    f.write('/* Generado por gen_frag_table.py */\n')
    for _, s in entries: f.write(f'extern char {s}[];\n')
    f.write('const struct { unsigned int vram; void* native; } gPsFragSyms[] = {\n')
    for a, s in entries: f.write(f'    {{ 0x{a:08X}u, {s} }},\n')
    f.write('};\nconst unsigned int gPsFragSymsCount = sizeof(gPsFragSyms) / sizeof(gPsFragSyms[0]);\n')
print(f'{len(entries)} simbolos de fragments mapeados a nativo')

# ---- Tablas de saltos en ensamblador (fragment31_unk_table): "j destino; nop" en direcciones fijas ----
# El juego llama a 0x81000020 + 8*i; en nativo esa ranura se resuelve al simbolo del destino.
import glob
jt = []
for path, vram in [(os.path.join(decomp, 'assets/us/fragments/31/fragment31_unk_table.textbin.bin'), 0x81000020)]:
    if not os.path.exists(path): continue
    data = open(path, 'rb').read()
    for k in range(0, len(data) - 7, 8):
        w, nop = struct.unpack('>II', data[k:k + 8])
        if (w >> 26) == 2 and nop == 0:
            jt.append((vram + k, (vram & 0xF0000000) | ((w & 0x3FFFFFF) << 2)))
with open(symtab, 'a') as f:
    f.write('/* Ranuras de tablas de saltos: direccion de la ranura -> direccion MIPS del destino */\n')
    f.write('const struct { unsigned int slot; unsigned int target; } gPsJumpSlots[] = {\n')
    for slot, target in jt: f.write(f'    {{ 0x{slot:08X}u, 0x{target:08X}u }},\n')
    f.write('    { 0, 0 }\n};\nconst unsigned int gPsJumpSlotsCount = %d;\n' % len(jt))
print(f'{len(jt)} ranuras de tablas de saltos')

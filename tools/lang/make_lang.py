#!/usr/bin/env python3
"""make_lang.py: arma un paquete de idioma (lang_xx.bin) para el port, en la PC del usuario.

Toma los textos de la ROM del usuario (US) y una tabla de traducciones en CSV (columnas: en, es, de, pt, fr, it...),
empareja cada texto del juego con su fila por el texto en ingles y escribe los 46 archivos de texto ya traducidos en
el mismo formato que usa el juego. Lo que no empareja se queda en ingles. El paquete no trae nada de la ROM que no
sea texto ya reemplazado por el del CSV; los textos sin traducir van como "igual que el original" (no se copian).

Uso: make_lang.py baserom.z64 traducciones.csv [mas.csv ...] --lang es --out lang_es.bin
"""
import argparse, csv, difflib, re, struct, sys, unicodedata

TEXT_ROM = 0x783760
KEEP_FILES = {36, 39}       # nombres de Pokemon y de objetos: se quedan como en la US
UPPER_FILES = {37}          # nombres de movimientos: en mayusculas, como en la US

def yay0(d):
    n, lo, co = struct.unpack('>III', d[4:16]); out = bytearray(); p = 16; mask = 0; bits = 0
    while len(out) < n:
        if bits == 0: mask = struct.unpack('>I', d[p:p + 4])[0]; p += 4; bits = 32
        if mask & 0x80000000: out.append(d[co]); co += 1
        else:
            v = struct.unpack('>H', d[lo:lo + 2])[0]; lo += 2
            c = v >> 12; o = (v & 0xFFF) + 1
            if c == 0: c = d[co] + 18; co += 1
            else: c += 2
            for _ in range(c): out.append(out[-o])
        mask = (mask << 1) & 0xFFFFFFFF; bits -= 1
    return bytes(out)

def text_files(rom):
    n = struct.unpack('>I', rom[TEXT_ROM + 12:TEXT_ROM + 16])[0]
    out = []
    for i in range(n):
        off, size = struct.unpack('>II', rom[TEXT_ROM + 16 + i * 16:TEXT_ROM + 24 + i * 16])
        d = rom[TEXT_ROM + off:TEXT_ROM + off + size]
        if d[:8] == b'PERS-SZP': d = yay0(d[d.find(b'Yay0'):])
        cnt = struct.unpack('>I', d[:4])[0] if len(d) >= 4 else 0
        if cnt == 0 or cnt > 5000 or 4 + 4 * cnt > len(d): out.append(None); continue
        offs = struct.unpack('>%dI' % cnt, d[4:4 + 4 * cnt])
        out.append([d[o:d.index(b'\0', o)] for o in offs])
    return out

def norm(s):
    s = unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode().lower()
    return re.sub(r'[^a-z0-9#%]+', ' ', s).strip()

TOKEN = re.compile(rb'#\d+|[\x80-\x9f\xa5\xa9\xb7\xbe\xd7]')
def tokens(b): return sorted(TOKEN.findall(b))

def encode(s):
    s = s.replace('\r', '').replace('’', "'").replace('…', '...').replace('“', '"').replace('”', '"')
    try: return s.encode('latin-1')
    except UnicodeEncodeError: return unicodedata.normalize('NFKD', s).encode('latin-1', 'ignore')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom'); ap.add_argument('csv', nargs='+'); ap.add_argument('--lang', default='es'); ap.add_argument('--out', required=True)
    ap.add_argument('--report'); a = ap.parse_args()
    rom = open(a.rom, 'rb').read()
    if len(rom) != 0x2000000: sys.exit('la ROM no mide 32 MB (US 1.0 en .z64)')
    rows = {}; by_id = {}
    for path in a.csv:   # los ultimos CSV mandan (p. ej. correcciones a mano)
        for r in csv.DictReader(open(path, encoding='utf-8')):
            en, tr = (r.get('en') or '').strip(), (r.get(a.lang) or '').strip()
            if en and tr: rows[norm(en)] = tr
            if tr and r.get('id'): by_id[r['id']] = (en, tr)
    keys = list(rows)
    by_word = {}
    for k in keys:
        for w in set(k.split()): by_word.setdefault(w, []).append(k)
    files = text_files(rom); total = done = fuzzy = 0; blobs = []; rep = []
    # Pokedex y descripciones de movimientos: el texto en ingles de la tabla no es el de la US, asi que se emparejan
    # por posicion: la fila X_DEX_ENTRY va con el Pokemon X (archivo 36 -> 10) y X_MOVE_DESC con el movimiento X (37 -> 11)
    by_pos = {}
    for src, dst, suf, namesuf in ((36, 10, '_DEX_ENTRY', '_NAME'), (37, 11, '_MOVE_DESC', '_MOVE_NAME')):
        if files[src] is None or files[dst] is None: continue
        pos = {norm(x.decode('latin-1')): i for i, x in enumerate(files[src])}
        for rid, (en, tr) in by_id.items():
            if not rid.endswith(suf): continue
            name = by_id.get(rid[:-len(suf)] + namesuf)
            i = pos.get(norm(name[0])) if name else None
            if i is not None and i < len(files[dst]): by_pos[(dst, i)] = tr
    for fi, strs in enumerate(files):
        if strs is None or fi in KEEP_FILES: blobs.append(b''); continue
        new = []; changed = False
        for si, s in enumerate(strs):
            total += 1
            txt = s.decode('latin-1'); k = norm(txt); tr = rows.get(k); how = 'exacto'
            if (fi, si) in by_pos: tr = by_pos[(fi, si)]; how = 'posicion'
            if tr is None and len(k) >= 12:
                cand = {}
                for w in set(k.split()):
                    for c in by_word.get(w, ()): cand[c] = cand.get(c, 0) + 1
                best = sorted(cand, key=lambda c: -cand[c])[:12]
                m = difflib.get_close_matches(k, best, 1, 0.90)
                if m: tr = rows[m[0]]; how = 'parecido'
            if tr is not None:
                b = encode(tr.upper() if fi in UPPER_FILES else tr)
                lo, lt = s.split(b'\n'), b.split(b'\n')
                wo, wt = max(map(len, lo)), max(map(len, lt))
                # se deja en ingles si no trae los mismos codigos del juego, trae un formato ajeno (%s), ocupa mas
                # renglones, es mucho mas ancho que el original (se saldria de su cuadro) o viene acomodado en columnas
                if tokens(b) != tokens(s) or b == s or (b'%' in b and b'%' not in s) or len(lt) > len(lo) or wt > max(wo * 3 // 2, wo + 8) or (b'   ' in b and b'   ' not in s): tr = None
            if tr is None: new.append(s); continue
            new.append(b); changed = True; done += 1; fuzzy += how == 'parecido'
            if a.report: rep.append('%d\t%s\t%s\t%s' % (fi, how, txt.replace('\n', '\\n'), tr.replace('\n', '\\n')))
        if not changed: blobs.append(b''); continue
        body = b''; offs = []
        base = 4 + 4 * len(new)
        for b in new: offs.append(base + len(body)); body += b + b'\0'
        t = struct.pack('>I', len(new)) + struct.pack('>%dI' % len(new), *offs) + body
        blobs.append(t + b'\0' * (-len(t) % 4))
    head = b'PSLG' + struct.pack('>I', len(blobs)); pos = 8 + 8 * len(blobs); tab = b''
    for b in blobs: tab += struct.pack('>II', pos if b else 0, len(b)); pos += len(b)
    open(a.out, 'wb').write(head + tab + b''.join(blobs))
    if a.report: open(a.report, 'w', encoding='utf-8').write('\n'.join(rep) + '\n')
    print('%s: %d de %d textos traducidos (%d por parecido), %d KB' % (a.out, done, total, fuzzy, pos // 1024))

if __name__ == '__main__': main()

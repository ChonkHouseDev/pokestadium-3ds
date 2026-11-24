#!/usr/bin/env python3
# gbsav.py: partidas de prueba de Pokemon Rojo/Azul/Amarillo (internacional) y su revision.
#   gbsav.py crear salida.sav NOMBRE semilla   -> partida con equipo, 12 cajas con Pokemon y objetos
#   gbsav.py ver a.sav                         -> resumen (sumas, equipo, cajas, objetos)
#   gbsav.py cmp antes.sav despues.sav         -> que Pokemon/objetos cambiaron de lugar y si se perdio o daño alguno
import sys, random, collections

SPEC = {  # indice interno -> (nombre, numero de Pokedex, tipo1, tipo2, tasa de captura, estadisticas base hp/atk/def/spd/spc)
    0x54: ('PIKACHU', 25, 0x17, 0x17, 190, (35, 55, 30, 90, 50)), 0xB4: ('CHARIZARD', 6, 0x14, 0x02, 45, (78, 84, 78, 100, 85)),
    0x1C: ('BLASTOISE', 9, 0x15, 0x15, 45, (79, 83, 100, 78, 85)), 0x9A: ('VENUSAUR', 3, 0x16, 0x03, 45, (80, 82, 83, 80, 100)),
    0x84: ('SNORLAX', 143, 0x00, 0x00, 25, (160, 110, 65, 30, 65)), 0x0E: ('GENGAR', 94, 0x08, 0x03, 45, (60, 65, 60, 110, 130)),
    0x95: ('ALAKAZAM', 65, 0x18, 0x18, 50, (55, 50, 45, 120, 135)), 0x13: ('LAPRAS', 131, 0x15, 0x19, 45, (130, 85, 80, 60, 95)),
    0x42: ('DRAGONITE', 149, 0x1A, 0x02, 45, (91, 134, 95, 80, 100)), 0x16: ('GYARADOS', 130, 0x15, 0x02, 45, (95, 125, 79, 81, 100)),
    0x3C: ('TAUROS', 128, 0x00, 0x00, 45, (75, 100, 95, 110, 70)), 0x98: ('STARMIE', 121, 0x15, 0x18, 60, (60, 75, 85, 115, 100)),
    0x68: ('JOLTEON', 135, 0x17, 0x17, 45, (65, 65, 60, 130, 110)), 0x01: ('RHYDON', 112, 0x04, 0x05, 60, (105, 130, 120, 40, 45)),
    0x0A: ('EXEGGUTOR', 103, 0x16, 0x18, 45, (95, 95, 85, 55, 125)), 0x28: ('CHANSEY', 113, 0x00, 0x00, 30, (250, 5, 5, 50, 105)),
    0x24: ('PIDGEY', 16, 0x00, 0x02, 255, (40, 45, 40, 56, 35)), 0xA5: ('RATTATA', 19, 0x00, 0x00, 255, (30, 56, 35, 72, 25)),
    0x7B: ('CATERPIE', 10, 0x07, 0x07, 255, (45, 30, 35, 45, 20)), 0x85: ('MAGIKARP', 129, 0x15, 0x15, 255, (20, 10, 55, 80, 20)),
}
GROW = {0x54: 1, 0xB4: 2, 0x1C: 2, 0x9A: 2, 0x84: 3, 0x0E: 2, 0x95: 2, 0x13: 3, 0x42: 3, 0x16: 3, 0x3C: 3, 0x98: 3, 0x68: 1,
        0x01: 3, 0x0A: 3, 0x28: 0, 0x24: 2, 0xA5: 1, 0x7B: 1, 0x85: 3}   # 0 rapido, 1 medio, 2 medio lento, 3 lento
def expfor(sp, n):
    g = GROW[sp]
    return 4 * n ** 3 // 5 if g == 0 else n ** 3 if g == 1 else 6 * n ** 3 // 5 - 15 * n * n + 100 * n - 140 if g == 2 else 5 * n ** 3 // 4
MOVES = [0x21, 0x2D, 0x55, 0x39, 0x3A, 0x35, 0x59, 0x5E, 0x62, 0x13, 0x22, 0x56, 0x5C, 0x69, 0x26, 0x3F]
PPS = {0x21: 35, 0x2D: 40, 0x55: 15, 0x39: 15, 0x3A: 10, 0x35: 15, 0x59: 10, 0x5E: 10, 0x62: 30, 0x13: 15, 0x22: 15,
       0x56: 20, 0x5C: 10, 0x69: 20, 0x26: 15, 0x3F: 5}
PARTY, BOX, BANKS = 0x2F2C, 0x30C0, (0x4000, 0x6000)

def txt(s, n=11):
    b = bytearray([0x50] * n)
    for i, c in enumerate(s[:n - 1]):
        b[i] = 0x80 + ord(c) - ord('A') if c.isalpha() else 0xF6 + int(c)
    return bytes(b)
def untxt(b):
    o = ''
    for c in b:
        if c == 0x50: break
        o += chr(c - 0x80 + 65) if 0x80 <= c < 0x9A else chr(c - 0xA0 + 97) if 0xA0 <= c < 0xBA else str(c - 0xF6) if 0xF6 <= c <= 0xFF else '?'
    return o
def be16(v): return bytes([v >> 8 & 255, v & 255])
def stat(base, dv, ev, lvl, hp):
    import math
    v = ((base + dv) * 2 + min(255, int(math.ceil(math.sqrt(ev))) // 4)) * lvl // 100
    return v + lvl + 10 if hp else v + 5
def boxmon(r, sp, lvl, otid):
    name, dex, t1, t2, rate, base = SPEC[sp]
    dvs = [r.randrange(16) for _ in range(4)]  # atk def spd spc
    hpdv = (dvs[0] & 1) << 3 | (dvs[1] & 1) << 2 | (dvs[2] & 1) << 1 | (dvs[3] & 1)
    evs = [r.randrange(0, 20000) for _ in range(5)]
    mv = r.sample(MOVES, 4)
    m = bytearray(33)
    hp = stat(base[0], hpdv, evs[0], lvl, True)
    m[0] = sp; m[1:3] = be16(hp); m[3] = lvl; m[4] = 0; m[5] = t1; m[6] = t2; m[7] = rate
    m[8:12] = bytes(mv); m[12:14] = be16(otid); m[14:17] = expfor(sp, lvl).to_bytes(3, 'big')
    for i, e in enumerate(evs): m[17 + 2 * i:19 + 2 * i] = be16(e)
    m[27] = dvs[0] << 4 | dvs[1]; m[28] = dvs[2] << 4 | dvs[3]
    m[29:33] = bytes(PPS[x] for x in mv)
    return bytes(m)
def partymon(bm):
    sp = bm[0]; lvl = bm[3]; base = SPEC[sp][5]
    dv = [bm[27] >> 4, bm[27] & 15, bm[28] >> 4, bm[28] & 15]
    hpdv = (dv[0] & 1) << 3 | (dv[1] & 1) << 2 | (dv[2] & 1) << 1 | (dv[3] & 1)
    ev = [int.from_bytes(bm[17 + 2 * i:19 + 2 * i], 'big') for i in range(5)]
    st = [stat(base[0], hpdv, ev[0], lvl, True)] + [stat(base[i], dv[i - 1], ev[i], lvl, False) for i in (1, 2, 3, 4)]
    return bytes(bm) + bytes([lvl]) + b''.join(be16(s) for s in st)

def put_list(sav, base, cap, msize, mons):  # mons: [(datos, ot, mote)]
    sav[base] = len(mons)
    for i in range(cap + 1): sav[base + 1 + i] = mons[i][0][0] if i < len(mons) else 0xFF if i == len(mons) else 0
    d = base + 2 + cap
    for i, (m, ot, nick) in enumerate(mons):
        sav[d + msize * i:d + msize * (i + 1)] = m
        sav[d + msize * cap + 11 * i:d + msize * cap + 11 * (i + 1)] = txt(ot)
        sav[d + msize * cap + 11 * cap + 11 * i:d + msize * cap + 11 * cap + 11 * (i + 1)] = txt(nick)
def get_list(sav, base, cap, msize):
    n = sav[base]; out = []; err = []
    if n > cap: return [], ['cuenta %d mayor que %d' % (n, cap)]
    d = base + 2 + cap
    if sav[base + 1 + n] != 0xFF: err.append('falta el FF tras %d especies' % n)
    for i in range(n):
        m = bytes(sav[d + msize * i:d + msize * (i + 1)])
        if sav[base + 1 + i] != m[0]: err.append('especie de la lista %02X distinta a la del Pokemon %02X (lugar %d)' % (sav[base + 1 + i], m[0], i))
        out.append((m, untxt(sav[d + msize * cap + 11 * i:d + msize * cap + 11 * (i + 1)]),
                    untxt(sav[d + msize * cap + 11 * cap + 11 * i:d + msize * cap + 11 * cap + 11 * (i + 1)])))
    return out, err
def sums(sav):
    sav[0x3523] = (~sum(sav[0x2598:0x3523])) & 0xFF
    for bank in BANKS:
        for b in range(6):
            o = bank + b * 0x462
            sav[bank + 0x1A4D + b] = (~sum(sav[o:o + 0x462])) & 0xFF
        sav[bank + 0x1A4C] = (~sum(sav[bank:bank + 0x1A4C])) & 0xFF
def items(sav, base):
    n = sav[base]; return [(sav[base + 1 + 2 * i], sav[base + 2 + 2 * i]) for i in range(n)], sav[base + 1 + 2 * n] == 0xFF

def crear(path, name, seed):
    r = random.Random(seed); sav = bytearray(0x8000); otid = 10000 + seed
    sav[0x2598:0x25A3] = txt(name)
    sp = list(SPEC); k = [0]
    def nuevo(lvl=None):
        k[0] += 1
        s = sp[(k[0] * 7 + seed) % len(sp)]
        return (boxmon(r, s, lvl or r.randrange(5, 60), otid), name, '%s%02d' % (name[0], k[0]))
    party = [nuevo(50) for _ in range(4)]
    put_list(sav, PARTY, 6, 44, [(partymon(m), o, n) for m, o, n in party])
    cur = 2  # caja actual: la 3
    cuenta = [3, 20, 5, 0, 1, 19, 0, 2, 0, 0, 4, 1]   # caja 2 llena, caja 6 casi llena, varias vacias
    boxes = [[nuevo() for _ in range(c)] for c in cuenta]
    for b in range(12):
        base = BANKS[b // 6] + (b % 6) * 0x462
        put_list(sav, base, 20, 33, boxes[b])
    put_list(sav, BOX, 20, 33, boxes[cur])
    allm = party + [m for b in boxes for m in b]
    for m, _, _ in allm:
        dex = SPEC[m[0]][1]
        for base in (0x25A3, 0x25B6): sav[base + (dex - 1) // 8] |= 1 << ((dex - 1) % 8)
    bag = [(0x04, 10), (0x14, 5), (0x10, 3), (0x28, 1), (0xC9, 1)]            # Poke Ball, Pocion, Superpocion, Caramelo raro, MT01
    pc = [(0x01, 1), (0x13, 7), (0x35, 2), (0x0A, 4)]                          # Master Ball, Restaurar todo, Revivir, Piedra lunar
    sav[0x25C9] = len(bag)
    for i, (a, q) in enumerate(bag): sav[0x25CA + 2 * i] = a; sav[0x25CB + 2 * i] = q
    sav[0x25CA + 2 * len(bag)] = 0xFF
    sav[0x27E6] = len(pc)
    for i, (a, q) in enumerate(pc): sav[0x27E7 + 2 * i] = a; sav[0x27E8 + 2 * i] = q
    sav[0x27E7 + 2 * len(pc)] = 0xFF
    sav[0x25F3:0x25F6] = bytes([0x01, 0x23, 0x45]); sav[0x25F6:0x2601] = txt('GARY'); sav[0x2602] = 0xFF
    sav[0x2605:0x2607] = be16(otid)
    sav[0x284C] = 0x80 | cur
    sav[0x2CED:0x2CF1] = bytes([10, 0, 30, 0])
    sav[0x29F7] |= 0x20; sav[0x260A] = 0x29; sav[0x2613] = 6   # con Pokedex, guardado en un Centro Pokemon
    sums(sav)
    open(path, 'wb').write(sav)

def leer(path):
    sav = bytearray(open(path, 'rb').read()); e = []
    if sav[0x3523] != (~sum(sav[0x2598:0x3523])) & 0xFF: e.append('suma principal mala')
    cur = sav[0x284C] & 0x7F
    party, er = get_list(sav, PARTY, 6, 44); e += ['equipo: ' + x for x in er]
    boxes = []
    for b in range(12):
        base = BANKS[b // 6] + (b % 6) * 0x462
        bl, er = get_list(sav, base, 20, 33)
        if b == cur: bl, er = get_list(sav, BOX, 20, 33)
        e += ['caja %d: %s' % (b + 1, x) for x in er]; boxes.append(bl)
    banksum = []
    for bank in BANKS:
        ok = sav[bank + 0x1A4C] == (~sum(sav[bank:bank + 0x1A4C])) & 0xFF
        okb = [sav[bank + 0x1A4D + b] == (~sum(sav[bank + b * 0x462:bank + (b + 1) * 0x462])) & 0xFF for b in range(6)]
        banksum.append((ok, okb))
    for i, (m, ot, nick) in enumerate(party):   # las estadisticas del equipo deben salir de los datos del Pokemon
        if m[0] in SPEC and partymon(m[:33]) != m: e.append('equipo %d (%s): nivel/estadisticas no corresponden: %s contra %s' % (i, nick, m[33:].hex(), partymon(m[:33])[33:].hex()))
    bag, ok1 = items(sav, 0x25C9); pc, ok2 = items(sav, 0x27E6)
    if not ok1: e.append('mochila sin FF final')
    if not ok2: e.append('objetos del PC sin FF final')
    return dict(sav=sav, cur=cur, party=party, boxes=boxes, bag=bag, pc=pc, err=e, banksum=banksum, dex=(bytes(sav[0x25A3:0x25B6]), bytes(sav[0x25B6:0x25C9])))
def ver(path):
    s = leer(path)
    print('%s: caja actual %d (byte %02X) | equipo %d: %s' % (path.split('/')[-1], s['cur'] + 1, s['sav'][0x284C], len(s['party']), ' '.join(n for _, _, n in s['party'])))
    for b, bl in enumerate(s['boxes']):
        if bl: print('  caja %2d (%2d): %s' % (b + 1, len(bl), ' '.join(n for _, _, n in bl)))
    print('  mochila %s | PC %s' % (s['bag'], s['pc']))
    print('  sumas de bancos: %s' % ' '.join(('ok' if ok else 'MAL') + '[' + ''.join('.' if x else 'X' for x in okb) + ']' for ok, okb in s['banksum']))
    for x in s['err']: print('  ERROR:', x)
    return s
def donde(s):
    d = {}
    for i, (m, ot, n) in enumerate(s['party']): d.setdefault((m[:33], ot, n), []).append('equipo')
    for b, bl in enumerate(s['boxes']):
        for i, (m, ot, n) in enumerate(bl): d.setdefault((m, ot, n), []).append('caja %d' % (b + 1))
    return d
def cmp(a, b):
    A = ver(a); B = ver(b); da = donde(A); db = donde(B); bad = 0
    for k in da:
        if k not in db:
            # mismo mote con datos distintos?
            alt = [x for x in db if x[2] == k[2] and x[1] == k[1]]
            if alt:
                dif = [i for i in range(33) if alt[0][0][i] != k[0][i]]
                # al sacar de caja el juego original recalcula HP actual (1-2) y nivel de caja (3): se anota aparte
                tag = 'solo HP/nivel' if set(dif) <= {1, 2, 3} else 'DATOS CAMBIADOS'
                if tag != 'solo HP/nivel': bad += 1
                print('  %s: %s -> %s, %s en bytes %s: %s -> %s' % (k[2], da[k], db[alt[0]], tag, dif, k[0].hex(), alt[0][0].hex()))
            else: print('  PERDIDO: %s (%s)' % (k[2], da[k])); bad += 1
        elif da[k] != db[k]: print('  %s: %s -> %s' % (k[2], da[k], db[k]))
    for k in db:
        if k not in da and not [x for x in da if x[2] == k[2] and x[1] == k[1]]: print('  NUEVO: %s en %s' % (k[2], db[k])); bad += 1
    ca = collections.Counter(n for _, _, n in da);
    for k, v in db.items():
        if len(v) > 1: print('  DUPLICADO: %s en %s' % (k[2], v)); bad += 1
    if A['bag'] != B['bag']: print('  mochila: %s -> %s' % (A['bag'], B['bag']))
    if A['pc'] != B['pc']: print('  objetos PC: %s -> %s' % (A['pc'], B['pc']))
    if A['dex'] != B['dex']: print('  Pokedex cambio')
    # bytes de la partida principal que cambiaron fuera de equipo/caja/objetos/suma/caja actual
    sa, sb = A['sav'], B['sav']
    otros = [i for i in range(0x2598, 0x3523) if sa[i] != sb[i] and not (0x25C9 <= i < 0x25F3 or 0x27E6 <= i < 0x284D or PARTY <= i < PARTY + 0x194 or BOX <= i < BOX + 0x462 or 0x25A3 <= i < 0x25C9)]
    if otros: print('  OTROS BYTES de la partida cambiados: %s' % [hex(x) for x in otros[:40]]); bad += 1
    print('  => %s' % ('BIEN' if bad == 0 and not B['err'] else 'HAY PROBLEMAS'))
if __name__ == '__main__':
    c = sys.argv[1]
    if c == 'crear': crear(sys.argv[2], sys.argv[3], int(sys.argv[4])); ver(sys.argv[2])
    elif c == 'ver': ver(sys.argv[2])
    elif c == 'cmp': cmp(sys.argv[2], sys.argv[3])

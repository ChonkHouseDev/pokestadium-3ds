#!/usr/bin/env python3
# tr.py volcado.psfd: traza completa de la DL (profundidad, comando, palabras) con VTX/MTX/TRI decodificados
import sys, struct
d = open(sys.argv[1], 'rb').read()
magic, base, size, dl = struct.unpack('>4I', d[:16]); mem = d[16:]
def ok(a): return base <= a < base + len(mem)
def w32(a): return struct.unpack_from('>I', mem, a - base)[0]
names = {0x01:'VTX',0x02:'MODVTX',0x03:'CULLDL',0x04:'BRANCHZ',0x05:'TRI1',0x06:'TRI2',0x07:'QUAD',0xDE:'DL',0xDF:'ENDDL',0xE4:'TEXRECT',0xE5:'TEXRECTFLIP',0xF6:'FILLRECT',0xFD:'SETTIMG',0xFF:'SETCIMG',0xFE:'SETZIMG',
         0xF5:'SETTILE',0xF3:'LOADBLOCK',0xF4:'LOADTILE',0xF0:'LOADTLUT',0xF2:'SETTILESIZE',0xED:'SCISSOR',0xFC:'COMBINE',0xE2:'OML',0xE3:'OMH',0xDA:'MTX',0xDB:'MOVEWORD',0xDC:'MOVEMEM',
         0xD7:'TEXTURE',0xD9:'GEOM',0xFA:'PRIM',0xFB:'ENV',0xF7:'FILLCOLOR',0xF8:'FOG',0xF9:'BLEND',0xE7:'PIPESYNC',0xE6:'LOADSYNC',0xE9:'FULLSYNC',0xE8:'TILESYNC',0xEE:'PRIMDEPTH',0xE1:'RDPHALF1',0xF1:'RDPHALF2',0xD8:'POPMTX',0x4E:'PS_HUD',0xE0:'SPNOOP',0x00:'NOOP',0xD6:'DMA_IO',0xD5:'SPECIAL1',0xD3:'SPECIAL3',0xDD:'LOAD_UCODE'}
seg = [0]*16
def res(a):
    if ok(a): return a
    s = (a >> 24) & 0xF
    r = seg[s] + (a & 0xFFFFFF)
    return r
stack = []; pc = dl; n = 0
while n < 400000:
    if not ok(pc): print('!! pc fuera %08x' % pc); break
    w0, w1 = w32(pc), w32(pc + 4); op = w0 >> 24; at = pc; pc += 8; n += 1
    nm = names.get(op, '%02X' % op); ex = ''
    if op == 0xDB:
        idx = (w0 >> 16) & 0xFF; off = w0 & 0xFFFF
        if idx == 6: seg[off // 4] = w1; ex = 'seg %d = %08x' % (off // 4, w1)
        elif idx == 8: ex = 'fog mul %d off %d' % (struct.unpack('>h', struct.pack('>H', w1 >> 16))[0], struct.unpack('>h', struct.pack('>H', w1 & 0xFFFF))[0])
        elif idx == 2: ex = 'numlights'
        else: ex = 'idx %d off %d' % (idx, off)
    elif op == 0x01:
        nv = (w0 >> 12) & 0xFF; v0 = ((w0 & 0xFF) >> 1) - nv; a = res(w1)
        ex = 'n %d v0 %d @%08x%s' % (nv, v0, a, '' if ok(a) else ' FUERA')
        if ok(a) and '-p' in sys.argv:
            for i in range(nv):
                x, y, z, f, s, t, r, g, b, al = struct.unpack_from('>hhhHhhBBBB', mem, a - base + 16 * i)
                ex += '\n        v%d (%d,%d,%d) st %d,%d rgba %02x%02x%02x%02x' % (v0 + i, x, y, z, s, t, r, g, b, al)
    elif op == 0xDA:
        p = w0 & 0xFF; a = res(w1); p ^= 1
        ex = '%s %s %s @%08x' % ('PROJ' if p & 4 else 'MV', 'LOAD' if p & 2 else 'MUL', 'PUSH' if p & 1 else 'NOPUSH', a)
        if ok(a) and '-m' in sys.argv:
            m = struct.unpack_from('>16h', mem, a - base); fr = struct.unpack_from('>16H', mem, a - base + 32)
            v = [m[i] + fr[i] / 65536.0 for i in range(16)]
            ex += '\n        ' + ' | '.join(' '.join('%.3f' % v[r * 4 + c] for c in range(4)) for r in range(4))
    elif op in (5, 6, 7):
        if op == 5: ex = '%d %d %d' % (((w0 >> 16) & 0xFF) // 2, ((w0 >> 8) & 0xFF) // 2, (w0 & 0xFF) // 2)
        else: ex = '%d %d %d, %d %d %d' % (((w0 >> 16) & 0xFF) // 2, ((w0 >> 8) & 0xFF) // 2, (w0 & 0xFF) // 2, ((w1 >> 16) & 0xFF) // 2, ((w1 >> 8) & 0xFF) // 2, (w1 & 0xFF) // 2)
    elif op == 0xFD: ex = 'fmt %d siz %d @%08x' % ((w0 >> 21) & 7, (w0 >> 19) & 3, res(w1))
    elif op == 0xF5: ex = 'fmt %d siz %d line %d tmem %d tile %d pal %d cmt %d maskt %d cms %d masks %d' % ((w0 >> 21) & 7, (w0 >> 19) & 3, (w0 >> 9) & 0x1FF, w0 & 0x1FF, (w1 >> 24) & 7, (w1 >> 20) & 15, (w1 >> 18) & 3, (w1 >> 14) & 15, (w1 >> 8) & 3, (w1 >> 4) & 15)
    elif op == 0xF2: ex = 'tile %d (%g,%g)-(%g,%g)' % ((w1 >> 24) & 7, ((w0 >> 12) & 0xFFF) / 4, (w0 & 0xFFF) / 4, ((w1 >> 12) & 0xFFF) / 4, (w1 & 0xFFF) / 4)
    elif op == 0xD7: ex = 'on %d tile %d lvl %d s %04x t %04x' % (w0 & 0xFF, (w0 >> 8) & 7, (w0 >> 11) & 7, w1 >> 16, w1 & 0xFFFF)
    elif op == 0xD9: ex = 'clr %06x set %08x' % (~w0 & 0xFFFFFF, w1)
    print('%s%08x %-10s %08x %08x %s' % ('  ' * len(stack), at, nm, w0, w1, ex))
    if op in (0xE4, 0xE5): pc += 16
    if op == 0xDE:
        a = res(w1)
        if (w0 >> 16) & 0xFF == 0: stack.append(pc)
        if not ok(a): print('!! DL a %08x' % a); 
        else: pc = a
        continue
    if op == 0xDF:
        if not stack: break
        pc = stack.pop(); continue

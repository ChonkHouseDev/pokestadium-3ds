#!/usr/bin/env python3
"""Enlace de la escena de entrada del Stadium japones (opcional) dentro del bloque.

Los archivos de la escena (vs_scene.c y vs_trainer_panel.c del decomp pokestadium0, mas port/jp/ps_jp_extra.c) se
compilan tal cual y todos sus simbolos llevan el prefijo jp_ para que no choquen con los del juego de EE. UU.
Aqui se resuelve lo que les falta, sin datos de ninguna ROM:
  - motor: mismo nombre en la lista de simbolos de la ROM de EE. UU. -> func_XXXXXXXX / D_XXXXXXXX del bloque
  - direcciones de fragments japoneses (D_8Fxxxxxx, D_822xxxxx...): zona fija donde port/ps_jp.c carga ese fragment
  - D_00xxxxxx: posiciones en la ROM japonesa (el numero mismo)
Tambien genera las tablas con las que ps_jp.c corrige los apuntadores que traen los datos japoneses.
Uso: gen_jp_scene.py <nm de jp_all.o> <jp symbol_addrs.txt> <dir us_syms> <nm de los objetos del bloque> <renombres.txt> <salida.ld> <tablas.c>
"""
import re, sys, glob

IMAGES = {  # vram -> (inicio en la ROM japonesa, fin, bytes reservados)
    0x81000000: (0x15B1A0, 0x15E220, 0x3400), 0x82200000: (0x232510, 0x235D70, 0x3900), 0x82400000: (0x175570, 0x17BE70, 0x6800),
    0x8F200000: (0x3553C0, 0x35C390, 0x12C00), 0x8F500000: (0x35C390, 0x35E790, 0x5A00), 0x8F600000: (0x35E790, 0x369F50, 0x26C00),
    0x8EB00000: (0x369F50, 0x37B090, 0x44000), 0x8F300000: (0x320820, 0x326700, 0xDC00), 0x8F900000: (0x3139C0, 0x317EF0, 0x9000),
    0x8FA00000: (0x317EF0, 0x31C300, 0x9000), 0x8FB00000: (0x31C300, 0x320820, 0x9000), 0x8EC00000: (0x37B090, 0x380D60, 0x14A00),
    0x8ED00000: (0x380D60, 0x385D60, 0xFE00), 0x8EE00000: (0x385D60, 0x38A540, 0x10200),
}
STUB = 'jp_ps_jp_stub'
MANUAL = {  # nombre en el decomp japones -> simbolo del bloque
    'MainPool_PushState': 'main_pool_push_state', 'MainPool_PopState': 'main_pool_pop_state', 'MainPool_GetAvailable': 'main_pool_get_available',
    'MainPool_Alloc': 'main_pool_alloc', 'GeoLayout_Process': 'process_geo_layout',
    'Fragment_Load': 'ps_jp_Fragment_Load', 'Fragment_LoadAndCall': 'ps_jp_Fragment_LoadAndCall', 'Asset_LoadToSegment': 'ps_jp_Asset_LoadToSegment',
    'BinArchive_Open': 'ps_jp_BinArchive_Open', 'BinArchive_GetFile': 'ps_jp_BinArchive_GetFile', 'GfxImage_Allocate': 'ps_jp_GfxImage_Allocate', 'GfxImage_SetRenderTarget': 'ps_jp_GfxImage_SetRenderTarget', 'ps_jp_image_drawn': 'ps_jp_image_drawn', 'PokeIcon_SetMonSlot': 'jp_ps_jp_SetMonSlot', 'ps_jp_icon_slot': 'ps_jp_icon_slot', 'ps_jp_trace': 'ps_jp_trace',
    'memcpy': 'memcpy', 'memset': 'memset', 'sinf': 'sinf', 'cosf': 'cosf', 'ps_geo_cur_mtx': 'ps_geo_cur_mtx', 'ps_jp_tracef': 'ps_jp_tracef',
    'D_80060484': 'D_8006F084', 'D_80060488': 'D_8006F088', 'D_80060490': 'D_8006F090', 'D_8006049C': 'D_8006F09C', 'sqrtf': 'sqrtf', 'D_80060290': 'D_8006F4E0', 'D_80060398': 'D_8006F630',
    'gUiEnvTintTextureDL': 'D_8006F518', 'gUiRestoreTextureDL': 'D_8006F630',
}
for s in ('Memmap_GetFragmentVaddr', '_bcopy', 'osSetIntMask', 'osVirtualToPhysical'): MANUAL[s] = s   # definidos en port/, no en obj/
for s in ('Audio_StopSoundByCategory', 'Audio_PlayCommand', 'Audio_PlayMusicIfChanged', 'Audio_StopMusic', 'Audio_PlaySoundEffectById',
          'Save_GetGbPlayerSettings', 'Save_GetProgressFlags', 'Save_LoadSram', 'Save_SetGbPlayerFrameTier', 'Save_SetProgressFlags',
          'Save_CommitTypedRecord', 'Save_ResetAndCommitTypedRecord', 'Tournament_SaveContinueData'):
    MANUAL[s] = STUB

MANUAL.update({'Audio_PlayCommand': 'ps_jp_audio_cmd', 'Audio_PlayMusicIfChanged': 'ps_jp_music', 'Audio_StopMusic': 'ps_jp_music_stop', 'ps_jp_frame': 'ps_jp_frame', 'ps_geo_text_flush': 'ps_geo_text_flush', 'ps_jp_us_stadium_attach': 'ps_jp_us_stadium_attach', 'ps_jp_arena_add': 'ps_jp_arena_add', 'ps_jp_board_pos': 'ps_jp_board_pos', 'ps_jp_board_block': 'ps_jp_board_block', 'ps_jp_board_done': 'ps_jp_board_done', 'ps_jp_gold_frame': 'ps_jp_gold_frame'})   # sonido de la entrada: con lo de la US

# direcciones del motor japones sin nombre en su decomp -> simbolo del bloque
EXTRA_ADDR = {
    0x800AD1D0: 'D_800AC840',   # raices de los objetos que se dibujan (nodo de referencia de los geo layouts)
}

def load_syms(paths):
    m = {}
    for f in paths:
        for l in open(f, errors='ignore'):
            x = re.match(r'\s*([A-Za-z_]\w*)\s*=\s*0x([0-9A-Fa-f]+)', l)
            if x: m[x.group(1)] = int(x.group(2), 16)
    return m

def main():
    nm_jp, jpsyms, usdir, nm_blob, out_ren, out_ld, out_c = sys.argv[1:8]
    jp = load_syms([jpsyms]); us = load_syms(glob.glob(usdir + '/*.txt'))
    jp_by_addr = {}
    for s, a in jp.items(): jp_by_addr.setdefault(a, s)
    und, own = [], []
    for l in open(nm_jp):
        p = l.split()
        if len(p) == 2 and p[0] == 'U': und.append(p[1])
        elif len(p) == 3 and p[1].isupper(): own.append(p[2])
    defined = set()
    for l in open(nm_blob):
        p = l.split()
        if len(p) == 3 and p[1].isupper() and p[1] != 'U': defined.add(p[2])
    keep = lambda s: s.startswith('__')   # libgcc: sin prefijo
    open(out_ren, 'w').write(''.join('%s jp_%s\n' % (s, s) for s in sorted(set(und + own)) if not keep(s)))
    def image_of(a):
        base = a & 0xFFF00000
        return base if base in IMAGES and a - base < IMAGES[base][2] else None
    def us_symbol(name):
        if name not in us: return None
        for cand in ('func_%08X' % us[name], 'D_%08X' % us[name], name):
            if cand in defined: return cand
        return None
    addr_named = re.compile(r'(D|func)_([0-9A-F]{8})$')
    lines, missing = [], []
    for s in sorted(set(und)):
        if keep(s) or s in own: continue
        t = None
        x = addr_named.match(s)
        a = int(x.group(2), 16) if x else jp.get(s)
        if s in MANUAL: t = MANUAL[s]
        elif x and a < 0x10000000: t = '0x%X' % a                                    # posicion en la ROM o direccion segmentada
        elif a is not None and image_of(a) is not None: b = image_of(a); t = 'gPsJpImg_%03X + 0x%X' % (b >> 20, a - b)
        elif x:                                                                      # funcion o dato del motor japones sin nombre propio
            n = jp_by_addr.get(a)
            t = (MANUAL.get(n) or us_symbol(n)) if n else None
        elif s in defined: t = s                                                     # mismo nombre que en el juego de EE. UU.
        else: t = us_symbol(s)
        if t: lines.append('jp_%s = %s;' % (s, t))
        else: missing.append('%s%s' % (s, ' (japones %08X %s)' % (a, jp_by_addr.get(a, '')) if a is not None else ''))
    open(out_ld, 'w').write('/* generado por tools/port/gen_jp_scene.py */\n' + '\n'.join(lines) + '\n')

    c = ['/* generado por tools/port/gen_jp_scene.py: sin datos de ninguna ROM. Las direcciones del N64 van invertidas (~):',
         '   al arrancar, el port traduce a nativo toda palabra de sus datos que parezca direccion del juego de EE. UU. */', '#include "ps_jp.h"']
    for b, (a, e, n) in sorted(IMAGES.items()): c.append('unsigned char gPsJpImg_%03X[0x%X] __attribute__((aligned(16)));' % (b >> 20, n))
    c.append('const PsJpImage gPsJpImages[] = {')
    for b, (a, e, n) in sorted(IMAGES.items()): c.append('    { 0x%08Xu, 0x%Xu, 0x%Xu, 0x%Xu, gPsJpImg_%03X },' % (b ^ 0xFFFFFFFF, a, e, n, b >> 20))
    c.append('};\nconst unsigned int gPsJpImagesCount = sizeof gPsJpImages / sizeof gPsJpImages[0];')
    # direcciones del motor japones -> simbolo nativo (para apuntadores escritos en los datos)
    ents = {}
    for s, a in jp.items():
        if not (0x80000400 <= a < 0x80100000): continue
        t = ('jp_' + s) if s in own else (MANUAL.get(s) if MANUAL.get(s) not in (None, STUB) else us_symbol(s))
        if t and not t.startswith('0x') and '+' not in t: ents[a] = t
    for a, t in EXTRA_ADDR.items(): ents[a] = t
    # funciones de la escena dentro de sus fragments
    for s in own:
        a = jp.get(s)
        if a is not None and image_of(a) is not None: ents[a] = 'jp_' + s
    for t in sorted(set(ents.values())): c.append('extern char %s[];' % t)
    c.append('const PsJpSym gPsJpSyms[] = {')
    for a in sorted(ents): c.append('    { 0x%08Xu, %s },' % (a ^ 0xFFFFFFFF, ents[a]))
    c.append('};\nconst unsigned int gPsJpSymsCount = sizeof gPsJpSyms / sizeof gPsJpSyms[0];')
    open(out_c, 'w').write('\n'.join(c) + '\n')
    for m in missing: print('FALTA', m)
    print('[jp] %d simbolos resueltos, %d faltan, %d direcciones japonesas en la tabla' % (len(lines), len(missing), len(ents)))
    return 1 if missing else 0

sys.exit(main())

#!/usr/bin/env python3
# gen_online_gfx.py <baserom.z64> <salida.inc> [vista.png]   (salida: src/fragments/37/online_gfx.inc)
# Graficos propios para el letrero ONLINE del mapa (donde estaba GB Tower) y el simbolo de senal en la pantalla del
# Game Boy del fondo. No copia graficos del juego: lee los letreros de la ROM solo para saber que pixeles tapaban
# las letras viejas (lo que NO coincide entre los letreros de una linea, que comparten la misma nube) y ahi pone lo
# dibujado aqui: relleno de nube, contorno, y las letras ONLINE. Lo que queda igual que en la ROM se marca "conservar".
import struct, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

KEEP, CLEAR = 0xFFFE, 0x0000
W, H = 196, 64

def yay0(d):
    size, lo, co = struct.unpack('>III', d[4:16])
    out = bytearray(); mp = 16; mask = 0; bits = 0
    while len(out) < size:
        if bits == 0:
            mask = struct.unpack('>I', d[mp:mp+4])[0]; mp += 4; bits = 32
        if mask & 0x80000000:
            out.append(d[co]); co += 1
        else:
            v = struct.unpack('>H', d[lo:lo+2])[0]; lo += 2
            dist = (v & 0xFFF) + 1; n = v >> 12
            if n == 0: n = d[co] + 0x12; co += 1
            else: n += 2
            for _ in range(n): out.append(out[-dist])
        mask = (mask << 1) & 0xFFFFFFFF; bits -= 1
    return bytes(out)

def labels(rom):
    off = 0x4D1330
    assert rom[off:off+8] == b'PERS-SZP'
    ui = yay0(rom[off+0x18:])
    res = {}
    for i, a in ((0, 0x6E60), (1, 0xD060), (3, 0x19460), (4, 0x1F660), (6, 0x2BA60), (7, 0x31C60)):
        res[i] = np.frombuffer(ui[a:a+W*H*2], '>u2').reshape(H, W).astype(np.uint16)
    return res

def to_rgba(v):
    v = v.astype(int)
    return np.stack([((v >> 11) & 31) * 255 // 31, ((v >> 6) & 31) * 255 // 31, ((v >> 1) & 31) * 255 // 31, (v & 1) * 255], -1).astype(np.uint8)
def to_5551(rgb):
    r, g, b = [int(round(c * 31 / 255)) for c in rgb]
    return (r << 11) | (g << 6) | (b << 1) | 1

def letters():
    """ONLINE con el estilo de los letreros: contorno negro grueso, relleno plateado con brillo arriba y sombra abajo."""
    S = 8
    f = ImageFont.truetype('/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf', 31 * S)
    big = Image.new('L', (W * S, H * S), 0); d = ImageDraw.Draw(big)
    text, x = 'ONLINE', 0
    adv = [d.textlength(c, font=f) for c in text]
    track = 2.6 * S
    total = sum(adv) + track * (len(text) - 1)
    x = (97.5 * S) - total / 2
    for c, a in zip(text, adv):
        d.text((x, 12.6 * S), c, font=f, fill=255); x += a + track
    k = 1.05                                                         # letras anchas, como las del juego
    wide = big.resize((int(W * S * k), H * S), Image.LANCZOS)
    off = int((wide.size[0] - W * S) / 2 + 0.5 * S)
    big = wide.crop((off, 0, off + W * S, H * S))
    big = big.filter(ImageFilter.GaussianBlur(1.5 * S)).point(lambda v: 255 if v > 118 else 0)  # esquinas redondeadas
    fill = np.array(big.resize((W, H), Image.LANCZOS)) > 110
    def dil(m, n):
        im = Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(2 * n + 1))
        return np.array(im) > 0
    def shift(m, dx, dy):
        o = np.zeros_like(m); ys, xs = np.nonzero(m)
        ys2, xs2 = ys + dy, xs + dx; ok = (ys2 >= 0) & (ys2 < H) & (xs2 >= 0) & (xs2 < W)
        o[ys2[ok], xs2[ok]] = True; return o
    fill = dil(fill, 1)  # un pixel mas gruesa, como las del juego
    out2 = dil(fill, 2); out1 = dil(fill, 1)
    img = np.zeros((H, W), np.uint16); m = np.zeros((H, W), bool)
    ys = np.nonzero(fill.any(1))[0]; y0, y1 = ys.min(), ys.max()
    for y in range(H):
        for x in range(W):
            if fill[y, x]:
                t = (y - y0) / max(1, y1 - y0)
                g = 222 - 30 * t                                   # plateado, mas claro arriba
                if not fill[y - 1, x] or not fill[y, x - 1]: g = 255    # brillo arriba e izquierda
                elif not fill[y + 1, x] or not fill[y, x + 1]: g = 150  # sombra abajo y derecha
                img[y, x] = to_5551((g, g, g)); m[y, x] = True
            elif out1[y, x]:
                img[y, x] = to_5551((24, 24, 24)); m[y, x] = True
            elif out2[y, x]:
                img[y, x] = to_5551((24, 24, 24)); m[y, x] = True
    return img, m

def build(rom):
    L = labels(rom)
    stack = np.stack([L[i] for i in sorted(L)])
    known = np.zeros((H, W), bool); cons = np.zeros((H, W), np.uint16)
    for y in range(H):
        for x in range(W):
            vals = list(stack[:, y, x]); best = max(set(vals), key=vals.count)
            if vals.count(best) == len(vals): known[y, x] = True; cons[y, x] = best  # solo si todos coinciden
    # zona de letras viejas: lo no conocido, cerrado (los puntos sueltos conocidos dentro de la franja no son de fiar)
    unk = ~known
    im = Image.fromarray((unk * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))
    zone = (np.array(im) > 0) | unk
    zone[:10] = False; zone[50:] = False
    # silueta de la nube dentro de la zona: bordes dibujados aqui (arcos entre los puntos donde el contorno conocido
    # entra a la franja)
    sil = np.zeros((H, W), bool)
    ys = np.nonzero(zone.any(1))[0]; ya, yb = ys.min(), ys.max()
    def smooth(t): t = min(1.0, max(0.0, t)); return t * t * (3 - 2 * t)
    for y in range(ya, yb + 1):
        xl = 55 - 25 * smooth((y - 15) / 21.0)                       # de (55, 15) a (30, 36) y recto hasta abajo
        if y <= 26: xr = 150 + (y - 14) * 28 / 12.0                  # lo que se conoce del lado derecho
        elif y <= 33: xr = 179
        else: xr = 179 - 14 * ((y - 33) / 11.0) ** 1.3
        sil[y, int(round(xl)):int(round(xr)) + 1] = True
    patch = np.full((H, W), KEEP, np.uint16)
    alpha_known = (cons & 1).astype(bool) & known & ~zone
    for y in range(H):
        for x in range(W):
            if not zone[y, x]: continue
            if not sil[y, x]: patch[y, x] = CLEAR; continue
            stops = [(14, (255, 255, 255)), (28, (225, 243, 245)), (36, (205, 232, 238)), (41, (160, 205, 220)), (46, (95, 160, 188))]
            c = stops[-1][1]
            for (y0, c0), (y1, c1) in zip(stops, stops[1:]):
                if y <= y1:
                    u = max(0.0, (y - y0) / (y1 - y0)); c = tuple(a + (b - a) * u for a, b in zip(c0, c1)); break
            if x > 158:                                           # la punta derecha de la nube va en sombra
                u = min(1.0, (x - 158) / 16.0); c = tuple(a + (b - a) * u for a, b in zip(c, (110, 172, 198)))
            patch[y, x] = to_5551(c)
    # contorno: pixeles de la silueta pegados a transparente (zona despejada o fuera de la nube conocida)
    solid = (sil & zone) | alpha_known
    edge = np.zeros((H, W), bool)
    for y in range(1, H - 1):
        for x in range(1, W - 1):
            if zone[y, x] and sil[y, x]:
                nb = solid[y-2:y+3, x-2:x+3] if 2 <= y < H - 2 and 2 <= x < W - 2 else solid[y-1:y+2, x-1:x+2]
                if not nb.all(): edge[y, x] = True
    patch[edge] = to_5551((24, 24, 24))
    img, m = letters()
    patch[m] = img[m]
    return patch, L[7]

def apply(patch, lab):
    out = lab.copy()
    out[patch == CLEAR] = 0
    sel = (patch != KEEP) & (patch != CLEAR)
    out[sel] = patch[sel]
    return out

def lcd_art():
    """Simbolo de senal para la pantalla del Game Boy del fondo (320x240): cobertura 0-255, 15x11."""
    S = 16; w, h = 15, 11
    im = Image.new('L', (w * S, h * S), 0); d = ImageDraw.Draw(im)
    cx, cy = w * S / 2, 9.4 * S
    for r in (3.4, 6.2, 9.0):
        d.arc([cx - r * S, cy - r * S, cx + r * S, cy + r * S], 222, 318, fill=255, width=int(1.45 * S))
    d.ellipse([cx - 1.25 * S, cy - 1.25 * S, cx + 1.25 * S, cy + 1.25 * S], fill=255)
    return np.array(im.resize((w, h), Image.LANCZOS))

def main():
    rom = open(sys.argv[1], 'rb').read()
    patch, lab = build(rom)
    lcd = lcd_art()
    with open(sys.argv[2], 'w') as f:
        f.write('/* Generado por tools/port/gen_online_gfx.py: letrero ONLINE (parche sobre el letrero de GB Tower) y simbolo de senal.\n')
        f.write(' * 0xFFFE = conservar el pixel de la ROM, 0x0000 = transparente, otro = color RGBA5551. */\n')
        ys = np.nonzero((patch != KEEP).any(1))[0]; y0, y1 = int(ys.min()), int(ys.max())
        f.write('#define PS_ONLINE_Y0 %d\n#define PS_ONLINE_ROWS %d\n' % (y0, y1 - y0 + 1))
        f.write('static const u16 sPsOnlinePatch[PS_ONLINE_ROWS][%d] = {\n' % W)
        for y in range(y0, y1 + 1):
            f.write('    { ' + ','.join('0x%04X' % v for v in patch[y]) + ' },\n')
        f.write('};\n')
        f.write('#define PS_WIFI_W %d\n#define PS_WIFI_H %d\n' % (lcd.shape[1], lcd.shape[0]))
        f.write('static const u8 sPsWifi[PS_WIFI_H][PS_WIFI_W] = {\n')
        for r in lcd: f.write('    { ' + ','.join('%3d' % v for v in r) + ' },\n')
        f.write('};\n')
    if len(sys.argv) > 3:
        out = apply(patch, lab)
        Image.fromarray(to_rgba(out), 'RGBA').save(sys.argv[3])

if __name__ == '__main__':
    main()

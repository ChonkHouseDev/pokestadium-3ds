#!/usr/bin/env python3
# gen_ui3ds.py <salida.inc> [vista.png]   (salida: platform/3ds/host/ui_gfx.inc)
# Graficos propios de la pantalla de abajo del 3DS (gb_saves.inc), dibujados aqui con suavizado:
#  - dos tamanos de letra (Poppins, licencia OFL) como mapas de cobertura de 8 bits;
#  - los cartuchos (rojo, azul, amarillo, verde, sin version y vacio), RGBA de 64x72.
# No usa ni copia graficos del juego ni de sus cajas.
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter
F = '/usr/share/fonts/truetype/google-fonts/'
FONTS = [('Sm', F + 'Poppins-Medium.ttf', 11, 14), ('Lg', F + 'Poppins-Bold.ttf', 17, 22)]
CW, CH = 64, 72
VERS = [('RED', (216, 40, 32)), ('BLUE', (36, 88, 224)), ('YELLOW', (240, 190, 20)), ('GREEN', (32, 160, 64)), ('?', (112, 120, 136))]

def font_data(path, size, cell_h):
    f = ImageFont.truetype(path, size)
    asc, desc = f.getmetrics()
    glyphs = []
    for c in range(32, 127):
        w = max(1, int(round(f.getlength(chr(c)))))
        im = Image.new('L', (w + 2, cell_h), 0)
        ImageDraw.Draw(im).text((1, (cell_h - (asc + desc)) // 2), chr(c), font=f, fill=255)
        glyphs.append((w, im))
    return glyphs

def cart(name, color):
    S = 6
    im = Image.new('RGBA', (CW * S, CH * S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    def R(x0, y0, x1, y1): return [x0 * S, y0 * S, x1 * S, y1 * S]
    # sombra
    sh = Image.new('RGBA', im.size, (0, 0, 0, 0)); ImageDraw.Draw(sh).rounded_rectangle(R(5, 5, 62, 71), radius=5 * S, fill=(0, 0, 20, 110))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(1.5 * S)))
    # cuerpo gris con degradado
    body = Image.new('RGBA', im.size, (0, 0, 0, 0)); bd = ImageDraw.Draw(body)
    shell = color if name not in (None, '?') else (190, 192, 202)      # la carcasa, del color de la version
    def sh_(k): return tuple(int(max(0, min(255, v * k))) for v in shell) + (255,)
    for y in range(CH * S):
        t = y / (CH * S)
        bd.line([(0, y), (CW * S, y)], fill=sh_(1.14 - 0.34 * t))
    mask = Image.new('L', im.size, 0); md = ImageDraw.Draw(mask)
    md.rounded_rectangle(R(3, 2, 59, 68), radius=4 * S, fill=255)
    md.polygon([(50 * S, 2 * S), (59 * S, 2 * S), (59 * S, 11 * S)], fill=0)       # esquina cortada
    md.polygon([(50 * S, 2 * S), (59 * S, 11 * S), (59 * S, 12 * S), (49 * S, 2 * S)], fill=255)
    md.rounded_rectangle(R(47, 2, 59, 16), radius=6 * S, fill=255)
    im.paste(body, (0, 0), mask)
    edge = mask.filter(ImageFilter.MaxFilter(2 * (S // 3) + 1))
    line = Image.new('RGBA', im.size, sh_(0.42))
    ring = Image.eval(Image.composite(edge, Image.new('L', im.size, 0), edge), lambda v: v)
    ring = Image.composite(Image.new('L', im.size, 0), ring, mask.filter(ImageFilter.MinFilter(2 * (S // 2) + 1)))
    im.paste(line, (0, 0), ring)
    d = ImageDraw.Draw(im)
    for i in range(4):                                                              # ranuras de agarre
        y = 6 + i * 2.6
        d.rounded_rectangle(R(10, y, 46, y + 1.2), radius=S // 2, fill=sh_(0.68))
        d.line([(10 * S, (y + 1.5) * S), (46 * S, (y + 1.5) * S)], fill=sh_(1.35), width=max(1, S // 3))
    if name is None:                                                                # sin cartucho: hueco
        d.rounded_rectangle(R(9, 19, 53, 55), radius=3 * S, fill=(120, 124, 140, 255), outline=(84, 86, 100, 255), width=S // 2)
    else:
        d.rounded_rectangle(R(8, 18, 54, 56), radius=3 * S, fill=sh_(0.30))
        lab = Image.new('RGBA', im.size, (0, 0, 0, 0)); ld = ImageDraw.Draw(lab)
        for y in range(19 * S, 55 * S):
            t = (y - 19 * S) / (36 * S)
            c = tuple(int(min(255, v * (0.74 - 0.26 * t))) for v in color)     # etiqueta: el mismo color, mas oscuro
            ld.line([(9 * S, y), (53 * S, y)], fill=c + (255,))
        lm = Image.new('L', im.size, 0); ImageDraw.Draw(lm).rounded_rectangle(R(9, 19, 53, 55), radius=int(2.4 * S), fill=255)
        im.paste(lab, (0, 0), lm)
        d = ImageDraw.Draw(im)
        d.rounded_rectangle(R(11, 21, 51, 29), radius=2 * S, fill=(250, 250, 252, 235))  # franja clara
        f = ImageFont.truetype(F + 'Poppins-Bold.ttf', int((8.2 if len(name) > 5 else 11) * S))
        w = f.getlength(name); x = 31 * S - w / 2; y = (35.5 if len(name) > 5 else 33.5) * S
        d.text((x + S * 0.6, y + S * 0.6), name, font=f, fill=(0, 0, 0, 120))
        d.text((x, y), name, font=f, fill=(255, 255, 255, 255))
        shine = Image.new('RGBA', im.size, (0, 0, 0, 0))
        ImageDraw.Draw(shine).polygon([(9 * S, 19 * S), (30 * S, 19 * S), (16 * S, 55 * S), (9 * S, 55 * S)], fill=(255, 255, 255, 34))
        im.paste(shine, (0, 0), Image.composite(shine.split()[3], Image.new('L', im.size, 0), lm))
    d = ImageDraw.Draw(im)
    d.polygon([(27 * S, 60 * S), (35 * S, 60 * S), (31 * S, 65 * S)], fill=sh_(0.62))  # flecha
    return im.resize((CW, CH), Image.LANCZOS)

def main():
    out = open(sys.argv[1], 'w')
    out.write('/* Generado por tools/port/gen_ui3ds.py: letras (cobertura 0-255) y cartuchos (RGBA) de la pantalla de abajo. */\n')
    out.write('struct GbUiFont { int h; const u16* off; const u8* w; const u8* a; };\n')
    for tag, path, size, ch in FONTS:
        gl = font_data(path, size, ch)
        offs, data = [], []
        for w, im in gl:
            offs.append(len(data)); data += list(im.tobytes())
        out.write('static const u8 kUiW%s[95] = { %s };\n' % (tag, ','.join(str(w) for w, _ in gl)))
        out.write('static const u16 kUiO%s[95] = { %s };\n' % (tag, ','.join(str(o) for o in offs)))
        out.write('static const u8 kUiA%s[%d] = {\n' % (tag, len(data)))
        for i in range(0, len(data), 40): out.write(' ' + ','.join(str(v) for v in data[i:i + 40]) + ',\n')
        out.write('};\nstatic const GbUiFont kUiFont%s = { %d, kUiO%s, kUiW%s, kUiA%s };\n' % (tag, ch, tag, tag, tag))
        assert len(data) < 65536
    sprites = [cart(n, c) for n, c in VERS] + [cart(None, None)]
    out.write('#define GB_CART_W %d\n#define GB_CART_H %d\n' % (CW, CH))
    out.write('static const u8 kUiCart[%d][%d] = {\n' % (len(sprites), CW * CH * 4))
    for im in sprites:
        b = list(im.tobytes())
        out.write(' {\n')
        for i in range(0, len(b), 48): out.write('  ' + ','.join(str(v) for v in b[i:i + 48]) + ',\n')
        out.write(' },\n')
    out.write('};\n')
    if len(sys.argv) > 2:
        S = Image.new('RGBA', (len(sprites) * (CW + 6) * 3, CH * 3 + 12), (20, 34, 112, 255))
        for i, im in enumerate(sprites): S.alpha_composite(im.resize((CW * 3, CH * 3), Image.NEAREST), (6 + i * (CW + 6) * 3, 6))
        S.convert('RGB').save(sys.argv[2])

if __name__ == '__main__':
    main()

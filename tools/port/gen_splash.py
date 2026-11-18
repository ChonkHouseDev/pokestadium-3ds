#!/usr/bin/env python3
# gen_splash.py: el logo de arranque (platform/3ds/host/assets/pokethrasher.png, del autor del port) -> splash_logo.inc
# Se guarda como JPEG de 240x240 sobre negro (unos 10 KB dentro del programa); el host lo abre con stb_image al
# arrancar, lo deja en la pantalla de arriba mientras abajo se eligen los cartuchos y suelta la memoria.
import io, os, sys
from PIL import Image
here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, '..', '..', 'platform', '3ds', 'host', 'assets', 'pokethrasher.png')
dst = os.path.join(here, '..', '..', 'platform', '3ds', 'host', 'splash_logo.inc')
im = Image.open(src).convert('RGBA')
bg = Image.new('RGBA', im.size, (0, 0, 0, 255))
bg.alpha_composite(im)
small = bg.convert('RGB').resize((240, 240), Image.LANCZOS)
buf = io.BytesIO()
small.save(buf, 'JPEG', quality=90, subsampling=0)
data = buf.getvalue()
with open(dst, 'w') as f:
    f.write('// generado por tools/port/gen_splash.py: logo de arranque, JPEG de 240x240 sobre negro\n')
    f.write('static const unsigned char kSplashJpg[%d] = {\n' % len(data))
    for i in range(0, len(data), 24):
        f.write('    ' + ','.join(str(b) for b in data[i:i + 24]) + ',\n')
    f.write('};\n')
print(len(data), 'bytes')

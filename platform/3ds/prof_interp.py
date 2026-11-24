#!/usr/bin/env python3
"""Cronometros (tiempo exclusivo) en funciones del interprete MK64 para medir en la consola.
Uso: prof_interp.py interprete.cpp (modifica en el lugar)"""
import re, sys
p = sys.argv[1]
s = open(p).read()
funcs = ["Flush", "LookupOrCreateShaderProgram", "TextureCacheLookup", "ImportTexture", "GfxSpMatrix",
         "GfxSpVertex", "GfxSpTri1", "GfxDpTextureRectangle", "GfxDpFillRectangle", "TextureCacheDelete"]
hdr = r'''
#include <3ds/svc.h>
extern "C" { uint64_t gPsProf[24]; uint32_t gPsProfCount[24]; }
namespace {
struct PsProf3DS {
    static inline uint64_t sChild[48];
    static inline int sDepth = 0;
    int id; uint64_t t0;
    explicit PsProf3DS(int i) : id(i), t0(svcGetSystemTick()) { if (sDepth < 46) sChild[++sDepth] = 0; }
    ~PsProf3DS() {
        uint64_t dt = svcGetSystemTick() - t0;
        gPsProf[id] += dt - sChild[sDepth]; gPsProfCount[id]++;
        if (sDepth > 0) { sDepth--; sChild[sDepth] += dt; }
    }
};
}
'''
# despues de los #include iniciales
idx = s.index("\nnamespace")
s = s[:idx] + hdr + s[idx:]
n = 0
for i, f in enumerate(funcs):
    pat = re.compile(r"(\n[A-Za-z_][A-Za-z0-9_:<>\*& ]*\bInterpreter::" + f + r"\([^;{]*\)\s*(?:const\s*)?\{\n)")
    s, k = pat.subn(lambda m, i=i: m.group(1) + "    PsProf3DS _psprof(%d);\n" % i, s, count=1)
    n += k
# Razones de Flush: cada llamada cuenta en gPsFlushWhy[k]; el mapa k -> linea se guarda junto al fuente
lines = s.split("\n")
k = 0
mapping = []
for i, l in enumerate(lines):
    if "Flush();" in l and "Interpreter::Flush()" not in l and k < 32:
        tok = "gfx->Flush();" if "gfx->Flush();" in l else "Flush();"
        lines[i] = l.replace(tok, "{ gPsFlushWhy[%d]++; %s }" % (k, tok), 1)
        mapping.append("%d %d %s" % (k, i + 1, l.strip()[:80]))
        k += 1
s = "\n".join(lines)
s = s.replace('extern "C" { uint64_t gPsProf[24]; uint32_t gPsProfCount[24]; }',
              'extern "C" { uint64_t gPsProf[24]; uint32_t gPsProfCount[24]; uint32_t gPsFlushWhy[32]; }', 1)
open(p + ".flushmap.txt", "w").write("\n".join(mapping) + "\n")
a = "    const auto material3DS = Mk64ResolveTriangleMaterial3DS(this, is_rect);"
if a in s:
    s = s.replace(a, "    const auto material3DS = [&] { PsProf3DS _psm(16); return Mk64ResolveTriangleMaterial3DS(this, is_rect); }();", 1)
b = "    const uint8_t vertexMode3DS = (nativeDraw3DS.enabled ? 1 : 0) | (nativeLighting3DS ? 2 : 0);\n"
if b in s:
    s = s.replace(b, b + "    PsProf3DS _psv(17);\n", 1)
for a, i in (("bool gfx_ps_skin_handler(F3DGfx** cmd0) {\n", 18), ("    void append(const PsSkinVtx3DS* v, uint32_t nVtx, const uint16_t* ix, uint32_t nIdx) {\n", 19),
             ("    bool begin() {\n", 20)):
    if a in s:
        s = s.replace(a, a + "    PsProf3DS _psk%d(%d);\n" % (i, i), 1)
# Tiempo por opcode (inclusivo: lo que cuesta el comando con todo lo que dispare, p. ej. un Flush).
# El host imprime los 10 mas caros en [op] cada 60 cuadros.
_g = "    sPsCurOp3DS = (uint8_t)opcode;\n"
if _g in s:
    s = s.replace(_g, _g + "    PsOpTimer3DS _psop((uint8_t)opcode);\n", 1)
    _i = s.index("static void gfx_step() {")
    s = s[:_i] + """extern "C" { uint64_t gPsOpTicks3DS[256]; uint32_t gPsOpCount3DS[256]; }
struct PsOpTimer3DS {
    uint8_t op; uint64_t t0;
    explicit PsOpTimer3DS(uint8_t o) : op(o), t0(svcGetSystemTick()) {}
    ~PsOpTimer3DS() { gPsOpTicks3DS[op] += svcGetSystemTick() - t0; gPsOpCount3DS[op]++; }
};
""" + s[_i:]
else:
    print("prof_interp: sin tiempo por opcode (no encontre gfx_step)")
open(p, "w").write(s)
print("prof_interp: %d funciones medidas" % n)

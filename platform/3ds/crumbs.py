#!/usr/bin/env python3
"""Caja negra (PS_BLACKBOX=1): migas. Al entrar a cada funcion del archivo se anota su numero en gPsCrumb; la caja
muestra las ultimas con su nombre, para saber en que funcion se quedo el hilo principal si el cuadro se atora.
Uso: crumbs.py <fuente> <salida> <base> <nombres.inc> [carpeta de .inc ...]"""
import os, re, sys
src, dst, base, names = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
incdirs = sys.argv[5:]
tag = os.path.basename(src).split('.')[0][:10]
s = open(src).read()
# los .inc propios van dentro, para que tambien lleven migas
def inline(m):
    for d in incdirs:
        p = os.path.join(d, m.group(1))
        if os.path.exists(p): return "// --- " + m.group(1) + " ---\n" + open(p).read() + "\n"
    return m.group(0)
s = re.sub(r'#include "(ps_[a-z_]+\.inc)"[^\n]*\n', inline, s)
SKIP = re.compile(r'^\s*(if|for|while|switch|else|do|catch|return|case|default|namespace|struct|class|enum|union|template|typedef|using|try|extern|#|//|\}|\{)\b|^\s*[\}\{#/]')
out, ids = [], []
lines = s.split("\n")
for i, l in enumerate(lines):
    out.append(l)
    t = l.rstrip()
    ind = len(l) - len(l.lstrip())
    if ind > 4 or not t.endswith("{") or "(" not in t or ";" in t: continue
    if SKIP.search(l) or "constexpr" in t or "operator" in t or "[" in t.split("(")[0] or "=" in t.split("(")[0]: continue
    if re.search(r'\)\s*(const\s*)?(noexcept\s*)?(override\s*)?(->[^{]*)?\{$', t) is None and ") :" not in t and "):" not in t: continue
    name = re.findall(r'([A-Za-z_~][A-Za-z0-9_:~]*)\s*\(', t.split("(")[0] + "(")
    if not name: continue
    n = base + len(ids)
    ids.append(name[-1].split("::")[-1][:22])
    out.append(" " * (ind + 4) + "PS_CRUMB(%d);" % n)
hdr = ('extern "C" { extern volatile unsigned gPsCrumb[8]; extern volatile unsigned gPsCrumbN; }\n'
       '#define PS_CRUMB(i) do { gPsCrumb[gPsCrumbN & 7] = (i); gPsCrumbN = gPsCrumbN + 1; } while (0)\n')
open(dst, "w").write(hdr + "\n".join(out))
open(names, "w").write("".join('    { %d, "%s:%s" },\n' % (base + k, tag[:6], nm) for k, nm in enumerate(ids)))
print("migas: %s %d funciones" % (src, len(ids)))

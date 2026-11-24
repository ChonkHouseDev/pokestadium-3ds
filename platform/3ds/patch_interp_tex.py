#!/usr/bin/env python3
"""Menos envios al GPU: el interprete vaciaba su lote (Flush) cada vez que el juego volvia a cargar una textura,
aunque fuera la misma que ya estaba puesta. Stadium recarga la textura en cada hueso de los Pokemon, asi que
cada hueso era un envio aparte (~100 us cada uno en el Old 3DS). Ahora solo se vacia cuando la textura que
queda puesta en la unidad cambia de verdad.

Uso: patch_interp_tex.py interprete.cpp   (modifica en el lugar)
"""
import sys

p = sys.argv[1]
s = open(p).read()


def rep(old, new, count=1):
    global s
    if s.count(old) < count:
        sys.exit("patch_interp_tex: no encontre: " + old[:80])
    s = s.replace(old, new, count)


# textura puesta en cada unidad, segun este interprete (se invalida en cada cambio que no pase por aqui)
rep("bool Interpreter::TextureCacheLookup(int i, const TextureCacheKey& key) {",
    "extern \"C\" { uint32_t gPsTexDbg3DS[8]; }\n"
    "static uint32_t sPsBoundTex3DS[8] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u };\n"
    "static void PsForgetBoundTextures3DS() { for (auto& b : sPsBoundTex3DS) b = ~0u; }\n"
    "bool Interpreter::TextureCacheLookup(int i, const TextureCacheKey& key) {")

# acierto: solo vaciar y seleccionar si la unidad tenia otra textura
rep("""        Mk64MarkTextureUsed3DS(it->second.texture_id);
        mRapi->SelectTexture(i, it->second.texture_id);
        *n = &*it;""", """        Mk64MarkTextureUsed3DS(it->second.texture_id);
        if (sPsBoundTex3DS[i & 7] != it->second.texture_id || *n != &*it) {
            {
                const bool small = key.size_bytes <= 2048, clampB = (it->second.cms & 2) && (it->second.cmt & 2);
                gPsTexDbg3DS[0]++; gPsTexDbg3DS[1] += small; gPsTexDbg3DS[2] += clampB; gPsTexDbg3DS[3] += small && clampB;
                gPsTexDbg3DS[4] += small && !clampB; gPsTexDbg3DS[5] += i != 0;
            }
            Flush();
            mRapi->SelectTexture(i, it->second.texture_id);
            sPsBoundTex3DS[i & 7] = it->second.texture_id;
        }
        *n = &*it;""")

# fallos (textura nueva): vaciar antes de cambiarla
rep("""        Mk64MarkTextureUsed3DS(textureId3DS);
        mRapi->SelectTexture(i, textureId3DS);""", """        Mk64MarkTextureUsed3DS(textureId3DS);
        Flush();
        mRapi->SelectTexture(i, textureId3DS);
        sPsBoundTex3DS[i & 7] = textureId3DS;""")
rep("""    mRapi->SelectTexture(i, texture_id);
    mRapi->SetSamplerParameters(i, false, 0, 0);
    *n = node;""", """    Flush();
    mRapi->SelectTexture(i, texture_id);
    sPsBoundTex3DS[i & 7] = texture_id;
    mRapi->SetSamplerParameters(i, false, 0, 0);
    *n = node;""")

# framebuffers como textura: cambian la unidad por fuera
rep("            mRapi->SelectTextureFb(fbIt->second);",
    "            mRapi->SelectTextureFb(fbIt->second);\n            PsForgetBoundTextures3DS();")
rep("    gfx->mRapi->SelectTextureFb((uint32_t)cmd->words.w1);",
    "    gfx->mRapi->SelectTextureFb((uint32_t)cmd->words.w1);\n    PsForgetBoundTextures3DS();")

# el sitio que vaciaba siempre
rep("""                imported3DS = true;
                gfx->Flush();
                gfx->ImportTexture(i, tile, false);""", """                imported3DS = true;
                gfx->ImportTexture(i, tile, false);""")

# cada cuadro empieza sin suponer nada de las unidades
rep("void Interpreter::StartFrame() {", "void Interpreter::StartFrame() {\n    PsForgetBoundTextures3DS();")

open(p, "w").write(s)
print("patch_interp_tex: ok")

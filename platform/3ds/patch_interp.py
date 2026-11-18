#!/usr/bin/env python3
"""Parches de rendimiento al interprete MK64 para Stadium.

Stadium vuelve a cargar la textura en cada parte del modelo aunque sea la misma que ya estaba (misma
direccion, formato, tile y paleta). El interprete corta el lote (Flush) e importa de nuevo cada vez: era el
85% de los cortes de lote en batalla. Si la carga es identica a la ultima importada en ese slot dentro del
mismo cuadro, se marca como sin cambio y el lote sigue.

Uso: patch_interp.py interprete.cpp (en el lugar)"""
import sys
p = sys.argv[1]
s = open(p).read()


def rep(old, new, count=1):
    global s
    if old not in s:
        sys.exit("patch_interp: no encontre: " + old[:60])
    s = s.replace(old, new, count)


helpers = r'''
// --- Stadium: firma de la ultima textura importada por slot (se reinicia en cada cuadro) ---
namespace {
struct PsTexSig3DS {
    const uint8_t* addr; uint32_t origSize, sizeBytes, fullLine, line, flags;
    const uint8_t* pal[2];
    uint8_t tile[sizeof(((Fast::RDP*)nullptr)->texture_tile[0])];
    uint32_t gen; bool valid;
};
PsTexSig3DS sPsTexSig3DS[2];
uint32_t sPsTexSigGen3DS = 1;
void PsFillTexSig3DS(Fast::Interpreter* gfx, int tile, PsTexSig3DS& sig) {
    const auto& tt = gfx->mRdp->texture_tile[tile];
    const auto& lt = gfx->mRdp->loaded_texture[tt.tmem_index];
    std::memset(&sig, 0, sizeof(sig));
    sig.addr = lt.addr; sig.origSize = lt.orig_size_bytes; sig.sizeBytes = lt.size_bytes;
    sig.fullLine = lt.full_image_line_size_bytes; sig.line = lt.line_size_bytes; sig.flags = lt.tex_flags;
    sig.pal[0] = gfx->mRdp->palette_dram_addr[0]; sig.pal[1] = gfx->mRdp->palette_dram_addr[1];
    std::memcpy(sig.tile, &tt, sizeof(sig.tile));
}
bool PsSameTexture3DS(Fast::Interpreter* gfx, int i, int tile) {
    const auto& tt = gfx->mRdp->texture_tile[tile];
    const auto& lt = gfx->mRdp->loaded_texture[tt.tmem_index];
    if (lt.masked || lt.blended || lt.addr == nullptr) return false;
    const PsTexSig3DS& last = sPsTexSig3DS[i];
    if (!last.valid || last.gen != sPsTexSigGen3DS) return false;
    PsTexSig3DS now;
    PsFillTexSig3DS(gfx, tile, now);
    return std::memcmp(&now, &last, offsetof(PsTexSig3DS, gen)) == 0;
}
void PsRememberTexture3DS(Fast::Interpreter* gfx, int i, int tile) {
    PsFillTexSig3DS(gfx, tile, sPsTexSig3DS[i]);
    sPsTexSig3DS[i].gen = sPsTexSigGen3DS;
    sPsTexSig3DS[i].valid = true;
}
}
'''
# antes de la primera funcion que usa el bloque de importacion
anchor = "            if (gfx->mRdp->textures_changed[i]) {\n                imported3DS = true;\n                gfx->Flush();\n                gfx->ImportTexture(i, tile, false);\n"
i = s.index(anchor)
fn_start = s.rfind("\nstatic ", 0, i)
if fn_start < 0:
    sys.exit("patch_interp: no encontre la funcion de importacion")
s = s[:fn_start] + "\n" + helpers + s[fn_start:]
rep(anchor, "            if (gfx->mRdp->textures_changed[i] && PsSameTexture3DS(gfx, i, tile)) {\n"
            "                gfx->mRdp->textures_changed[i] = false;\n            }\n" + anchor +
            "                PsRememberTexture3DS(gfx, i, tile);\n")
rep("void Interpreter::StartFrame() {\n", "void Interpreter::StartFrame() {\n    ++sPsTexSigGen3DS;\n")
# los borrados del cache de texturas invalidan las firmas
rep("void Interpreter::TextureCacheDelete(const uint8_t* origAddr) {\n",
    "void Interpreter::TextureCacheDelete(const uint8_t* origAddr) {\n    ++sPsTexSigGen3DS;\n")
if "#include <cstddef>" not in s:
    s = "#include <cstddef>\n" + s
open(p, "w").write(s)
print("patch_interp: texturas repetidas sin cortar el lote")

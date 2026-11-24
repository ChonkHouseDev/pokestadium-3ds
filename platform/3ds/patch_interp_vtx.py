#!/usr/bin/env python3
# gSPVertex / gSPDisplayList con una direccion fuera de la memoria del juego: se salta el comando (y se anota, con la
# pila de listas de dibujo) en vez de leer memoria sin mapear, que en la consola es un abort de datos.
import sys
p = sys.argv[1]
s = open(p).read()
helper = '''static void PsBadCtx3DS(const void* ptr, const F3DGfx* cmd) {
    PsBadPtr3DS(ptr, cmd, (uint32_t)cmd->words.w0);
    uint32_t stk[6] = {}; uint32_t res = 0; const uint32_t d = (uint32_t)sMk64ExecStackDepth3DS;
    for (uint32_t i = 0; i < 6 && i < d; i++) { stk[i] = (uint32_t)(uintptr_t)sMk64ExecStack3DS[d - 1 - i]; if (sMk64ResourceFrame3DS[d - 1 - i]) res |= 1u << i; }
    const size_t np = g_exec_stack.gfx_path.size();
    for (uint32_t i = 0; i < 5 && i < np; i++) stk[1 + i] = (uint32_t)(uintptr_t)g_exec_stack.gfx_path[np - 1 - i];
    PsBadStk3DS(stk, d, res);
}
'''
a = "// Almost all versions of the microcode have their own version of this opcode\nbool gfx_vtx_handler_f3dex2("
assert s.count(a) == 1
s = s.replace(a, helper + a)
calls = [
    ("C0(12, 8)", "C0(1, 7) - C0(12, 8)"),
    ("C0(10, 6)", "C0(17, 7)"),
    ("(C0(0, 16)) / sizeof(F3DVtx)", "C0(16, 4)"),
]
for n, dst in calls:
    a = "gfx->GfxSpVertex(%s, %s, (const F3DVtx*)gfx->SegAddr(cmd->words.w1));" % (n, dst)
    assert s.count(a) == 1, a
    s = s.replace(a, "{ const F3DVtx* psV = (const F3DVtx*)gfx->SegAddr(cmd->words.w1); const uint32_t psN = %s;\n"
                     "      if (!PsPtrOk3DS(psV, psN * (uint32_t)sizeof(F3DVtx))) { PsBadCtx3DS(psV, cmd); return false; }\n"
                     "      gfx->GfxSpVertex(psN, %s, psV); }" % (n, dst))
a = "    F3DGfx* subGFX = (F3DGfx*)gfx->SegAddr(cmd->words.w1);\n    if (C0(16, 1) == 0) {"
assert s.count(a) == 1
s = s.replace(a, "    F3DGfx* subGFX = (F3DGfx*)gfx->SegAddr(cmd->words.w1);\n"
                 "    if (subGFX != nullptr && !PsPtrOk3DS(subGFX, 8)) { PsBadCtx3DS(subGFX, cmd); subGFX = nullptr; }\n    if (C0(16, 1) == 0) {")
a = "    F3DGfx* subGFX = (F3DGfx*)gfx->SegAddr(segAddr);\n"
assert s.count(a) == 1
s = s.replace(a, a + "    if (subGFX != nullptr && !PsPtrOk3DS(subGFX, 8)) { PsBadCtx3DS(subGFX, cmd); subGFX = nullptr; }\n")
a = "    gfx->GfxDpTextureRectangle(ulx, uly, lrx, lry, tile, uls, ult, dsdx, dtdy, opcode == RDP_G_TEXRECTFLIP);\n    return false;\n}\n\nbool gfx_tex_rect_wide_handler_custom("
assert s.count(a) == 1
s = s.replace(a, "    if (gPsRectLog3DS > 0) { gPsRectLog3DS--; PsLog3DS(\"[rect] %d %d %d %d tile %d s %u t %u cmd %08X\\n\", ulx >> 2, uly >> 2, lrx >> 2, lry >> 2, tile, uls, ult, (unsigned)(uintptr_t)(*cmd0 - 2)); }\n" + a)
a = "    ::mk64_3ds::PerformanceTimer stageTimer3DS(::mk64_3ds::PerformanceCurrent().timg_us);\n    Interpreter* gfx = mInstance;\n    F3DGfx* cmd = *cmd0;\n    uintptr_t i = (uintptr_t)gfx->SegAddr(cmd->words.w1);\n"
assert s.count(a) == 1
s = s.replace(a, a + "    if (gPsRectLog3DS > 0) PsLog3DS(\"[timg] %08X fmt %u siz %u w %u\\n\", (unsigned)i, (unsigned)C0(21, 3), (unsigned)C0(19, 2), (unsigned)(C0(0, 12) + 1));\n")
a = "    gfx->GfxDpSetColorImage(C0(21, 3), C0(19, 2), C0(0, 11), gfx->SegAddr(cmd->words.w1));"
assert s.count(a) == 1
s = s.replace(a, "    if (gPsRectLog3DS > 0) PsLog3DS(\"[cimg] %08X w %u\\n\", (unsigned)(uintptr_t)gfx->SegAddr(cmd->words.w1), (unsigned)(C0(0, 11) + 1));\n" + a)
i = s.index("#include")
s = s[:i] + ('#include <cstdint>\nextern "C" { int PsPtrOk3DS(const void* p, uint32_t len); void PsBadPtr3DS(const void* p, const void* cmd, uint32_t w0);'
             ' void PsBadStk3DS(const uint32_t* stk, uint32_t depth, uint32_t res); void PsLog3DS(const char*, ...); int gPsRectLog3DS; }\n') + s[i:]
open(p, "w").write(s)

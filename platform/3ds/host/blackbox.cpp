// Caja negra: ver blackbox.h. Solo se compila con PS_BLACKBOX=1 (el enlazador redirige las llamadas a GX_*).
#include "blackbox.h"

#include <3ds.h>
#include <cmath>
#include <cstring>

BoxState gBox;
volatile uint32_t gBoxFrame;

extern "C" {
Result __real_GX_ProcessCommandList(u32* buf0a, u32 buf0s, u8 flags);
Result __real_GX_DisplayTransfer(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 flags);
Result __real_GX_TextureCopy(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 size, u32 flags);
Result __real_GX_MemoryFill(u32* buf0a, u32 buf0v, u32* buf0e, u16 control0, u32* buf1a, u32 buf1v, u32* buf1e, u16 control1);
}

namespace {
// Lo que el PICA200 puede tocar: VRAM y FCRAM (128 MB en el Old 3DS, 256 en el New)
bool physOk(u32 pa, u32 len) {
    if (len == 0) len = 1;
    const u64 end = (u64)pa + len;
    return (pa >= 0x18000000u && end <= 0x18600000u) || (pa >= 0x20000000u && end <= 0x30000000u);
}
bool virtOk(const void* p, u32 len) {
    if (!p) return false;
    if (len == 0) len = 1;
    const u32 a = osConvertVirtToPhys(p), b = osConvertVirtToPhys(static_cast<const u8*>(p) + len - 1);
    return a && b && b - a == len - 1 && physOk(a, len);
}
BoxEvent& push(uint8_t kind) {
    const u32 s = gBox.seq + 1;
    BoxEvent& e = gBox.ev[s % kBoxEvents];
    std::memset(&e, 0, sizeof e);
    e.seq = s;
    e.frame = gBoxFrame;
    e.tick = (u32)(svcGetSystemTick() >> 18);
    e.kind = kind;
    return e;
}
void commit(BoxEvent& e) {
    __sync_synchronize();
    gBox.seq = e.seq;
    if (e.bad) { gBox.lastBad = e; }
}
void flag(BoxEvent& e, u32 reg, u32 val) {
    if (!e.bad) { e.badReg = reg; e.badVal = val; }
    e.bad = 1;
    gBox.bad++;
}
u32 bppOf(u32 fmt) { return fmt == 0 ? 4 : fmt == 1 ? 3 : 2; } // GX_TRANSFER_FMT_*

// Recorre la lista de comandos como la leera el PICA y valida cada direccion de los dibujos
void scanList(const u32* w, u32 words, BoxEvent& e) {
    u32 texCfg = 0, texAddr[3] = {}, colorLoc = 0, depthLoc = 0, attrBase = 0, attrOff[12] = {}, attrCfg2[12] = {};
    u32 idxCfg = 0, numVerts = 0, uniCfg = 0;
    u32 uni[4];
    int uniN = 0;
    u32 draws = 0;
    for (u32 i = 0; i + 1 < words;) {
        const u32 header = w[i + 1];
        const u32 id0 = header & 0xFFFF, mask = (header >> 16) & 0xF, extra = (header >> 20) & 0x7FF;
        const bool consec = header >> 31;
        const u32 n = 1 + extra;
        if (i + 2 + extra > words) { flag(e, 0xFFFE, i); break; } // la lista se sale de su tamano
        for (u32 k = 0; k < n; k++) {
            const u32 v = k == 0 ? w[i] : w[i + 1 + k];
            const u32 id = consec ? id0 + k : id0;
            if (!mask) continue;
            switch (id) {
            case 0x080: texCfg = v; break;
            case 0x085: texAddr[0] = v; break;
            case 0x095: texAddr[1] = v; break;
            case 0x09D: texAddr[2] = v; break;
            case 0x11C: depthLoc = v; break;
            case 0x11D: colorLoc = v; break;
            case 0x200: attrBase = v; break;
            case 0x227: idxCfg = v; break;
            case 0x228: numVerts = v; break;
            case 0x2C0: uniCfg = v; uniN = 0; break;
            case 0x22E:
            case 0x22F: {
                draws++;
                if (!colorLoc || !physOk(colorLoc << 3, 4)) flag(e, 0x11D, colorLoc);
                if (depthLoc && !physOk(depthLoc << 3, 4)) flag(e, 0x11C, depthLoc);
                for (int t = 0; t < 3; t++)
                    if ((texCfg >> t) & 1)
                        if (!physOk(texAddr[t] << 3, 8)) flag(e, t == 0 ? 0x085 : t == 1 ? 0x095 : 0x09D, texAddr[t]);
                const u32 base = attrBase << 3;
                for (int b = 0; b < 12; b++) {
                    if ((attrCfg2[b] >> 28) == 0) continue; // sin componentes: buffer sin usar
                    const u32 stride = (attrCfg2[b] >> 16) & 0xFF;
                    const u32 len = id == 0x22E ? stride * (numVerts ? numVerts : 1) : stride;
                    if (!physOk(base + attrOff[b], len)) flag(e, 0x203 + 3 * b, base + attrOff[b]);
                }
                if (id == 0x22F) {
                    const u32 len = numVerts * ((idxCfg >> 31) ? 2 : 1);
                    if (!physOk(base + (idxCfg & 0x0FFFFFFF), len)) flag(e, 0x227, base + (idxCfg & 0x0FFFFFFF));
                }
                break;
            }
            default:
                if (id >= 0x203 && id < 0x203 + 36) {
                    const u32 b = (id - 0x203) / 3, r = (id - 0x203) % 3;
                    if (r == 0) attrOff[b] = v;
                    else if (r == 2) attrCfg2[b] = v;
                } else if (id >= 0x2C1 && id <= 0x2C8) {
                    // uniformes del programa de vertices: NaN (los infinitos son legitimos: el backend los usa como "sin recorte")
                    uni[uniN++] = v;
                    if ((uniCfg >> 31) == 0 && uniN == 3) {
                        const u32 f[4] = { uni[0] >> 8, ((uni[0] & 0xFF) << 16) | (uni[1] >> 16), ((uni[1] & 0xFFFF) << 8) | (uni[2] >> 24),
                                           uni[2] & 0xFFFFFF };
                        for (u32 x : f)
                            if (((x >> 16) & 0x7F) == 0x7F && (x & 0xFFFF)) { gBox.badNan++; gBox.nanReg = uniCfg & 0x7F; gBox.nanVal = x; gBox.nanMode = 0; }
                        uniCfg = (uniCfg & ~0x7Fu) | ((uniCfg + 1) & 0x7F); // el registro avanza solo
                        uniN = 0;
                    } else if ((uniCfg >> 31) != 0 && uniN == 4) {
                        for (int q = 0; q < 4; q++) {
                            float fv;
                            std::memcpy(&fv, &uni[q], 4);
                            if (std::isnan(fv)) { gBox.badNan++; gBox.nanReg = uniCfg & 0x7F; gBox.nanVal = uni[q]; gBox.nanMode = 1; }
                        }
                        uniN = 0;
                        uniCfg = (uniCfg & ~0x7Fu) | ((uniCfg + 1) & 0x7F);
                    }
                }
                break;
            }
        }
        i += 2 + extra + (extra & 1); // los parametros extra se rellenan a pares
    }
    e.c = draws;
    gBox.draws += draws;
}
} // namespace

extern "C" Result __wrap_GX_ProcessCommandList(u32* buf0a, u32 buf0s, u8 flags) {
    BoxEvent& e = push(BOX_LIST);
    e.a = (u32)(uintptr_t)buf0a;
    e.b = buf0s / 4;
    if (!virtOk(buf0a, buf0s)) flag(e, 0xFFFF, (u32)(uintptr_t)buf0a);
    else scanList(buf0a, buf0s / 4, e);
    gBox.lists++;
    gBox.lastListTick = e.tick;
    if (e.b > gBox.maxListWords) gBox.maxListWords = e.b;
    commit(e);
    return __real_GX_ProcessCommandList(buf0a, buf0s, flags);
}

extern "C" Result __wrap_GX_DisplayTransfer(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 flags) {
    BoxEvent& e = push(BOX_XFER);
    e.a = (u32)(uintptr_t)inadr; e.b = (u32)(uintptr_t)outadr; e.c = indim; e.d = flags;
    const u32 inBytes = (indim & 0xFFFF) * (indim >> 16) * bppOf((flags >> 8) & 7);
    const u32 outBytes = (outdim & 0xFFFF) * (outdim >> 16) * bppOf((flags >> 12) & 7);
    if (!virtOk(inadr, inBytes)) flag(e, 0xFFFF, e.a);
    if (!virtOk(outadr, outBytes)) flag(e, 0xFFFF, e.b);
    if (!(indim & 0xFFFF) || !(indim >> 16) || !(outdim & 0xFFFF) || !(outdim >> 16)) flag(e, 0xFFFD, indim);
    gBox.xfers++;
    commit(e);
    return __real_GX_DisplayTransfer(inadr, indim, outadr, outdim, flags);
}

extern "C" Result __wrap_GX_TextureCopy(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 size, u32 flags) {
    BoxEvent& e = push(BOX_COPY);
    e.a = (u32)(uintptr_t)inadr; e.b = (u32)(uintptr_t)outadr; e.c = size; e.d = indim;
    auto span = [&](u32 dim) -> u32 {
        const u32 width = (dim & 0xFFFF) * 16, gap = (dim >> 16) * 16;
        if (!width) return size;
        return (size + width - 1) / width * (width + gap);
    };
    if (!size) flag(e, 0xFFFD, 0);
    if (!virtOk(inadr, span(indim))) flag(e, 0xFFFF, e.a);
    if (!virtOk(outadr, span(outdim))) flag(e, 0xFFFF, e.b);
    gBox.copies++;
    commit(e);
    return __real_GX_TextureCopy(inadr, indim, outadr, outdim, size, flags);
}

extern "C" Result __wrap_GX_MemoryFill(u32* buf0a, u32 buf0v, u32* buf0e, u16 control0, u32* buf1a, u32 buf1v, u32* buf1e, u16 control1) {
    BoxEvent& e = push(BOX_FILL);
    e.a = (u32)(uintptr_t)buf0a; e.b = (u32)(uintptr_t)buf0e; e.c = (u32)(uintptr_t)buf1a; e.d = (u32)(uintptr_t)buf1e;
    if (buf0a && (buf0e <= buf0a || !virtOk(buf0a, (u32)((u8*)buf0e - (u8*)buf0a)))) flag(e, 0xFFFF, e.a);
    if (buf1a && (buf1e <= buf1a || !virtOk(buf1a, (u32)((u8*)buf1e - (u8*)buf1a)))) flag(e, 0xFFFF, e.c);
    gBox.fills++;
    commit(e);
    return __real_GX_MemoryFill(buf0a, buf0v, buf0e, control0, buf1a, buf1v, buf1e, control1);
}

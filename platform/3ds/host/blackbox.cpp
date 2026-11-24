// Caja negra: ver blackbox.h. Solo se compila con PS_BLACKBOX=1 (el enlazador redirige las llamadas a GX_*).
#include "blackbox.h"

#include <3ds.h>
#include <cmath>
#include <cstring>

BoxState gBox;
extern "C" void PsLog3DS(const char* fmt, ...);
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
    if (e.bad || e.badReg) { gBox.lastBad = e; }
}
void flag(BoxEvent& e, u32 reg, u32 val) {
    if (!e.bad) { e.badReg = reg; e.badVal = val; }
    e.bad = 1;
    gBox.bad++;
}
u32 bppOf(u32 fmt) { return fmt == 0 ? 4 : fmt == 1 ? 3 : 2; } // GX_TRANSFER_FMT_*

// Recorre la lista de comandos como la leera el PICA y valida cada direccion de los dibujos
// Cosas que el emulador aguanta y el PICA200 de la consola quiza no. No son direcciones malas: van aparte, como
// bits en e.d (se ven en el renglon de la lista como "s<hex>") y la primera queda en badReg/badVal:
//   bit 0 (E001) dibujo con 0 vertices            bit 1 (E002) viewport con escala 0 / infinita / NaN
//   bit 2 (E003) recorte invertido o fuera        bit 3 (E004) vertice con NaN o infinito (val = dibujo << 16 | vertice)
//   bit 4 (E005) vertice ya proyectado con w <= 0 bit 5 (E006) textura con lado 0 o mayor que 1024
//   bit 6 (E007) profundidad NaN / infinita       bit 7 (E008) imagen de destino con lado 0
//   bit 8 (E00A) vertice ya proyectado con |x| o |y| > 4096 w
void sus(BoxEvent& e, u32 bit, u32 val) {
    if (!(e.d & (1u << bit))) gBox.sus++;
    e.d |= 1u << bit;
    if (!e.bad && !e.badReg) { e.badReg = 0xE001 + (bit == 8 ? 9 : bit); e.badVal = val; }
    gBox.lastSus = e.d; gBox.lastSusReg = 0xE001 + (bit == 8 ? 9 : bit); gBox.lastSusVal = val; gBox.lastSusFrame = e.frame;
}
bool f24bad(u32 v) { return ((v >> 16) & 0x7F) == 0x7F; }
bool f32bad(u32 v) { return (v & 0x7F800000u) == 0x7F800000u; }
// ---- captura: la ultima lista que se mando, el estado del GPU al empezarla y los datos de cada dibujo ----
u32 sShadow[0x300], sShadowAtList[0x300];
struct CapDraw { u32 id, base, fmtLo, fmtHi, off[2], cfg1[2], cfg2[2], idxCfg, num, first, boolU; };
constexpr u32 kCapDraws = 512;
CapDraw sCapDraw[kCapDraws];
u32 sCapDrawN;
const u32* sCapList; u32 sCapWords;
void scanList(const u32* w, u32 words, BoxEvent& e) {
    u32 texCfg = 0, texAddr[3] = {}, colorLoc = 0, depthLoc = 0, attrBase = 0, attrOff[12] = {}, attrCfg2[12] = {};
    u32 idxCfg = 0, numVerts = 0, uniCfg = 0;
    u32 texDim[3] = { 0x00010001, 0x00010001, 0x00010001 }, scMode = 0, scPos = 0, scDim = 0, fbDim = 0, vtxFirst = 0, attrFmt = 0;
    static u32 boolU; // citro3d solo lo manda cuando cambia: vale de una lista a la siguiente
    const u32 delta = (u32)(uintptr_t)w - osConvertVirtToPhys(w); // memoria lineal: virtual = fisica + delta
    // vertices ya proyectados (camino del CPU): cuantos con w <= 0, cuantos con NaN/infinito y el mayor |x o y| / w
    // (como potencia de 2: 12 = 4096 veces fuera de la pantalla)
    int maxR = 0; u32 nW0 = 0, nNan = 0;
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
            if (id < 0x300) sShadow[id] = v; // estado del GPU segun lo que se le ha mandado (para la captura)
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
            case 0x22A: vtxFirst = v; break;
            case 0x201: attrFmt = v; break;
            case 0x2B0: boolU = v & 0xFFFF; break;
            case 0x082: texDim[0] = v; break;
            case 0x092: texDim[1] = v; break;
            case 0x09A: texDim[2] = v; break;
            case 0x041: case 0x043: if (f24bad(v) || (v & 0xFFFFFF) == 0) sus(e, 1, v); break;
            case 0x042: case 0x044: if (((v >> 24) & 0x7F) == 0x7F || (v & 0x7FFFFFFF) == 0) sus(e, 1, v); break;
            case 0x04D: case 0x04E: if (f24bad(v)) sus(e, 6, v); break;
            case 0x065: scMode = v & 3; break;
            case 0x066: scPos = v; break;
            case 0x067: scDim = v; break;
            case 0x11E: fbDim = v; if ((v & 0x7FF) == 0 || ((v >> 12) & 0x3FF) == 0) sus(e, 7, v); break;
            case 0x2C0: uniCfg = v; uniN = 0; break;
            case 0x22E:
            case 0x22F: {
                draws++;
                if (sCapDrawN < kCapDraws) {
                    CapDraw& d = sCapDraw[sCapDrawN++];
                    d.id = id; d.base = sShadow[0x200]; d.fmtLo = sShadow[0x201]; d.fmtHi = sShadow[0x202];
                    for (int b = 0; b < 2; b++) { d.off[b] = sShadow[0x203 + 3 * b]; d.cfg1[b] = sShadow[0x204 + 3 * b]; d.cfg2[b] = sShadow[0x205 + 3 * b]; }
                    d.idxCfg = sShadow[0x227]; d.num = sShadow[0x228]; d.first = sShadow[0x22A]; d.boolU = sShadow[0x2B0];
                }
                if (numVerts == 0) sus(e, 0, draws);
                if (scMode) {
                    const u32 x0 = scPos & 0x3FF, y0 = (scPos >> 16) & 0x3FF, x1 = scDim & 0x3FF, y1 = (scDim >> 16) & 0x3FF;
                    const u32 fw = fbDim & 0x7FF, fh = ((fbDim >> 12) & 0x3FF) + 1;
                    if (x1 < x0 || y1 < y0 || (fw && x1 >= fw) || (fbDim && y1 >= fh)) sus(e, 2, (scPos & 0xFFFF) << 16 | (scDim & 0xFFFF));
                }
                for (int t = 0; t < 3; t++)
                    if ((texCfg >> t) & 1) {
                        const u32 tw = texDim[t] >> 16, th = texDim[t] & 0xFFFF;
                        if (tw == 0 || th == 0 || tw > 1024 || th > 1024) sus(e, 5, texDim[t]);
                    }
                {   // posiciones: primer atributo del primer buffer, 4 flotantes
                    const u32 stride = (attrCfg2[0] >> 16) & 0xFF;
                    const u32 vpa = (attrBase << 3) + attrOff[0];
                    const bool plain = (boolU & ((1u << 0) | (1u << 2) | (1u << 5) | (1u << 9))) == 0; // ya proyectado en el CPU
                    const u32 count = numVerts > 4096 ? 4096 : numVerts;
                    if (stride >= 16 && (attrCfg2[0] >> 28) && vpa >= 0x20000000u && (attrFmt & 0xF) == 0xF) { // posicion: 4 flotantes
                        const u16* ix = nullptr;
                        if (id == 0x22F && (idxCfg >> 31)) {
                            const u32 ipa = (attrBase << 3) + (idxCfg & 0x0FFFFFFF);
                            if (physOk(ipa, count * 2) && ipa >= 0x20000000u) ix = reinterpret_cast<const u16*>(static_cast<uintptr_t>(ipa + delta));
                        }
                        if (id == 0x22E || ix) {
                            for (u32 q = 0; q < count; q++) {
                                const u32 vi = ix ? ix[q] : vtxFirst + q;
                                const u32 pa = vpa + vi * stride;
                                if (!physOk(pa, 16)) break;
                                const u32* pv = reinterpret_cast<const u32*>(static_cast<uintptr_t>(pa + delta));
                                if (f32bad(pv[0]) || f32bad(pv[1]) || f32bad(pv[2]) || (plain && f32bad(pv[3]))) { sus(e, 3, draws << 16 | (q & 0xFFFF)); nNan++; continue; }
                                if (plain) {
                                    float x, y, ww;
                                    std::memcpy(&x, &pv[0], 4); std::memcpy(&y, &pv[1], 4); std::memcpy(&ww, &pv[3], 4);
                                    if (!(ww > 0.0f)) { nW0++; continue; }
                                    const float m = std::fabs(x) > std::fabs(y) ? std::fabs(x) : std::fabs(y);
                                    int ex = 0;
                                    if (m > ww) { std::frexp(m / ww, &ex); if (ex > maxR) maxR = ex; }
                                }
                            }
                        }
                    }
                }
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
                            if (((x >> 16) & 0x7F) == 0x7F) { gBox.badNan++; gBox.nanReg = uniCfg & 0x7F; gBox.nanVal = x; gBox.nanMode = 0; }
                        uniCfg = (uniCfg & ~0x7Fu) | ((uniCfg + 1) & 0x7F); // el registro avanza solo
                        uniN = 0;
                    } else if ((uniCfg >> 31) != 0 && uniN == 4) {
                        for (int q = 0; q < 4; q++) {
                            float fv;
                            std::memcpy(&fv, &uni[q], 4);
                            if (!std::isfinite(fv)) { gBox.badNan++; gBox.nanReg = uniCfg & 0x7F; gBox.nanVal = uni[q]; gBox.nanMode = 1; }
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
    e.a = (u32)(maxR < 0 ? 0 : maxR) | (nW0 > 0xFFF ? 0xFFF : nW0) << 8 | (nNan > 0xFFF ? 0xFFF : nNan) << 20; // el host lo muestra
    gBox.draws += draws;
}
} // namespace

static constexpr u32 kPrevMax = 0x8000; // palabras
static u32 sPrevList[kPrevMax], sPrevWords;
extern "C" Result __wrap_GX_ProcessCommandList(u32* buf0a, u32 buf0s, u8 flags) {
    BoxEvent& e = push(BOX_LIST);
    e.b = buf0s / 4;
    // la lista anterior se guarda completa (la de ahora sigue en su bufer): la captura las lleva a las dos
    if (sCapList && sCapWords && sCapWords <= kPrevMax) { std::memcpy(sPrevList, sCapList, sCapWords * 4); sPrevWords = sCapWords; }
    std::memcpy(sShadowAtList, sShadow, sizeof sShadow); sCapDrawN = 0; sCapList = buf0a; sCapWords = buf0s / 4;
    if (!virtOk(buf0a, buf0s)) { flag(e, 0xFFFF, (u32)(uintptr_t)buf0a); }
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

// La cola de comandos GX de citro3d: para ver cual comando es el que no termina cuando el cuadro se queda esperando
extern "C" {
void __real_GX_BindQueue(gxCmdQueue_s* queue);
gxCmdQueue_s* volatile gBoxQueue;
void __wrap_GX_BindQueue(gxCmdQueue_s* queue) { if (queue) gBoxQueue = queue; __real_GX_BindQueue(queue); }
}

// Arma la captura de la ultima lista en out (hasta cap bytes) y regresa su tamano. Formato (palabras de 32 bits, LE):
// "PSGH", 1, palabras de la lista, la lista, 0x300 registros al empezar, n dibujos, y por dibujo: sus 16 campos, y por
// cada flujo (indices, buffer 0, buffer 1): direccion fisica, primer elemento, bytes, y los bytes (rellenos a 4).
extern "C" u32 PsBoxCapture(u8* out, u32 cap) {
    u32 n = 0;
    auto put = [&](const void* p, u32 len) { if (n + len <= cap) { std::memcpy(out + n, p, len); n += len; return true; } return false; };
    auto put32 = [&](u32 v) { return put(&v, 4); };
    if (!sCapList) return 0;
    const u32 delta = (u32)(uintptr_t)sCapList - osConvertVirtToPhys(sCapList);
    put32(0x48475350); put32(2); put32(sCapWords); put(sCapList, sCapWords * 4); put(sShadowAtList, sizeof sShadowAtList); put32(sCapDrawN);
    for (u32 i = 0; i < sCapDrawN; i++) {
        const CapDraw& d = sCapDraw[i];
        put(&d, sizeof d);
        u32 lo = d.first, hi = d.first + d.num; // [lo, hi) vertices usados
        u32 ipa = 0, ibytes = 0;
        if (d.id == 0x22F) {
            ipa = (d.base << 3) + (d.idxCfg & 0x0FFFFFFF);
            const bool wide = (d.idxCfg >> 31) != 0;
            ibytes = d.num * (wide ? 2 : 1);
            if (ibytes > 0x20000 || ipa < 0x20000000u || !physOk(ipa, ibytes)) ibytes = 0;
            lo = 0xFFFFFFFF; hi = 0;
            const u8* ip = reinterpret_cast<const u8*>(static_cast<uintptr_t>(ipa + delta));
            for (u32 q = 0; ibytes && q < d.num; q++) { const u32 v = wide ? (ip[q * 2] | ip[q * 2 + 1] << 8) : ip[q]; if (v < lo) lo = v; if (v + 1 > hi) hi = v + 1; }
            if (!ibytes) { lo = 0; hi = 0; }
        }
        put32(ipa); put32(0); put32(ibytes);
        if (ibytes) { put(reinterpret_cast<const void*>(static_cast<uintptr_t>(ipa + delta)), ibytes); const u32 z = 0; put(&z, (4 - (ibytes & 3)) & 3); }
        for (int b = 0; b < 2; b++) {
            const u32 stride = (d.cfg2[b] >> 16) & 0xFF, comps = d.cfg2[b] >> 28;
            u32 pa = (d.base << 3) + d.off[b] + lo * stride, bytes = (hi > lo ? hi - lo : 0) * stride;
            if (!comps || !stride || bytes > 0x40000 || pa < 0x20000000u || !physOk(pa, bytes)) bytes = 0;
            put32(pa); put32(lo); put32(bytes);
            if (bytes) { put(reinterpret_cast<const void*>(static_cast<uintptr_t>(pa + delta)), bytes); const u32 z = 0; put(&z, (4 - (bytes & 3)) & 3); }
        }
    }
    // version 2: lo que paso alrededor de la lista, para cuando la lista misma esta bien
    // "EVTS", n, y los ultimos n eventos del GPU (listas, transferencias, copias, rellenos) del mas viejo al mas nuevo
    {
        const u32 last = gBox.seq;
        const u32 cnt = last < (u32)kBoxEvents ? last : (u32)kBoxEvents;
        put32(0x53545645); put32(cnt); put32(sizeof(BoxEvent));
        for (u32 k = 0; k < cnt; k++) put(&gBox.ev[(last - cnt + 1 + k) % kBoxEvents], sizeof(BoxEvent));
    }
    // "QUEU", la cola de citro3d: capacidad, cuantos hay, el primero sin mandar, cuantos termino el GPU, y sus entradas
    {
        gxCmdQueue_s* q = gBoxQueue;
        put32(0x55455551);
        if (q && q->entries) {
            put32(q->maxEntries); put32(q->numEntries); put32(q->curEntry); put32(q->lastEntry);
            const u32 ne = q->numEntries < 32 ? q->numEntries : 32;
            put32(ne);
            for (u32 k = 0; k < ne; k++) put(&q->entries[k], sizeof(gxCmdEntry_s));
        } else { put32(0); put32(0); put32(0); put32(0); put32(0); }
    }
    // "PREV", la lista anterior completa
    put32(0x56455250); put32(sPrevWords); put(sPrevList, sPrevWords * 4);
    return n;
}

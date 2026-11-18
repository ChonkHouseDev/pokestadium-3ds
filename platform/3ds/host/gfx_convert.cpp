// Conversion de display lists del juego (F3DEX2 en big-endian, memoria del bloque) a display lists
// nativas para el interprete Fast3D de libultraship (little-endian).
//
// Se recorre la display list resolviendo segmentos; cada comando se voltea a palabras nativas y los
// datos con tipo que apunta (vertices, matrices, viewport, sub-listas) se copian convertidos. Las
// texturas y luces son bytes: se pasan tal cual (el interprete las lee en orden del N64).
#include "gfx_convert.h"
#include "soft_rdp.h"
#include "model_cache.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

extern bool gGfxConvertTexel1Test;
extern int gGfxConvertOnlyGroup;
extern bool gGfxConvertGpuFb;
extern bool gGfxConvertSkin;
extern bool gGfxConvertMeshBlocks;
extern uint32_t gGfxConvertCcW0, gGfxConvertCcW1;
#ifdef __3DS__
extern "C" void host_print(const char* s);
#else
static void host_print(const char* s) { std::fputs(s, stderr); }
#endif
mc::ModelCache gModelCache;
uint32_t gGfxConvertDbgDrop = 0;
// Capa del HUD: el tramo de rectangulos de la lista principal se marca (G_PS_HUD) con una llave de su contenido;
// el interprete lo guarda en una textura y deja de dibujarlo mientras la llave no cambie (ps_hud_interp.inc)
bool gGfxConvertHud = true;
uint32_t gGfxConvertHudWhy = 0;
uint32_t gGfxScanDbg[5]; // rectangulos con textura por tipo de ciclo, cuadros que tapan toda la pantalla
bool gGfxConvertHudDbg = false;
int gGfxConvertLog = 0; // convlog.txt (repeticion): que se hizo con cada lista y cada malla
static void convLog(const char* what, uint32_t a, uint32_t b, uint32_t c) {
    if (gGfxConvertLog <= 0) return;
    gGfxConvertLog--;
    char t[96]; std::snprintf(t, sizeof t, "[conv] %s %08lx %lu %lu\n", what, (unsigned long)a, (unsigned long)b, (unsigned long)c); host_print(t);
}
uint32_t gGfxConvertHudStats[4]; // 0 marcados, 1 sin tramo, 2 tramo no apto, 3 tramos sin convertir (ya en su capa)
#ifdef __3DS__
extern "C" bool PsHudLayerReady3DS(int layer, uint32_t kA, uint32_t kB);
#else
static bool PsHudLayerReady3DS(int, uint32_t, uint32_t) { return false; }
#endif
// F2: los lotes de la cache se mandan como 'PSK2' (id del lote + apuntador a su cuerpo en la cache) en vez de
// copiarse a la arena; el interprete los dibuja desde buffers residentes (ver ps_resident_interp.inc)
bool gGfxConvertF2 = true;
extern "C" void* PsModelGpuGet3DS(uint32_t id, uint32_t key) { return gModelCache.gpuGet(id, key); }
extern "C" void* PsModelGpuAttach3DS(uint32_t id, uint32_t key, uint32_t bytes) { return gModelCache.gpuAttach(id, key, bytes); }
namespace {

constexpr uint8_t G_VTX = 0x01, G_MODIFYVTX = 0x02, G_CULLDL = 0x03, G_BRANCH_Z = 0x04, G_DL = 0xDE,
                  G_ENDDL = 0xDF, G_MTX = 0xDA, G_MOVEMEM = 0xDC, G_MOVEWORD = 0xDB, G_RDPHALF_1 = 0xE1,
                  G_SETTIMG = 0xFD, G_SETZIMG = 0xFE, G_SETCIMG = 0xFF;
constexpr uint8_t G_MW_SEGMENT = 0x06, G_MV_VIEWPORT = 8, G_MV_LIGHT = 10, G_MV_MATRIX = 14;

inline uint32_t be32(const void* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return __builtin_bswap32(v);
}
inline uint16_t be16(const void* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return __builtin_bswap16(v);
}

struct Converter {
    GfxConvertConfig cfg;
    uint32_t segments[16] = {};
    std::vector<uint8_t> arena;     // memoria de la conversion de este frame
    size_t used = 0;
    std::vector<const uint8_t*> textures; // direcciones de G_SETTIMG de este frame
    struct CachedDl { uint32_t out; uint16_t segs; uint8_t hflags; uint16_t hsets; uint64_t hash; }; // hflags: 1 con llave, 2 no apta // segs: segmentos que uso al convertirse (incluye sub-listas)
    std::unordered_map<uint32_t, CachedDl> dlCache;   // DL del juego -> DL nativa (por frame)
    uint16_t usedSegs = 0;
    // llave del tramo del HUD: se acumula mientras se convierte (ahi ya estan resueltas las direcciones)
    bool hOn = false, hBad = false, hudDone = false;
    uint32_t hWhy = 0, kA = 0, kB = 0;
    std::vector<uint64_t> hudCmdH, hudPrevH;
    std::vector<uint8_t> hudAge, hudCmdF;
    uint16_t hS = 0; // estado que fija el comando (y sus sub-listas), como la mascara de la marca
    std::vector<uint16_t> hudCmdS;
    uint8_t hF = 0; // lo que usa el comando (y sus sub-listas): 1 o 4 carga vertices, 8 dibuja triangulos
    inline void hudFold(uint32_t k) {
        if (k < hudCmdH.size()) { hudCmdH[k] = ((uint64_t)hB << 32) | hA; hudCmdF[k] = hF; hudCmdS[k] = hS; }
    }
    uint32_t hA = 0, hB = 0;
    const uint8_t* hTimg = nullptr;
    uint32_t hTimgSiz = 0, hTimgW = 0;
    inline void hMix(uint32_t v) {
        hA = (hA ^ v) * 0x9E3779B1u; hA ^= hA >> 15;
        hB = (hB + v) * 0x85EBCA77u; hB ^= hB >> 13;
    }
    void hBytes(const uint8_t* p, uint32_t n) {
        if (!p) { hBad = true, hWhy |= 1u << 1; return; }
        hMix(n);
        uint32_t i = 0;
        if (n > 96) {
            // texturas grandes: los primeros y ultimos 32 bytes completos y en medio una palabra de cada 16 bytes
            // (una textura que cambia, cambia en muchos lados; leerla toda en cada cuadro costaba mas que el resto)
            for (; i < 32; i += 4) { uint32_t v; std::memcpy(&v, p + i, 4); hMix(v); }
            const uint32_t end = (n - 32) & ~3u;
            for (; i < end; i += 16) { uint32_t v; std::memcpy(&v, p + i, 4); hMix(v); }
            i = end;
        }
        for (; i + 4 <= n; i += 4) { uint32_t v; std::memcpy(&v, p + i, 4); hMix(v); }
        uint32_t t = 0;
        for (; i < n; i++) t = (t << 8) | p[i];
        hMix(t);
    }
    // lo que el comando lee de la memoria del juego (vertices, matrices, texturas): cambia sin que cambie el comando
    void hCmd(uint8_t op, uint32_t w0, uint32_t w1) {
        // la direccion de vertices, matrices y sub-listas no entra: el juego las arma en memoria que alterna de un
        // cuadro a otro; entra su contenido
        hMix(w0);
        if (op != G_VTX && op != G_MTX && op != G_MOVEMEM && op != G_DL) hMix(w1);
        switch (op) {
            case G_VTX: hBytes(resolve(w1), ((w0 >> 12) & 0xFF) * 16); hF |= 1; break;
            case 0x05: case 0x06: case 0x07: hF |= 8; break;
            case 0xE4: case 0xE5: case 0xF6: hF |= 16; break;
            case 0xEF: hS |= 1; break;
            case 0xFC: hS |= 2; break;
            case 0xED: hS |= 4; break;
            case 0xFA: hS |= 8; break;
            case 0xFB: hS |= 16; break;
            case 0xF8: hS |= 32; break;
            case 0xF7: hS |= 64; break;
            case 0xF9: hS |= 128; break;
            case 0xFD: hS |= 1 << 10; break;
            case 0xF5: hS |= 1 << 11; break;
            case G_MTX: hBytes(resolve(w1), 64); break;
            case G_MOVEMEM: hBytes(resolve(w1), (((w0 >> 19) & 0x1F) + 1) * 8); break;
            case 0xF0: if (hTimg) hBytes(hTimg, std::min<uint32_t>((((w1 >> 14) & 0x3FF) + 1) * 2, 512)); else hBad = true, hWhy |= 1u << 2; break;
            case 0xF3: { // LOADBLOCK
                hS |= 1 << 12;
                if (!hTimg) { hBad = true, hWhy |= 1u << 3; break; }
                const uint32_t texels = ((w1 >> 12) & 0xFFF) + 1;
                hBytes(hTimg, std::min<uint32_t>(((texels << hTimgSiz) + 1) / 2, 4096));
                break;
            }
            case 0xF4: { // LOADTILE: renglones completos de ult a lrt
                hS |= 1 << 12;
                if (!hTimg) { hBad = true, hWhy |= 1u << 4; break; }
                const uint32_t ult = ((w0 & 0xFFF) >> 2), lrt = ((w1 & 0xFFF) >> 2);
                const uint32_t rowBytes = ((hTimgW << hTimgSiz) + 1) / 2;
                if (lrt < ult || rowBytes * (lrt - ult + 1) > 65536) { hBad = true, hWhy |= 1u << 5; break; }
                hBytes(hTimg + rowBytes * ult, rowBytes * (lrt - ult + 1));
                break;
            }
            case G_SETZIMG: case G_SETCIMG: case G_BRANCH_Z: case G_CULLDL: hBad = true, hWhy |= 1u << 6; break;
            default: break;
        }
    }
    const uint8_t* convMainFb = nullptr;
    const uint8_t* convZImg = nullptr;
    uint32_t convMainFbWidth = 0;
    bool sawOffscreen = false;
    int dbgGroup = -1; uint64_t dbgLastKey = 0; bool dbgLog = false, dbgOps = false;
    uint32_t dbgGeo = 0, dbgTimg = 0, dbgFmt = 0, dbgSiz = 0, dbgW = 0, dbgOthH = 0, dbgOthL = 0, dbgCcH = 0, dbgCcL = 0;
    std::unordered_map<uint32_t, uint32_t> dataCache; // datos convertidos (vertices/matrices) por direccion
    uint32_t vtxCount = 0, cmdCount = 0, errors = 0;
    SoftRdp soft;
    std::unordered_set<const uint8_t*> offscreenCmds; // comandos de dibujo que van a una textura (se quitan de la DL nativa)
    const uint8_t* mainFb = nullptr;
    uint32_t mainFbWidth = 0;
    // Imagenes aparte que reciben triangulos: se dibujan en el GPU (G_PS_FB) en vez del RDP por software
    struct FbTarget { uint32_t w = 0, h = 0; bool tris = false; std::vector<const uint8_t*> cmds; };
    std::unordered_map<const uint8_t*, FbTarget> fbTargets;
    const uint8_t* curOff = nullptr;
    std::unordered_map<const uint8_t*, std::pair<uint32_t, uint32_t>> gpuTargets; // destino -> ancho, alto
    // Destinos que el cuadro anterior dibujo en el GPU. El RDP por software ya no escribe en ellos: sus rectangulos
    // tambien van al GPU, y lo que el GPU copio a la memoria del juego al terminar el cuadro (fondo + modelo) no se
    // debe pisar. Antes el fondo se redibujaba aqui en cada cuadro, encima de esa copia, y el modelo del Pokemon
    // nunca se veia en las pantallas que pintan un fondo antes del modelo (resumen del laboratorio, renta).
    std::unordered_map<const uint8_t*, uint32_t> prevGpuTargets; // destino -> cuadros desde que se uso
    void markOff(const uint8_t* pc, bool tri) {
        offscreenCmds.insert(pc);
        if (curOff) { auto& t = fbTargets[curOff]; t.cmds.push_back(pc); t.tris |= tri; }
    }

    // Recorre la DL en orden de ejecucion solo para el RDP por software: sigue el estado del RDP y dibuja
    // lo que va a un color image que no es la pantalla. Marca esos comandos para quitarlos de la DL nativa.
    // Recorrido barato (sin convertir) para saber si el cuadro dibuja a una textura y el ancho de la pantalla.
    // Sirve para decidir el salto de cuadros antes de pagar la conversion completa.
    bool scanOffscreen(uint32_t dlAddr, uint32_t* fbWidth) {
        const uint8_t* stack[32];
        int sp = 0;
        std::memset(segments, 0, sizeof(segments));
        const uint8_t* pc = resolveRaw(dlAddr);
        const uint8_t *fb = nullptr, *z = nullptr;
        *fbWidth = 0;
        scanTris = 0;
        scanClears = false;
        scanCovers = false;
        const uint8_t* cur = nullptr;
        // Cobertura: pixeles de la pantalla tapados por rectangulos opacos (modo copia o relleno). Si la tapan
        // toda, el cuadro no depende de lo que habia antes y se puede saltar aunque sea 2D.
        static uint32_t cov[240][10];
        bool covUsed = false;
        uint32_t cycle = 0; // 2 copia, 3 relleno
        uint32_t oml = 0;   // SETOTHERMODE_L (modo de dibujo): solo vale cuando se vio completo en este cuadro
        bool omlKnown = false;
        // en 1 o 2 ciclos el rectangulo es opaco si el mezclador deja pasar el color tal cual (P = M = entrada, o sin
        // FORCE_BL), sin comparar alfa ni usar el alfa como cobertura
        auto opaque1c = [&]() {
            if (!omlKnown || (oml & 3) || (oml & 0x1000)) return false;
            return !(oml & 0x4000) || (((oml >> 30) & 3) == 0 && ((oml >> 22) & 3) == 0);
        };
        auto mark = [&](uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry) { // pixeles, lr inclusive
            const uint32_t sh = *fbWidth == 640 ? 1 : 0;
            ulx >>= sh; uly >>= sh; lrx >>= sh; lry >>= sh;
            if (lrx > 319) lrx = 319;
            if (lry > 239) lry = 239;
            if (ulx > lrx || uly > lry) return;
            if (!covUsed) { std::memset(cov, 0, sizeof cov); covUsed = true; }
            for (uint32_t y = uly; y <= lry; y++)
                for (uint32_t w = ulx >> 5; w <= (lrx >> 5); w++) {
                    uint32_t m = 0xFFFFFFFFu;
                    if (w == (ulx >> 5)) m &= 0xFFFFFFFFu << (ulx & 31);
                    if (w == (lrx >> 5)) m &= 0xFFFFFFFFu >> (31 - (lrx & 31));
                    cov[y][w] |= m;
                }
        };
        for (uint32_t guard = 0; pc && guard < 400000; guard++) {
            uint32_t w0 = be32(pc), w1 = be32(pc + 4);
            uint8_t op = w0 >> 24;
            const uint8_t* next = pc + 8;
            switch (op) {
                case G_DL: {
                    const uint8_t* t = resolveRaw(w1);
                    if (!t) break;
                    if (((w0 >> 16) & 0xFF) == 0) { if (sp < 32) stack[sp++] = next; else break; }
                    next = t;
                    break;
                }
                case G_ENDDL: next = sp ? stack[--sp] : nullptr; break;
                case G_MOVEWORD:
                    if (((w0 >> 16) & 0xFF) == G_MW_SEGMENT) segments[((w0 & 0xFFFF) >> 2) & 0x0F] = w1;
                    break;
                case 0x05: case 0x06: case 0x07: scanTris++; break;
                case G_SETZIMG: z = resolveRaw(w1); break;
                case G_SETCIMG: {
                    const uint8_t* p = resolveRaw(w1);
                    uint32_t w = (w0 & 0xFFF) + 1;
                    if (!p) break;
                    cur = p;
                    if (!fb && (w == 320 || w == 640)) { fb = p; *fbWidth = w; }
                    else if (p != fb && p != z) return true;
                    break;
                }
                case 0xF6: // FILLRECT sobre la pantalla que la cubre casi toda: el cuadro se dibuja completo
                    if (cur && cur == fb && cur != z) {
                        const uint32_t lrx = (w0 >> 14) & 0x3FF, lry = (w0 >> 2) & 0x3FF, ulx = (w1 >> 14) & 0x3FF, uly = (w1 >> 2) & 0x3FF;
                        if (lrx >= ulx + *fbWidth - 16 && lry >= uly + 200) scanClears = true;
                        if (cycle == 3) mark(ulx, uly, lrx, lry);
                    }
                    break;
                case 0xE4: // rectangulo con textura: en modo copia es opaco
                    gGfxScanDbg[cycle & 3]++;
                    if (cur && cur == fb && cur != z && (cycle == 2 || (cycle < 2 && opaque1c())))
                        mark((w1 >> 14) & 0x3FF, (w1 >> 2) & 0x3FF, (w0 >> 14) & 0x3FF, (w0 >> 2) & 0x3FF);
                    break;
                case 0xEF: cycle = (w0 >> 20) & 3; oml = w1; omlKnown = true; break;
                case 0xE2: { // SETOTHERMODE_L: el modo de dibujo son los bits 3-31
                    const uint32_t len = (w0 & 0xFF) + 1, sft = 32 - ((w0 >> 8) & 0xFF) - len;
                    const uint32_t mask = (len >= 32 ? 0xFFFFFFFFu : ((1u << len) - 1)) << sft;
                    oml = (oml & ~mask) | (w1 & mask);
                    if (sft <= 12 && sft + len >= 32) omlKnown = true;
                    break;
                }
                case 0xE3: { // SETOTHERMODE_H: tipo de ciclo en los bits 20-21
                    const uint32_t len = (w0 & 0xFF) + 1, sft = 32 - ((w0 >> 8) & 0xFF) - len;
                    if (sft <= 20 && sft + len >= 22) cycle = (w1 >> 20) & 3;
                    break;
                }
                default: break;
            }
            pc = next;
            scanCount = guard + 1;
        }
        if (covUsed) {
            bool all = true;
            for (uint32_t y = 0; y < 240 && all; y++)
                for (uint32_t w = 0; w < 10; w++)
                    if (cov[y][w] != 0xFFFFFFFFu) { all = false; break; }
            scanCovers = all;
            if (all) gGfxScanDbg[4]++;
        }
        return false;
    }
    uint32_t scanCount = 0; // comandos recorridos por el ultimo scanOffscreen (para saber si el cuadro cambio)
    uint32_t scanTris = 0;  // comandos de triangulos que vio
    bool scanClears = false; // borra (rellena) toda la pantalla antes de dibujar
    bool scanCovers = false; // rectangulos opacos tapan toda la pantalla
    void softWalk(uint32_t dlAddr) {
        const uint8_t* stack[32];
        int sp = 0;
        const uint8_t* pc = resolve(dlAddr);
        const uint8_t* zImg = nullptr;
        dbgGroup = -1; dbgLastKey = 0;
#ifndef __3DS__
        dbgLog = getenv("PS_DLLOG") != nullptr;
        dbgOps = getenv("PS_OPLOG") != nullptr;
        if (const char* og = getenv("PS_ONLYGROUP")) gGfxConvertOnlyGroup = atoi(og);
#endif
        soft.SetDrawing(false);
        fbTargets.clear();
        curOff = nullptr;
        for (uint32_t guard = 0; pc && guard < 400000; guard++) {
            uint32_t w0 = be32(pc), w1 = be32(pc + 4);
            uint8_t op = w0 >> 24;
            const uint8_t* next = pc + 8;
#ifndef __3DS__
            if (dbgOps) std::fprintf(stderr, "[op] %d %p %08x %08x\n", sp, (const void*)pc, w0, w1);
#endif
            switch (op) {
                case G_DL: {
                    const uint8_t* t = resolve(w1);
                    if (!t) break;
                    if (((w0 >> 16) & 0xFF) == 0) { if (sp < 32) stack[sp++] = next; else break; }
                    next = t;
                    break;
                }
                case G_ENDDL: next = sp ? stack[--sp] : nullptr; break;
                case G_MOVEWORD:
                    if (((w0 >> 16) & 0xFF) == G_MW_SEGMENT) segments[((w0 & 0xFFFF) >> 2) & 0x0F] = w1;
#ifndef __3DS__
                    else if (dbgLog && ((w0 >> 16) & 0xFF) == 2) std::fprintf(stderr, "[numlight] %u\n", w1 / 24);
#endif
                    break;
                case G_SETCIMG: {
                    uint8_t* p = const_cast<uint8_t*>(resolve(w1));
                    uint32_t w = (w0 & 0xFFF) + 1;
                    if (!mainFb && (w == 320 || w == 640)) { mainFb = p; mainFbWidth = w; }
                    // el z-buffer tambien se limpia con SETCIMG + relleno: eso lo maneja el interprete
                    bool off = p && p != mainFb && p != zImg;
                    soft.Command(w0, w1, p, 0, 0);
                    soft.SetDrawing(off);
                    soft.SetSkip(off && prevGpuTargets.count(p) != 0);
                    curOff = off ? p : nullptr;
                    if (off) { fbTargets[p].w = w; markOff(pc, false); }
                    break;
                }
                case G_SETZIMG: zImg = resolve(w1); break;
#ifndef __3DS__
                case G_MOVEMEM:
                    if (dbgLog && (w0 & 0xFF) == G_MV_LIGHT) {
                        const uint8_t* m = resolve(w1);
                        std::fprintf(stderr, "[light] off %u col %02x%02x%02x dir %d %d %d\n", ((w0 >> 8) & 0xFF) * 8,
                                     m ? m[0] : 0, m ? m[1] : 0, m ? m[2] : 0, m ? (int8_t)m[8] : 0, m ? (int8_t)m[9] : 0, m ? (int8_t)m[10] : 0);
                    }
                    break;
                case G_MTX:
                    if (dbgLog) {
                        const uint8_t* m = resolve(w1);
                        std::fprintf(stderr, "[mtx] p%02x", w0 & 0xFF);
                        for (int k = 0; m && k < 16; k++) {
                            int32_t v = (int32_t)(((uint32_t)be16(m + k * 2) << 16) | be16(m + 32 + k * 2));
                            std::fprintf(stderr, " %.2f", v / 65536.0);
                        }
                        std::fprintf(stderr, "\n");
                    }
                    break;
                case G_VTX:
                    if (dbgLog) {
                        const uint8_t* v = resolve(w1);
                        uint32_t n = (w0 >> 12) & 0xFF;
                        std::fprintf(stderr, "[vtx] n %u after g%d:", n, dbgGroup);
                        for (uint32_t k = 0; v && k < n && k < 4; k++)
                            std::fprintf(stderr, " (%d,%d,%d c %02x%02x%02x%02x)", (int16_t)be16(v + k * 16), (int16_t)be16(v + k * 16 + 2), (int16_t)be16(v + k * 16 + 4),
                                         v[k * 16 + 12], v[k * 16 + 13], v[k * 16 + 14], v[k * 16 + 15]);
                        std::fprintf(stderr, "\n");
                    }
                    break;
                case 0xFA: case 0xFB:
                    if (dbgLog) std::fprintf(stderr, "[%s] %08x\n", op == 0xFA ? "prim" : "env", w1);
                    soft.Command(w0, w1, nullptr, 0, 0);
                    break;
#endif
                case G_SETTIMG:
                    dbgTimg = w1; dbgFmt = (w0 >> 21) & 7; dbgSiz = (w0 >> 19) & 3; dbgW = (w0 & 0xFFF) + 1;
                    soft.Command(w0, w1, const_cast<uint8_t*>(resolve(w1)), 0, 0);
                    break;
                case 0xE4: case 0xE5: { // texrect + G_RDPHALF_1 + G_RDPHALF_2
                    uint32_t h1 = be32(pc + 12), h2 = be32(pc + 20);
#ifndef __3DS__
                    if (getenv("PS_DLLOG"))
                        std::fprintf(stderr, "[rect] %c x %d-%d y %d-%d timg %08x tile %08x othH %06x othL %08x cc %06x:%08x\n",
                                     op == 0xE5 ? 'F' : 'R', ((w1 >> 12) & 0xFFF) >> 2, ((w0 >> 12) & 0xFFF) >> 2, (w1 & 0xFFF) >> 2,
                                     (w0 & 0xFFF) >> 2, dbgTimg, soft.DbgTile((w1 >> 24) & 7), soft.DbgOtherHi(), soft.DbgOtherLo(), dbgCcH, dbgCcL);
#endif
                    soft.Command(w0, w1, nullptr, h1, h2);
                    if (soft.Offscreen()) markOff(pc, false);
                    next = pc + 24;
                    break;
                }
                case 0xF6: case 0x05: case 0x06: case 0x07:
                    if (op != 0xF6 && (dbgLog || gGfxConvertOnlyGroup >= 0)) {
                        // depuracion: grupos de triangulos con el mismo estado (combinador, modos, textura)
                        uint64_t key = ((uint64_t)dbgCcH << 40) ^ ((uint64_t)dbgCcL << 8) ^ soft.DbgOtherHi() ^ ((uint64_t)soft.DbgOtherLo() << 20) ^ dbgTimg;
                        if (key != dbgLastKey) dbgGroup++;
                        if (gGfxConvertOnlyGroup >= 0 && gGfxConvertOnlyGroup != dbgGroup) offscreenCmds.insert(pc);
#ifndef __3DS__
                        if (key != dbgLastKey && dbgLog)
                            std::fprintf(stderr, "[g%d] [tri] timg %08x t0 %08x t1 %08x othH %06x othL %08x cc %06x:%08x geo %08x\n", dbgGroup, dbgTimg,
                                         soft.DbgTile(0), soft.DbgTile(1), soft.DbgOtherHi(), soft.DbgOtherLo(), dbgCcH, dbgCcL, dbgGeo);
#endif
                        dbgLastKey = key;
                    }
                    soft.Command(w0, w1, nullptr, 0, 0);
                    if (soft.Offscreen()) markOff(pc, op != 0xF6);
                    break;
                default:
                    if (op == 0xFC) { dbgCcH = w0 & 0xFFFFFF; dbgCcL = w1; }
                    if (op == 0xD9) dbgGeo = (dbgGeo & (w0 & 0xFFFFFF)) | w1;
#ifndef __3DS__
                    if (op == 0xED && getenv("PS_DLLOG"))
                        std::fprintf(stderr, "[scissor] %d,%d - %d,%d\n", ((w0 >> 12) & 0xFFF) >> 2, (w0 & 0xFFF) >> 2, ((w1 >> 12) & 0xFFF) >> 2, (w1 & 0xFFF) >> 2);
#endif
                    if (op == 0xE3) dbgOthH = (dbgOthH & ~0) | w1; // aproximado: solo para el registro
                    if (op == 0xED && curOff) { uint32_t lry = (w1 & 0xFFF) >> 2; auto& t = fbTargets[curOff]; if (lry > t.h) t.h = lry; }
                    if (op == 0xE2) dbgOthL = w1;
                    if (op >= 0xE2) soft.Command(w0, w1, nullptr, 0, 0);
                    break;
            }
            pc = next;
        }
        soft.EndFrame();
        soft.SetDrawing(false);
        // las imagenes con triangulos se quedan en la DL nativa y se dibujan en el GPU
        gpuTargets.clear();
        for (auto& kv : fbTargets) {
            const FbTarget& t = kv.second;
            if (!t.tris || !gGfxConvertGpuFb) continue;
            const uint32_t h = t.h ? t.h : t.w * 3 / 4;
            if (t.w == 0 || t.w > 320 || h == 0 || h > 240) continue;
            for (const uint8_t* c : t.cmds) offscreenCmds.erase(c);
            gpuTargets[kv.first] = { t.w, h };
        }
        soft.SetSkip(false);
        for (auto it = prevGpuTargets.begin(); it != prevGpuTargets.end();) { if (++it->second > 8) it = prevGpuTargets.erase(it); else ++it; }
        for (auto& kv : gpuTargets) prevGpuTargets[kv.first] = 0;
    }

    void* alloc(size_t n) {
        n = (n + 7) & ~size_t(7);
        if (used + n > arena.size()) { errors++; return nullptr; }
        void* p = arena.data() + used;
        used += n;
        return p;
    }

    // Direccion del juego (segmentada, KSEG0 o nativa del bloque) -> apuntador en memoria del host
    const uint8_t* host(uint32_t addr) {
        return cfg.toHost ? cfg.toHost(addr) : reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(addr));
    }

    const uint8_t* resolve(uint32_t addr) {
        const uint8_t* r = resolveRaw(addr);
        if (!r && addr && unresolvedLogged < 8) {
            unresolvedLogged++;
            std::fprintf(stderr, "[gfx_convert] direccion sin resolver %08x\n", (unsigned)addr);
        }
        return r;
    }
    uint32_t unresolvedLogged = 0;

    const uint8_t* resolveRaw(uint32_t addr) {
        if (cfg.isBlobPtr(addr)) return host(addr);
        uint32_t seg = (addr >> 24) & 0x0F;
        if ((addr >> 28) == 0) {
            usedSegs |= 1u << seg;
            uint32_t base = segments[seg];
            if (base == 0) return nullptr;
            uint32_t a = base + (addr & 0x00FFFFFF);
            return cfg.isBlobPtr(a) ? host(a) : nullptr;
        }
        if ((addr >> 28) == 0x8 || (addr >> 28) == 0xA) {
            uint32_t a = cfg.n64ToBlob(addr);
            return a ? host(a) : nullptr;
        }
        return nullptr;
    }

    // Vertices convertidos persistentes: misma direccion de un cuadro a otro mientras el contenido no cambie,
    // para que el interprete pueda reconocer geometria ya vista (corridas estaticas en el GPU).
    struct PersistentVtx { uint8_t* buf; uint32_t n; uint32_t hash; uint32_t frame; };
    std::unordered_map<const uint8_t*, PersistentVtx> vtxCache;
    uint32_t frameSerial = 0;
    static uint32_t hashBytes(const uint8_t* p, uint32_t bytes) {
        uint32_t h = 2166136261u;
        for (uint32_t i = 0; i + 4 <= bytes; i += 4) { uint32_t w; std::memcpy(&w, p + i, 4); h = (h ^ w) * 16777619u; }
        return h;
    }
    uint32_t convertVtx(const uint8_t* src, uint32_t n) {
        uint32_t h = hashBytes(src, n * 16);
        auto it = vtxCache.find(src);
        uint8_t* dst;
        if (it != vtxCache.end() && it->second.n == n) {
            it->second.frame = frameSerial;
            vtxCount += n;
            if (it->second.hash == h) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(it->second.buf));
            it->second.hash = h;
            dst = it->second.buf; // mismo lugar, contenido nuevo (vertices animados por CPU)
        } else {
            if (it != vtxCache.end()) { std::free(it->second.buf); vtxCache.erase(it); }
            dst = static_cast<uint8_t*>(std::malloc(n * 16));
            if (!dst) { errors++; return 0; }
            vtxCache.emplace(src, PersistentVtx{ dst, n, h, frameSerial });
        }
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t* s = src + i * 16;
            uint8_t* d = dst + i * 16;
            for (int k = 0; k < 12; k += 2) { uint16_t v = be16(s + k); std::memcpy(d + k, &v, 2); }
            std::memcpy(d + 12, s + 12, 4); // color/normal: bytes
        }
        vtxCount += n;
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dst));
    }
    void evictVtx() {
        for (auto it = vtxCache.begin(); it != vtxCache.end();) {
            if (frameSerial - it->second.frame > 120) { std::free(it->second.buf); it = vtxCache.erase(it); }
            else ++it;
        }
    }

    uint32_t convertWords(const uint8_t* src, uint32_t bytes) {
        uint32_t* dst = static_cast<uint32_t*>(alloc(bytes));
        if (!dst) return 0;
        for (uint32_t i = 0; i < bytes / 4; i++) dst[i] = be32(src + i * 4);
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dst));
    }

    uint32_t convertHalves(const uint8_t* src, uint32_t bytes) {
        uint16_t* dst = static_cast<uint16_t*>(alloc(bytes));
        if (!dst) return 0;
        for (uint32_t i = 0; i < bytes / 2; i++) dst[i] = be16(src + i * 2);
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dst));
    }

    // ---------------- modelos con huesos (skinning) ----------------
    // Un Pokemon llega como [G_MTX hueso][G_DL malla]... con estado de texturas entre medio. Se recorre aqui
    // una vez: cada vertice guarda su hueso y los triangulos se agrupan por material. El interprete recibe un
    // comando G_PS_SKIN (0x41) por lote en vez de los G_VTX/G_TRI de cada hueso.
    struct SkinVtx { uint8_t v[16]; uint8_t bone; uint8_t pad[7]; }; // 24 bytes: F3DVtx (alineado a 8) + hueso
    // segmento: vertices unicos + indices de triangulos (un vertice compartido se calcula una sola vez)
    struct SkinSeg { std::vector<uint32_t> ops; std::vector<SkinVtx> verts; std::vector<uint16_t> idx; };
    struct SkinRun {
        bool active = false;
        bool dirty = false; // se uso: hay que dejarlo como nuevo antes de reusarlo
        std::vector<uint32_t> bones; // apuntadores a matrices ya convertidas (int32 nativos)
        int curBone = -1;
        // rtag: lote en el que se cargo la ranura (para saber si un lote lee ranuras que dejo otro)
        struct Slot { uint8_t v[16]; uint8_t bone; bool valid; uint32_t useg; uint16_t uid; uint32_t rtag; const uint8_t* src; } slots[64] = {};
        std::vector<SkinSeg> segs;
        uint32_t segSerial = 1;
        int lastMeshSlot = -1;
        // cache de modelos: las mallas del lote se anotan y se hornean (o se toman de la cache) al cerrar el lote
        std::vector<mc::Token> toks;
        uint32_t runStartBones = 0, runTag = 1;
        bool collecting = false;
        uint64_t inMask = 0;           // ranuras que el lote leyo sin haberlas cargado
        std::vector<mc::SlotIn> ins;
        std::vector<mc::Range> rgs;
        void newSeg() { segs.emplace_back(); segSerial++; }
        void reset() { toks.clear(); runStartBones = 0; runTag++; active = false; bones.clear(); curBone = -1; segs.clear(); lastMeshSlot = -1; segSerial++; for (auto& sl : slots) sl.valid = false; }
    };
    static constexpr uint8_t G_PS_SKIN = 0x41;
    static constexpr uint8_t G_PS_FB = 0x4D; // dibujar en una imagen del GPU que luego se copia a la memoria del juego
    uint32_t gpuFbCmds = 0;
    uint32_t skinBatches = 0, skinTris = 0;
    std::vector<std::unique_ptr<SkinRun>> skinPool;
    uint32_t meshStats[4] = {}; // tri en mallas puras / mezcladas, listas puras / mezcladas

    // Malla: solo vertices, triangulos, modo de geometria y sincronias
    bool meshLike(const uint8_t* d) {
        if (!d) return false;
        for (int k = 0; k < 4096; k++) {
            uint8_t op = d[k * 8];
            switch (op) {
                case G_VTX: case 0x05: case 0x06: case 0x07: case 0xD9: case 0xE7: case 0x00: break;
                case G_ENDDL: return true;
                default: return false;
            }
        }
        return false;
    }
    // Lista solo de estado del RDP (cargas de texturas, modos): se puede intercalar sin romper el modelo
    static bool stateOp(uint8_t op) {
        return (op >= 0xE2 && op != 0xE4 && op != 0xE5 && op != 0xF6 && op != 0xFF && op != 0xFE) ||
               op == 0xD7 || op == 0xD9 || op == G_MOVEWORD || op == G_MOVEMEM || op == 0x00;
    }
    bool stateOnlyDl(const uint8_t* d) {
        if (!d) return false;
        for (int k = 0; k < 4096; k++) {
            uint8_t op = d[k * 8];
            if (op == G_ENDDL) return true;
            if (!stateOp(op) || op == G_MOVEWORD) return false;
        }
        return false;
    }
    static void vtxToNative(const uint8_t* s, uint8_t* d) {
        for (int k = 0; k < 12; k += 2) { uint16_t v = be16(s + k); std::memcpy(d + k, &v, 2); }
        std::memcpy(d + 12, s + 12, 4);
    }
    void skinTri(SkinRun& r, int a, int b, int c) {
        if (a >= 64 || b >= 64 || c >= 64) return;
        if (r.collecting)
            for (int idx : { a, b, c }) {
                const auto& sl = r.slots[idx];
                if (sl.rtag == r.runTag || (r.inMask >> idx & 1)) continue;
                r.inMask |= 1ull << idx;
                r.ins.push_back({ sl.valid ? sl.src : nullptr, (uint8_t)idx, sl.valid ? sl.bone : (uint8_t)0, (uint8_t)sl.valid, 0 });
            }
        if (!r.slots[a].valid || !r.slots[b].valid || !r.slots[c].valid) return;
        if (r.segs.empty()) r.newSeg();
        auto& sg = r.segs.back();
        if (sg.verts.size() > 65000) { r.newSeg(); return skinTri(r, a, b, c); }
        for (int idx : { a, b, c }) {
            auto& sl = r.slots[idx];
            if (sl.useg != r.segSerial) {
                SkinVtx sv{};
                std::memcpy(sv.v, sl.v, 16);
                sv.bone = sl.bone;
                sl.uid = (uint16_t)r.segs.back().verts.size();
                sl.useg = r.segSerial;
                r.segs.back().verts.push_back(sv);
            }
            r.segs.back().idx.push_back(sl.uid);
        }
        skinTris++;
    }
    void skinMesh(SkinRun& r, const uint8_t* d) {
        for (int k = 0; k < 4096; k++) {
            uint32_t w0 = be32(d + k * 8), w1 = be32(d + k * 8 + 4);
            uint8_t op = w0 >> 24;
            if (op == G_ENDDL) {
                if (r.collecting) r.rgs.push_back({ reinterpret_cast<uintptr_t>(d), (uint32_t)(k + 1) * 8 });
                return;
            }
            if (op == G_VTX) {
                uint32_t n = (w0 >> 12) & 0xFF, end = (w0 & 0xFF) >> 1;
                const uint8_t* v = resolve(w1);
                if (!v || end < n || end > 64) continue;
                if (r.collecting && n) r.rgs.push_back({ reinterpret_cast<uintptr_t>(v), n * 16 });
                for (uint32_t j = 0; j < n; j++) {
                    auto& sl = r.slots[end - n + j];
                    vtxToNative(v + j * 16, sl.v);
                    sl.bone = (uint8_t)r.curBone;
                    sl.valid = true;
                    sl.useg = 0;
                    sl.rtag = r.runTag;
                    sl.src = v + j * 16;
                }
            } else if (op == 0x05) {
                skinTri(r, ((w0 >> 16) & 0xFF) / 2, ((w0 >> 8) & 0xFF) / 2, (w0 & 0xFF) / 2);
            } else if (op == 0x06 || op == 0x07) {
                skinTri(r, ((w0 >> 16) & 0xFF) / 2, ((w0 >> 8) & 0xFF) / 2, (w0 & 0xFF) / 2);
                skinTri(r, ((w1 >> 16) & 0xFF) / 2, ((w1 >> 8) & 0xFF) / 2, (w1 & 0xFF) / 2);
            } else if (op == 0xD9) {
                if (r.segs.empty() || !r.segs.back().idx.empty()) r.newSeg();
                r.segs.back().ops.push_back(~(w0 & 0xFFFFFF) & 0xFFFFFF);
                r.segs.back().ops.push_back(w1);
            }
        }
    }
    // Lote en la arena: 'PSSK', nBones, nSegs, bones[], y por segmento: nOps, nVtx, nIdx, ops[2*nOps], vtx[nVtx],
    // idx[nIdx] (u16, relleno a 4 bytes). El "cuerpo" es todo lo que va despues de bones[]: es lo que se hornea.
    static size_t skinBodyBytes(const SkinRun& r) {
        size_t bytes = 0;
        for (auto& sg : r.segs) bytes += 12 + sg.ops.size() * 4 + sg.verts.size() * sizeof(SkinVtx) + ((sg.idx.size() * 2 + 3) & ~size_t(3));
        return bytes;
    }
    static void skinWriteBody(const SkinRun& r, uint32_t* p) {
        for (auto& sg : r.segs) {
            *p++ = (uint32_t)(sg.ops.size() / 2); *p++ = (uint32_t)sg.verts.size(); *p++ = (uint32_t)sg.idx.size();
            for (uint32_t o : sg.ops) *p++ = o;
            std::memcpy(p, sg.verts.data(), sg.verts.size() * sizeof(SkinVtx));
            p += sg.verts.size() * sizeof(SkinVtx) / 4;
            if (!sg.idx.empty()) {
                std::memcpy(p, sg.idx.data(), sg.idx.size() * 2);
                if (sg.idx.size() & 1) reinterpret_cast<uint16_t*>(p)[sg.idx.size()] = 0; // relleno fijo (comparable)
            }
            p += (sg.idx.size() * 2 + 3) / 4;
        }
    }
    // encabezado y huesos del cuadro; regresa donde va el cuerpo (nullptr si no hubo arena)
    uint32_t* skinEmit(SkinRun& r, uint32_t* out, uint32_t nSegs, size_t bodyBytes) {
        uint32_t* b = static_cast<uint32_t*>(alloc(12 + r.bones.size() * 4 + bodyBytes));
        if (!b) return nullptr;
        uint32_t* p = b;
        *p++ = 0x5053534Bu; *p++ = (uint32_t)r.bones.size(); *p++ = nSegs;
        for (uint32_t m : r.bones) *p++ = m;
        out[r.lastMeshSlot * 2] = (uint32_t)G_PS_SKIN << 24;
        out[r.lastMeshSlot * 2 + 1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b));
        skinBatches++;
        return p;
    }
    // F2: encabezado 'PSK2', nBones, nSegs, id del lote, apuntador al cuerpo (en la cache), bytes del cuerpo, bones[]
    bool skinEmitResident(SkinRun& r, uint32_t* out, uint32_t id, const uint8_t* body, uint32_t nSegs, uint32_t bodyBytes) {
        if (!body) return false;
        uint32_t* b = static_cast<uint32_t*>(alloc(24 + r.bones.size() * 4));
        if (!b) return false;
        uint32_t* p = b;
        *p++ = 0x50534B32u; *p++ = (uint32_t)r.bones.size(); *p++ = nSegs;
        *p++ = id; *p++ = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(body)); *p++ = bodyBytes;
        for (uint32_t m : r.bones) *p++ = m;
        out[r.lastMeshSlot * 2] = (uint32_t)G_PS_SKIN << 24;
        out[r.lastMeshSlot * 2 + 1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b));
        skinBatches++;
        return true;
    }
    void skinFlush(SkinRun& r, uint32_t* out) {
        if (r.toks.empty()) {
            if (!r.segs.empty() && r.lastMeshSlot >= 0)
                if (uint32_t* p = skinEmit(r, out, (uint32_t)r.segs.size(), skinBodyBytes(r))) skinWriteBody(r, p);
            r.segs.clear();
        } else {
            skinFlushCached(r, out);
        }
        r.runStartBones = (uint32_t)r.bones.size();
        r.runTag++;
    }

    // ---------------- cache de modelos (model_cache.h) ----------------
    // Rehace el lote con las mallas anotadas, igual que si se hubieran recorrido en su lugar: entre que se
    // anota una malla y se cierra el lote no se ejecuta ningun otro comando (los segmentos no cambian).
    uint32_t skinReplay(SkinRun& r, uint32_t* segMask) {
        const uint32_t savedUsed = usedSegs, tris0 = skinTris;
        const int cur = r.curBone;
        usedSegs = 0;
        r.collecting = true;
        r.inMask = 0;
        r.ins.clear();
        r.rgs.clear();
        for (auto& t : r.toks) { r.curBone = t.bone; skinMesh(r, t.mesh); }
        r.collecting = false;
        r.curBone = cur;
        *segMask = usedSegs;
        usedSegs = savedUsed | *segMask;
        return skinTris - tris0;
    }
    static void skinApplySlots(SkinRun& r, const mc::SlotOut* so, uint32_t n) {
        for (uint32_t i = 0; i < n; i++) {
            auto& sl = r.slots[so[i].slot];
            std::memcpy(sl.v, so[i].v, 16);
            sl.bone = so[i].bone;
            sl.valid = true;
            sl.useg = 0;
            sl.rtag = r.runTag;
            sl.src = so[i].src;
        }
    }
    uint32_t mcVerifyLogged = 0;
    void skinFlushCached(SkinRun& r, uint32_t* out) {
        mc::Key k{ r.toks.data(), (uint32_t)r.toks.size(), r.runStartBones, (uint32_t)r.bones.size(), 0 };
        k.hash = mc::ModelCache::hashKey(k.tokens, k.nTokens, k.startBones, k.endBones);
        mc::Hit h{};
        bool hit = r.lastMeshSlot >= 0 && gModelCache.lookup(k, segments, &h);
        for (uint32_t i = 0; hit && i < h.nInputs; i++) {
            const auto& in = h.inputs[i];
            const auto& sl = r.slots[in.slot];
            if (sl.valid != (bool)in.valid || (in.valid && (sl.src != in.src || sl.bone != in.bone))) { hit = false; gModelCache.noteInputMismatch(); }
        }
        if (hit) {
            if (gModelCache.verify()) skinVerify(r, h);
            if (!(gGfxConvertF2 && skinEmitResident(r, out, h.id, h.body, h.nSegs, h.bodyBytes)))
                if (uint32_t* p = skinEmit(r, out, h.nSegs, h.bodyBytes)) std::memcpy(p, h.body, h.bodyBytes);
            skinApplySlots(r, h.slots, h.nSlots);
            usedSegs |= h.segMask;
            skinTris += h.tris;
            r.segSerial++;
            r.toks.clear();
            r.segs.clear();
            return;
        }
        uint32_t segMask = 0;
        const uint32_t tris = skinReplay(r, &segMask);
        if (r.segs.empty() || r.lastMeshSlot < 0) {
            gModelCache.noteUncacheable(mc::UC_EMPTY);
        } else {
            const size_t bodyBytes = skinBodyBytes(r);
            if (uint32_t* p = skinEmit(r, out, (uint32_t)r.segs.size(), bodyBytes)) {
                skinWriteBody(r, p);
                mc::SlotOut so[64];
                uint32_t n = 0;
                for (uint32_t i = 0; i < 64; i++)
                    if (r.slots[i].valid && r.slots[i].rtag == r.runTag) {
                        so[n] = {};
                        so[n].src = r.slots[i].src; so[n].slot = (uint8_t)i; so[n].bone = r.slots[i].bone; std::memcpy(so[n].v, r.slots[i].v, 16); n++;
                    }
                // lo que se lee de ranuras ajenas tambien es dependencia de memoria
                for (auto& in : r.ins)
                    if (in.valid) r.rgs.push_back({ reinterpret_cast<uintptr_t>(in.src), 16 });
                const uint32_t id = gModelCache.store(k, segMask, segments, reinterpret_cast<const uint8_t*>(p), (uint32_t)bodyBytes,
                                                      (uint32_t)r.segs.size(), so, n, r.ins.data(), (uint32_t)r.ins.size(), tris, r.rgs.data(),
                                                      (uint32_t)r.rgs.size());
                // recien horneado: este mismo cuadro ya puede ir por F2 (el 'PSSK' de la arena se reemplaza)
                if (id && gGfxConvertF2) skinEmitResident(r, out, id, gModelCache.bodyOf(id), (uint32_t)r.segs.size(), (uint32_t)bodyBytes);
            }
        }
        r.segs.clear();
        r.toks.clear();
    }
    // Modo de verificacion: el acierto se rehace por el camino de siempre y se compara cuerpo, ranuras y triangulos
    void skinVerify(SkinRun& r, const mc::Hit& h) {
        SkinRun::Slot saved[64];
        std::memcpy(saved, r.slots, sizeof saved);
        const uint32_t serial = r.segSerial;
        uint32_t segMask = 0;
        const uint32_t tris = skinReplay(r, &segMask);
        skinTris -= tris;
        std::vector<uint32_t> body(skinBodyBytes(r) / 4);
        skinWriteBody(r, body.data());
        SkinRun::Slot legacy[64];
        std::memcpy(legacy, r.slots, sizeof legacy);
        const uint32_t nSegs = (uint32_t)r.segs.size();
        r.segs.clear();
        std::memcpy(r.slots, saved, sizeof saved);
        r.segSerial = serial;
        skinApplySlots(r, h.slots, h.nSlots);
        const char* why = nullptr;
        if (body.size() * 4 != h.bodyBytes || nSegs != h.nSegs) why = "tamano";
        else if (std::memcmp(body.data(), h.body, h.bodyBytes) != 0) why = "cuerpo";
        else if (tris != h.tris) why = "triangulos";
        else if (segMask != h.segMask) why = "segmentos";
        else
            for (int i = 0; i < 64 && !why; i++)
                if (legacy[i].valid != r.slots[i].valid ||
                    (legacy[i].valid && (legacy[i].bone != r.slots[i].bone || legacy[i].src != r.slots[i].src ||
                                         std::memcmp(legacy[i].v, r.slots[i].v, 16) != 0)))
                    why = "ranuras";
        // lo restaurado arriba vuelve a ser el estado previo: el llamador aplica de nuevo las ranuras del acierto
        std::memcpy(r.slots, saved, sizeof saved);
        gModelCache.noteVerify(!why);
        if (why && mcVerifyLogged < 20) {
            mcVerifyLogged++;
            char b[160];
            std::snprintf(b, sizeof b, "[mc] verificacion fallo (%s): lote %lu, %u mallas, %lu bytes\n", why, (unsigned long)h.id,
                          (unsigned)r.toks.size(), (unsigned long)h.bodyBytes);
            host_print(b);
        }
    }


    // ---------------- mallas fijas (estadio, escenario) ----------------
    // Lista con estado y geometria mezclados: cada racha de G_VTX/G_TRI entre cambios de estado se compila a un
    // lote G_PS_SKIN de un solo "hueso" (la modelview actual). El interprete deja de cargar vertices y armar
    // triangulos en el CPU; el GPU transforma. Si algo de la lista no se puede seguir, se deja como estaba.
    uint32_t meshBlocks = 0, meshBlockTris = 0, meshPlanFails = 0;
    bool planMeshBlocks(const uint8_t* src, uint32_t n, std::vector<uint32_t>& blockAt, std::vector<uint8_t>& consumed) {
        struct Slot { uint8_t v[16]; bool valid; uint32_t useg; uint16_t uid; };
        static Slot slots[64]; // termina antes de recorrer las sublistas: no se pisa
        for (auto& sl : slots) { sl.valid = false; sl.useg = 0; }
        std::vector<SkinVtx> verts;
        std::vector<uint16_t> tris; // indices
        uint32_t runSerial = 1;
        int last = -1;
        bool any = false;
        blockAt.assign(n, 0);
        consumed.assign(n, 0);
        auto endRun = [&]() {
            if (last >= 0 && !tris.empty()) {
                size_t bytes = 12 + 4 + 12 + verts.size() * sizeof(SkinVtx) + ((tris.size() * 2 + 3) & ~size_t(3));
                uint32_t* b = static_cast<uint32_t*>(alloc(bytes));
                if (b) {
                    uint32_t* q = b;
                    *q++ = 0x5053534Bu; *q++ = 1; *q++ = 1; *q++ = 0; // un hueso: la matriz actual
                    *q++ = 0; *q++ = (uint32_t)verts.size(); *q++ = (uint32_t)tris.size();
                    std::memcpy(q, verts.data(), verts.size() * sizeof(SkinVtx));
                    q += verts.size() * sizeof(SkinVtx) / 4;
                    std::memcpy(q, tris.data(), tris.size() * 2);
                    blockAt[last] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b));
                    meshBlocks++;
                    meshBlockTris += (uint32_t)tris.size() / 3;
                    any = true;
                }
            }
            tris.clear();
            verts.clear();
            runSerial++;
            last = -1;
        };
        auto tri = [&](int a, int b, int c) -> bool {
            for (int idx : { a, b, c })
                if (idx >= 64 || !slots[idx].valid) return false;
            if (verts.size() > 65000) return false;
            for (int idx : { a, b, c }) {
                auto& sl = slots[idx];
                if (sl.useg != runSerial) {
                    SkinVtx sv{};
                    std::memcpy(sv.v, sl.v, 16);
                    sl.uid = (uint16_t)verts.size();
                    sl.useg = runSerial;
                    verts.push_back(sv);
                }
                tris.push_back(sl.uid);
            }
            return true;
        };
        bool anyValid = false; // casi todas las listas (HUD, estado) nunca cargan vertices: no recorrer las 64 ranuras
        auto invalidate = [&]() { if (!anyValid) return; anyValid = false; for (auto& sl : slots) sl.valid = false; };
        // Un G_VTX solo se puede quitar si nadie fuera de esta lista lee lo que cargo. Los efectos de los ataques
        // cargan el cuadrado aqui y lo dibujan en una sublista (G_VTX, G_DL -> G_TRI2): sin el G_VTX esos
        // triangulos salian con los vertices que hubiera dejado el dibujo anterior. Al llamar una sublista o al
        // terminar la lista, los G_VTX cuyas ranuras siguen cargadas se dejan para el interprete.
        int slotCmd[64];
        bool anyCmd = false;
        for (int& c : slotCmd) c = -1;
        auto keepLoaded = [&]() {
            if (!anyCmd) return;
            anyCmd = false;
            for (int& c : slotCmd) { if (c >= 0) consumed[c] = 0; c = -1; }
        };
        for (uint32_t i = 0; i < n; i++) {
            uint32_t w0 = be32(src + i * 8), w1 = be32(src + i * 8 + 4);
            uint8_t op = w0 >> 24;
            switch (op) {
                case G_VTX: {
                    uint32_t cnt = (w0 >> 12) & 0xFF, end = (w0 & 0xFF) >> 1;
                    const uint8_t* v = resolve(w1);
                    if (!v || end < cnt || end > 64) return false;
                    for (uint32_t j = 0; j < cnt; j++) {
                        vtxToNative(v + j * 16, slots[end - cnt + j].v);
                        slots[end - cnt + j].valid = true;
                        slots[end - cnt + j].useg = 0;
                        slotCmd[end - cnt + j] = (int)i;
                    }
                    if (cnt) anyValid = anyCmd = true;
                    consumed[i] = 1;
                    break;
                }
                case 0x05:
                    if (!tri(((w0 >> 16) & 0xFF) / 2, ((w0 >> 8) & 0xFF) / 2, (w0 & 0xFF) / 2)) return false;
                    consumed[i] = 1; last = (int)i;
                    break;
                case 0x06: case 0x07:
                    if (!tri(((w0 >> 16) & 0xFF) / 2, ((w0 >> 8) & 0xFF) / 2, (w0 & 0xFF) / 2)) return false;
                    if (!tri(((w1 >> 16) & 0xFF) / 2, ((w1 >> 8) & 0xFF) / 2, (w1 & 0xFF) / 2)) return false;
                    consumed[i] = 1; last = (int)i;
                    break;
                case 0x00: case 0xE6: case 0xE7: case 0xE8: case 0xE9: case 0xE1: case 0xF1:
                    break; // no tocan vertices ni estado
                case 0x02: case 0x03: case 0x04:
                    return false; // MODIFYVTX / CULLDL / BRANCH_Z leen los vertices cargados
                case 0xD9: {
                    endRun();
                    const uint32_t touched = (~(w0 & 0xFFFFFF) & 0xFFFFFF) | w1;
                    if (touched & 0x000F0000) invalidate(); // luz, texgen o niebla se calculan al cargar
                    break;
                }
                case 0xD7: // escala de UV: se aplica al cargar
                    endRun(); invalidate();
                    break;
                default:
                    endRun();
                    if (op < 0xE2 || op == 0xFE || op == 0xFF) invalidate(); // matrices, luces, sublistas, etc.
                    if (op == G_DL || op == G_ENDDL) { invalidate(); keepLoaded(); }
                    break;
            }
        }
        endRun();
        keepLoaded();
        return any;
    }

    // --- capa del HUD: llave sin convertir ---
    struct HW { uint64_t hash; uint8_t flags; uint16_t sets; }; // flags: 1 con llave, 2 no apta, 4 vertices, 8 triangulos, 16 dibujos
    std::unordered_map<uint32_t, HW> hwCache;
    void hOne(uint8_t op, uint32_t w0, uint32_t w1, int depth) {
        if (op == G_SETTIMG) {
            hTimg = resolve(w1); hTimgSiz = (w0 >> 19) & 3; hTimgW = (w0 & 0xFFF) + 1;
            if (!hTimg || gpuTargets.count(hTimg)) hBad = true, hWhy |= 1u << 8;
        }
        hCmd(op, w0, w1);
        if (op == G_MOVEWORD && ((w0 >> 16) & 0xFF) == G_MW_SEGMENT) { segments[((w0 & 0xFFFF) >> 2) & 0x0F] = w1; hwCache.clear(); }
        if (op == G_DL) {
            const HW c = hWalk(w1, depth + 1);
            if ((c.flags & 3) != 1) hBad = true, hWhy |= 1u << 7;
            hF |= c.flags & 28; hS |= c.sets;
            hMix((uint32_t)(c.hash >> 32)); hMix((uint32_t)c.hash);
        }
    }
    HW hWalk(uint32_t addr, int depth) {
        auto it = hwCache.find(addr);
        if (it != hwCache.end()) return it->second;
        HW r{ 0, 2, 0 };
        const uint8_t* d = resolve(addr);
        if (!d || depth > 10) return r;
        const uint32_t oA = hA, oB = hB;
        const uint8_t oF = hF;
        const uint16_t oS = hS;
        const bool oBad = hBad;
        hA = 0x811C9DC5u; hB = 0x1234567u; hF = 0; hS = 0; hBad = false;
        for (uint32_t i = 0;; i++) {
            const uint32_t w0 = be32(d + i * 8), w1 = be32(d + i * 8 + 4);
            const uint8_t op = w0 >> 24;
            if (op == G_ENDDL) break;
            if (i >= 20000) { hBad = true; break; }
            hOne(op, w0, w1, depth);
            if (op == G_DL && ((w0 >> 16) & 0xFF) == 1) break;
        }
        r.hash = ((uint64_t)hA << 32) | hB;
        r.flags = (hBad ? 2 : 1) | ((hF & 5) ? 4 : 0) | (hF & 24);
        r.sets = hS;
        hA = oA; hB = oB; hF = oF; hS = oS; hBad = oBad;
        hwCache[addr] = r;
        return r;
    }
    void hudPass1(const uint8_t* src, uint32_t b, uint32_t e, uint32_t n) {
        const uint32_t L = e - b;
        hudCmdH.assign(L, 0); hudCmdF.assign(L, 0); hudCmdS.assign(L, 0);
        hBad = false; hWhy = 0;
        uint32_t savedSeg[16];
        std::memcpy(savedSeg, segments, sizeof savedSeg);
        hwCache.clear();
        for (uint32_t k = 0; k < L; k++) {
            const uint8_t* c = src + (b + k) * 8;
            const uint32_t w0 = be32(c), w1 = be32(c + 4);
            const uint8_t op = w0 >> 24;
            hA = 0x811C9DC5u; hB = 0x1234567u; hF = 0; hS = 0;
            // modelos con huesos dentro del tramo: no es HUD
            if (gGfxConvertSkin && op == G_MTX && ((w0 & 0xFF) ^ 1) == 2 && b + k + 1 < n && c[8] == G_DL && c[9] == 0 && meshLike(resolve(be32(c + 12))))
                hBad = true, hWhy |= 1u << 10;
            // la mitad de los comandos (mitades de rectangulo, sincronias, tamano de tile) no leen memoria ni cuentan
            // para las banderas: solo sus dos palabras
            if (op == 0xE1 || op == 0xF1 || op == 0xE6 || op == 0xE7 || op == 0xE8 || op == 0xE9 || op == 0xF2 || op == 0x00) { hMix(w0); hMix(w1); }
            else hOne(op, w0, w1, 0);
            hudCmdH[k] = ((uint64_t)hB << 32) | hA; hudCmdF[k] = hF; hudCmdS[k] = hS;
        }
        std::memcpy(segments, savedSeg, sizeof savedSeg);
    }

    uint32_t convertDl(uint32_t addr, int depth) {
        auto it = dlCache.find(addr);
        if (it != dlCache.end()) {
            usedSegs |= it->second.segs;
            if (hOn && it->second.hflags == 0) dlCache.erase(it); // se convirtio fuera del tramo, sin llave: otra vez
            else {
                if (hOn) {
                    if ((it->second.hflags & 3) != 1) hBad = true, hWhy |= 1u << 7;
                    hF |= it->second.hflags & 28; hS |= it->second.hsets;
                    hMix((uint32_t)(it->second.hash >> 32)); hMix((uint32_t)it->second.hash);
                }
                return it->second.out;
            }
        }
        uint16_t outerSegs = usedSegs;
        usedSegs = 0;
        const uint32_t oA = hA, oB = hB;
        const bool oBad = hBad, sub = hOn; // sub: lista llamada desde el tramo del HUD
        const uint8_t oF = hF;
        const uint16_t oS = hS;
        if (sub) { hA = 0x811C9DC5u; hB = 0x1234567u; hBad = false; hF = 0; hS = 0; }
        uint32_t r = convertDlBody(addr, depth);
        auto e = dlCache.find(addr);
        if (e != dlCache.end()) {
            e->second.segs = usedSegs;
            if (sub) { e->second.hflags = (hBad ? 2 : 1) | ((hF & 5) ? 4 : 0) | (hF & 24); e->second.hsets = hS; e->second.hash = ((uint64_t)hA << 32) | hB; }
        }
        if (sub) {
            const uint32_t sA = hA, sB = hB;
            const bool sBad = hBad;
            hA = oA; hB = oB; hBad = oBad || sBad;
            hF = oF | hF; hS = oS | hS;
            hMix(sA); hMix(sB);
        }
        usedSegs |= outerSegs;
        return r;
    }

    uint32_t convertDlBody(uint32_t addr, int depth) {
        const uint8_t* src = resolve(addr);
        if (!src || depth > 32) { errors++; return 0; }

        // longitud: hasta G_ENDDL o un G_DL sin regreso (branch)
        uint32_t n = 0;
        for (;; n++) {
            uint8_t op = src[n * 8];
            if (op == G_ENDDL) { n++; break; }
            if (op == G_DL && src[n * 8 + 1] == 1) { n++; break; }
            if (n > 200000) { errors++; return 0; }
        }
        uint32_t* out = static_cast<uint32_t*>(alloc(n * 8));
        if (!out) return 0;
        uint32_t outAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out));
        dlCache[addr] = CachedDl{ outAddr, 0, 0, 0, 0 };
        // tramo del HUD (la primera lista del cuadro con muchos rectangulos): del primer al ultimo rectangulo con
        // textura, mas el estado del RDP que prepara al primero
        uint32_t hudB = 0, hudE = 0;
        struct Ins { uint32_t pos, w0, w1; uint32_t* q; } ins[4];
        int ni = 0, nSkip = 0;
        uint32_t skipB[2] = {}, skipE[2] = {};
        if (!hOn && !hudDone && gGfxConvertHud) {
            if (offscreenCmds.empty()) {
                uint32_t first = n, last = 0, rects = 0;
                for (uint32_t i = 0; i < n; i++) {
                    const uint8_t op = src[i * 8];
                    if (op == 0xE4 || op == 0xE5) { if (first == n) first = i; last = i; rects++; }
                }
                if (rects >= 24) {
                    // empieza en el primer rectangulo que carga su propia textura (con el estado que lo prepara,
                    // sub-listas de puro estado incluidas): los anteriores dibujan con lo que dejo la escena 3D
                    auto runStart = [&](uint32_t at, bool& own) {
                        bool img = false, tile = false, load = false;
                        uint32_t b = at;
                        while (b > 0) {
                            const uint8_t op = src[(b - 1) * 8];
                            const bool rdpState = op == 0x00 || op == 0xE2 || op == 0xE3 || (op >= 0xE6 && op <= 0xE9) || (op >= 0xED && op <= 0xF5 && op != 0xF1) ||
                                                  (op >= 0xF7 && op <= 0xFD);
                            if (!rdpState && !(op == G_DL && src[(b - 1) * 8 + 1] == 0 && stateOnlyDl(resolve(be32(src + (b - 1) * 8 + 4))))) break;
                            if (op == 0xFD) img = true; else if (op == 0xF5) tile = true; else if (op == 0xF3 || op == 0xF4) load = true;
                            b--;
                        }
                        own = img && tile && load;
                        return b;
                    };
                    bool own = false;
                    hudB = runStart(first, own);
                    if (!own) {
                        uint32_t tries = 0;
                        for (uint32_t i = first + 1; i < n && tries < 12; i++) {
                            if (src[i * 8] != 0xE4 && src[i * 8] != 0xE5) continue;
                            tries++;
                            bool o2 = false;
                            const uint32_t b = runStart(i, o2);
                            if (o2) { hudB = b; break; }
                        }
                    }
                    hudE = last + 1;
                    if (hudE < n && src[hudE * 8] == 0xE1) hudE++;
                    if (hudE < n && src[hudE * 8] == 0xF1) hudE++;
                    hudDone = true;
                }
            }
        }

        uint32_t pendingHalf1 = 0;
        // en el heap: convertDlBody es recursivo y el hilo del juego tiene poca pila en el 3DS
        // uno por profundidad, reusado: casi ninguna lista trae huesos y crear/limpiar 2.5 KB por lista salia caro
        if (skinPool.size() <= (size_t)depth) skinPool.resize(depth + 1);
        if (!skinPool[depth] || skinPool[depth]->dirty) skinPool[depth].reset(new SkinRun()); // en el heap: poca pila
        SkinRun& skin = *skinPool[depth];
        std::vector<uint32_t> blockAt;
        std::vector<uint8_t> consumed;
        const bool blocks = gGfxConvertSkin && gGfxConvertMeshBlocks && !meshLike(src) && planMeshBlocks(src, n, blockAt, consumed);
        if (gGfxConvertSkin && gGfxConvertMeshBlocks && !blocks && !meshLike(src)) meshPlanFails++;
        convLog(blocks ? "lista+bloques" : "lista", addr, n, (uint32_t)depth);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t w0 = be32(src + i * 8), w1 = be32(src + i * 8 + 4);
            uint8_t op = w0 >> 24;
            cmdCount++;
            if (!offscreenCmds.empty() && offscreenCmds.count(src + i * 8)) { out[i * 2] = 0; out[i * 2 + 1] = 0; continue; }
            if (hudE && i == hudB) {
                // pasada 1: llave de cada comando del tramo (con lo que lee y sus sub-listas), sin convertir nada
                hudPass1(src, hudB, hudE, n);
                const uint32_t L = hudE - hudB;
                // edad de cada comando: cuadros seguidos sin cambiar. Lo que cambio hace poco (el cursor que parpadea,
                // una barra moviendose) se deja fuera de las capas y se sigue dibujando en cada cuadro.
                static constexpr uint8_t kStable = 4;
                if (hudAge.size() != L || hudPrevH.size() != L) hudAge.assign(L, kStable);
                else
                    for (uint32_t k = 0; k < L; k++) {
                        if (hudCmdH[k] != hudPrevH[k]) hudAge[k] = 0;
                        else if (hudAge[k] < 255) hudAge[k]++;
                    }
                if (gGfxConvertHudDbg) {
                    if (hudPrevH.size() == L) {
                        uint32_t nd = 0;
                        char b[160];
                        for (uint32_t k = 0; k < L; k++)
                            if (hudCmdH[k] != hudPrevH[k] && nd++ < 5) {
                                const uint8_t* c = src + (hudB + k) * 8;
                                std::snprintf(b, sizeof b, "[huddbg] cmd %u de %u: %08lX %08lX\n", (unsigned)k, (unsigned)L, (unsigned long)be32(c), (unsigned long)be32(c + 4));
                                host_print(b);
                            }
                        if (nd) { std::snprintf(b, sizeof b, "[huddbg] %u comandos distintos\n", (unsigned)nd); host_print(b); }
                    } else {
                        host_print("[huddbg] cambio el tamano del tramo\n");
                        char t[400]; int o = std::snprintf(t, sizeof t, "[huddbg] inicio:");
                        for (uint32_t j = 0; j < 16 && j < L && o < 380; j++) o += std::snprintf(t + o, sizeof t - o, " %08lX/%02X", (unsigned long)be32(src + (hudB + j) * 8), hudCmdF[j]);
                        std::snprintf(t + o, sizeof t - o, "\n"); host_print(t);
                    }
                }
                hudPrevH = hudCmdH;
                if (hBad) { gGfxConvertHudStats[2]++; gGfxConvertHudWhy |= hWhy; }
                else {
                    auto opAt = [&](uint32_t k) -> uint8_t { return src[(hudB + k) * 8]; };
                    // hasta 2 tramos: antes del primer comando que cambia y despues de lo que alcanza el ultimo
                    struct Seg { uint32_t b, e; } segs[2];
                    int ns = 0;
                    uint32_t v1 = L, vL = 0;
                    for (uint32_t k = 0; k < L; k++)
                        if (hudAge[k] < kStable) { if (v1 == L) v1 = k; vL = k; }
                    if (v1 == L) segs[ns++] = { 0, L };
                    else {
                        // no partir un rectangulo (E4 + E1 + F1): el interprete los lee juntos
                        if (v1 > 0 && opAt(v1) == 0xF1 && opAt(v1 - 1) == 0xE1) v1--;
                        if (v1 > 0 && opAt(v1) == 0xE1 && (opAt(v1 - 1) == 0xE4 || opAt(v1 - 1) == 0xE5)) v1--;
                        if (v1 > 0) segs[ns++] = { 0, v1 };
                        // hasta donde llega el efecto del ultimo comando que cambia: un rectangulo, solo el; un color,
                        // una textura o un modo, hasta el siguiente comando igual; cualquier otra cosa, hasta el final
                        const uint8_t op = opAt(vL);
                        uint32_t end = L;
                        if (op == 0xE4 || op == 0xE5 || op == 0xE1 || op == 0xF1) {
                            end = vL + 1;
                            while (end < L && (opAt(end) == 0xE1 || opAt(end) == 0xF1)) end++;
                        } else if ((op >= 0xF7 && op <= 0xFD) || op == 0xF0 || op == 0xF2 || op == 0xF3 || op == 0xF4 || op == 0xF5 || op == 0xEF || op == 0xED) {
                            for (uint32_t k = vL + 1; k < L; k++)
                                if (opAt(k) == op) { end = k; break; }
                        }
                        if (end < L) segs[ns++] = { end, L };
                    }
                    uint32_t* qs = static_cast<uint32_t*>(alloc(2 * 24));
                    for (int g = 0; g < ns && qs; g++) {
                        const Seg sg = segs[g];
                        uint32_t rects = 0, a = 0x811C9DC5u ^ (uint32_t)g, b = sg.e - sg.b;
                        for (uint32_t k = sg.b; k < sg.e; k++) {
                            if (opAt(k) == 0xE4 || opAt(k) == 0xE5) rects++;
                            a = (a ^ (uint32_t)hudCmdH[k]) * 0x9E3779B1u; a ^= a >> 15;
                            b = (b + (uint32_t)(hudCmdH[k] >> 32)) * 0x85EBCA77u; b ^= b >> 13;
                        }
                        if (rects < 6) continue; // no vale una capa
                        // matrices y vertices: si el tramo dibuja con vertices que ya venian cargados no se puede; si
                        // carga vertices antes de fijar sus dos matrices, depende de la matriz con que llega
                        bool mv = false, pj = false, sawVtx = false, inherits = false, badSeg = false;
                        for (uint32_t k = sg.b; k < sg.e; k++) {
                            const uint8_t f = hudCmdF[k];
                            if (opAt(k) == G_MTX) {
                                const uint32_t pm = (be32(src + (hudB + k) * 8) & 0xFF) ^ 1;
                                if (pm & 2) { if (pm & 4) pj = true; else mv = true; }
                            }
                            if ((f & 8) && !(f & 5) && !sawVtx) badSeg = true;
                            if (f & 5) {
                                if (!(mv && pj)) {
                                    if (gGfxConvertHudDbg && !inherits) {
                                        char t[200]; int o = std::snprintf(t, sizeof t, "[huddbg] hereda matriz en %u (mv %d pj %d f %02X):", (unsigned)k, mv, pj, f);
                                        for (uint32_t j = k >= 6 ? k - 6 : 0; j <= k && o < 180; j++) o += std::snprintf(t + o, sizeof t - o, " %08lX", (unsigned long)be32(src + (hudB + j) * 8));
                                        std::snprintf(t + o, sizeof t - o, "\n"); host_print(t);
                                    }
                                    inherits = true;
                                }
                                sawVtx = true;
                            }
                        }
                        if (badSeg) continue;
                        // lo que el tramo fija antes de su primer dibujo no depende de como venia el estado
                        uint32_t mask = 0;
                        for (uint32_t k = sg.b; k < sg.e; k++) {
                            if (hudCmdF[k] & (8 | 16)) break; // primer dibujo
                            mask |= hudCmdS[k];
                        }
                        mask = (mask & 0xFF) | ((mask & 0x1C00) == 0x1C00 ? 512 : 0); // 512: carga su propia textura antes de dibujar
                        if (gGfxConvertHudDbg) {
                            char t[160];
                            std::snprintf(t, sizeof t, "[huddbg] tramo %d: %u..%u de %u (cambia %u..%u) llave %08lX%08lX mascara %02lX\n", g, (unsigned)sg.b, (unsigned)sg.e, (unsigned)L,
                                          (unsigned)v1, (unsigned)vL, (unsigned long)a, (unsigned long)b, (unsigned long)mask);
                            host_print(t);
                        }
                        if (!inherits) mask |= 256;
                        uint32_t* q = qs + g * 6;
                        q[0] = 0x50534842u; q[1] = a; q[2] = b; q[3] = 0; q[4] = (uint32_t)g; q[5] = mask;
                        // si el interprete ya tiene la capa de este tramo con esta llave, no hace falta convertirlo: la marca
                        // 3 solo la copia (los comandos del tramo quedan vacios)
                        const bool ready = PsHudLayerReady3DS(g, a, b);
                        ins[ni++] = { hudB + sg.b, ready ? 0x4E000003u : 0x4E000001u, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(q)), nullptr };
                        ins[ni++] = { hudB + sg.e, 0x4E000002u, 0, q };
                        if (ready) { skipB[nSkip] = hudB + sg.b; skipE[nSkip] = hudB + sg.e; nSkip++; gGfxConvertHudStats[3]++; }
                    }
                    if (!ni) gGfxConvertHudStats[2]++;
                }
                if (nSkip && skin.active) { skinFlush(skin, out); skin.reset(); }
            }
            if (op == G_SETTIMG) { hTimg = resolve(w1); hTimgSiz = (w0 >> 19) & 3; hTimgW = (w0 & 0xFFF) + 1; }
            if (nSkip && i >= hudB && i < hudE && ((i >= skipB[0] && i < skipE[0]) || (nSkip > 1 && i >= skipB[1] && i < skipE[1]))) {
                if (op == G_SETTIMG && hTimg) textures.push_back(hTimg);
                if (!(op == G_MOVEWORD && ((w0 >> 16) & 0xFF) == G_MW_SEGMENT)) { out[i * 2] = 0; out[i * 2 + 1] = 0; continue; }
            }
            if (gGfxConvertSkin) {
                // hueso: G_MTX de modelview (cargar) seguido de una malla
                bool boneMtx = op == G_MTX && ((w0 & 0xFF) ^ 1) == 2 && i + 1 < n && src[(i + 1) * 8] == G_DL &&
                               src[(i + 1) * 8 + 1] == 0 && meshLike(resolve(be32(src + (i + 1) * 8 + 4)));
                if (boneMtx) {
                    convLog("hueso", w1, i, be32(src + (i + 1) * 8 + 4));
                    const uint8_t* m = resolve(w1);
                    uint32_t conv = m ? convertWords(m, 64) : 0;
                    skin.dirty = true;
                    if (!conv || skin.bones.size() >= 250) { skinFlush(skin, out); skin.reset(); out[i * 2] = w0; out[i * 2 + 1] = conv; continue; }
                    skin.active = true;
                    skin.bones.push_back(conv);
                    skin.curBone = (int)skin.bones.size() - 1;
                    out[i * 2] = w0; out[i * 2 + 1] = conv; // el interprete sigue llevando la matriz actual
                    continue;
                }
                if (skin.active && op == G_DL && ((w0 >> 16) & 0xFF) == 0) {
                    const uint8_t* d = resolve(w1);
                    if (skin.curBone >= 0 && meshLike(d)) {
                        convLog("malla al hueso", w1, i, (uint32_t)skin.curBone);
                        if (gModelCache.enabled()) skin.toks.push_back({ skin.curBone, d });
                        else skinMesh(skin, d);
                        out[i * 2] = 0; out[i * 2 + 1] = 0;
                        skin.lastMeshSlot = (int)i;
                        continue;
                    }
                    if (stateOnlyDl(d)) skinFlush(skin, out); // texturas entre huesos: el modelo sigue
                    else { skinFlush(skin, out); skin.reset(); }
                } else if (skin.active) {
                    skinFlush(skin, out);
                    if (!stateOp(op)) skin.reset();
                }
            }
            if (blocks && consumed[i]) {
                if (blockAt[i]) { out[i * 2] = (uint32_t)G_PS_SKIN << 24; out[i * 2 + 1] = blockAt[i]; convLog("bloque", addr + i * 8, i, 0); }
                else { out[i * 2] = 0; out[i * 2 + 1] = 0; }
                continue;
            }
            switch (op) {
                case G_VTX: {
                    uint32_t count = (w0 >> 12) & 0xFF;
                    const uint8_t* v = resolve(w1);
                    w1 = v ? convertVtx(v, count) : 0;
                    break;
                }
                case G_MTX: {
                    const uint8_t* m = resolve(w1);
                    w1 = m ? convertWords(m, 64) : 0;
                    break;
                }
                case G_MOVEMEM: {
                    uint8_t index = w0 & 0xFF;
                    uint32_t size = (((w0 >> 19) & 0x1F) + 1) * 8;
                    const uint8_t* m = resolve(w1);
                    if (!m) { w1 = 0; break; }
                    if (index == G_MV_VIEWPORT) w1 = convertHalves(m, 16);
                    else if (index == G_MV_MATRIX) w1 = convertWords(m, 64);
                    else { // luces y demas: bytes
                        uint8_t* d = static_cast<uint8_t*>(alloc(size));
                        if (d) std::memcpy(d, m, size);
                        w1 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(d));
                    }
                    break;
                }
                case G_MOVEWORD: {
                    uint8_t index = (w0 >> 16) & 0xFF;
                    if (index == G_MW_SEGMENT) {
                        uint32_t seg = ((w0 & 0xFFFF) >> 2) & 0x0F;
                        // las sub-listas ya convertidas resolvieron sus direcciones con los segmentos viejos:
                        // si un segmento cambia (p. ej. textura por objeto con el mismo modelo) se invalidan
                        if (segments[seg] != w1) {
                            for (auto c = dlCache.begin(); c != dlCache.end();) {
                                if (c->second.segs & (1u << seg)) c = dlCache.erase(c); else ++c;
                            }
                        }
                        segments[seg] = w1;
                        // ya resolvimos los segmentos: el interprete no los necesita
                        w0 = 0; w1 = 0; // G_NOOP
                    }
                    break;
                }
                case G_DL: {
                    uint32_t sub = convertDl(w1, depth + 1);
                    w1 = sub;
                    if (!sub) { w0 = 0; } // si no se pudo convertir, se omite (G_NOOP)
                    break;
                }
                case G_RDPHALF_1: pendingHalf1 = i; break;
                case G_BRANCH_Z: {
                    uint32_t target = be32(src + pendingHalf1 * 8 + 4);
                    out[pendingHalf1 * 2 + 1] = convertDl(target, depth + 1);
                    break;
                }
                case 0xFC:
                    if (gGfxConvertCcW1) { w0 = 0xFC000000u | (gGfxConvertCcW0 & 0xFFFFFF); w1 = gGfxConvertCcW1; }
                    if (gGfxConvertTexel1Test) {
                        // prueba: en el ciclo 0, TEXEL1 -> TEXEL0 (a0, b0, c0 de color)
                        uint32_t a0 = (w0 >> 20) & 0xF, c0 = (w0 >> 15) & 0x1F, b0 = (w1 >> 28) & 0xF;
                        if (a0 == 2) w0 = (w0 & ~(0xFu << 20)) | (1u << 20);
                        if (c0 == 2) w0 = (w0 & ~(0x1Fu << 15)) | (1u << 15);
                        if (b0 == 2) w1 = (w1 & ~(0xFu << 28)) | (1u << 28);
                    }
                    break;
                case G_SETTIMG: case G_SETZIMG: case G_SETCIMG: {
                    const uint8_t* p = resolve(w1);
                    w1 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
                    if (op == G_SETTIMG && p) textures.push_back(p);
                    if (op == G_SETZIMG) convZImg = p;
                    if (op == G_SETCIMG && p) {
                        uint32_t w = (w0 & 0xFFF) + 1;
                        if (!convMainFb && (w == 320 || w == 640)) { convMainFb = p; convMainFbWidth = w; }
                        else if (p != convMainFb && p != convZImg) sawOffscreen = true;
                        // destino que va al GPU: 'PSFB', direccion, ancho, alto
                        auto gt = gpuTargets.find(p);
                        if (gt != gpuTargets.end()) {
                            if (uint32_t* q = static_cast<uint32_t*>(alloc(16))) {
                                q[0] = 0x50534642u;
                                q[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
                                q[2] = gt->second.first;
                                q[3] = gt->second.second;
                                w0 = static_cast<uint32_t>(G_PS_FB) << 24;
                                w1 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(q));
                                gpuFbCmds++;
                            }
                        }
                    }
                    break;
                }
                default: break;
            }
            out[i * 2] = w0;
            out[i * 2 + 1] = w1;
        }
        if (skin.active) skinFlush(skin, out);
        if (hudE && ni) {
            {
                    if (uint32_t* o2 = static_cast<uint32_t*>(alloc((n + ni) * 8))) {
                        // misma lista con las marcas: inicio (llave, capa y donde esta el fin) y fin de cada tramo
                        uint32_t from = 0;
                        for (int k = 0; k < ni; k++) {
                            std::memcpy(o2 + (from + k) * 2, out + from * 2, (ins[k].pos - from) * 8);
                            uint32_t* m = o2 + (ins[k].pos + k) * 2;
                            m[0] = ins[k].w0; m[1] = ins[k].w1;
                            if (ins[k].q) ins[k].q[3] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m));
                            from = ins[k].pos;
                        }
                        std::memcpy(o2 + (from + ni) * 2, out + from * 2, (n - from) * 8);
                        out = o2;
                        n += ni;
                        outAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(o2));
                        dlCache[addr].out = outAddr;
                        gGfxConvertHudStats[0]++;
                    }
            }
        }
        if (gGfxConvertDbgDrop) { // solo para medir: quitar.txt = 1 rectangulos, 2 modelos, 4 triangulos, 8 rellenos
            for (uint32_t i = 0; i < n; i++) {
                const uint8_t op = out[i * 2] >> 24;
                if (((gGfxConvertDbgDrop & 1) && (op == 0xE4 || op == 0xE5)) || ((gGfxConvertDbgDrop & 2) && op == G_PS_SKIN) ||
                    ((gGfxConvertDbgDrop & 4) && (op == 0x05 || op == 0x06 || op == 0x07)) || ((gGfxConvertDbgDrop & 8) && op == 0xF6))
                    out[i * 2] = out[i * 2 + 1] = 0;
            }
        }
        return outAddr;
    }
};

Converter sConv;
} // namespace
bool gGfxConvertTexel1Test = false;
int gGfxConvertOnlyGroup = -1;
bool gGfxConvertSkin = false;
bool gGfxConvertMeshBlocks = true;
bool gGfxConvertGpuFb = true; // imagenes aparte con triangulos en el GPU (modelos del VS, fotos); sinfbgpu.txt lo apaga
uint32_t gGfxConvertCcW0 = 0, gGfxConvertCcW1 = 0;
namespace {

} // namespace

void GfxConvertInit(const GfxConvertConfig& cfg, size_t arenaBytes) {
    sConv.cfg = cfg;
    sConv.arena.resize(arenaBytes);
}

static uint32_t convertOnce(uint32_t dlAddr) {
    sConv.frameSerial++;
    if ((sConv.frameSerial & 63) == 0) sConv.evictVtx();
    sConv.used = 0;
    sConv.textures.clear();
    sConv.dlCache.clear();
    sConv.usedSegs = 0;
    if (gGfxConvertHud && !sConv.hudDone) gGfxConvertHudStats[1]++;
    sConv.hudDone = false; sConv.hOn = false;
    sConv.vtxCount = sConv.cmdCount = sConv.errors = 0;
    sConv.convMainFb = sConv.convZImg = nullptr;
    sConv.convMainFbWidth = 0;
    sConv.sawOffscreen = false;
    std::memset(sConv.segments, 0, sizeof(sConv.segments));
    return sConv.convertDl(dlAddr, 0);
}

void GfxConvertMemEvent(uint32_t kind, uint32_t addr, uint32_t size) { gModelCache.onMemEvent(kind, addr, size); }
void GfxConvertModelCacheConfig(uint32_t budgetBytes, bool verify) {
    if (!budgetBytes) gModelCache.clear();
    gModelCache.configure(budgetBytes, verify);
}
mc::Stats GfxConvertModelCacheStats() { return gModelCache.takeStats(); }

uint32_t GfxConvertFrame(uint32_t dlAddr, GfxConvertStats* stats) {
    gModelCache.beginFrame();
    sConv.offscreenCmds.clear();
    sConv.gpuTargets.clear();
    sConv.soft.ResetFrame();
    uint32_t dl = convertOnce(dlAddr);
    sConv.mainFbWidth = sConv.convMainFbWidth;
    // Solo los cuadros que dibujan a una textura (menus) pagan el recorrido del RDP por software;
    // despues se convierte otra vez quitando esos comandos de la DL nativa.
    bool force = false;
#ifndef __3DS__
    force = getenv("PS_DLLOG") != nullptr || getenv("PS_ONLYGROUP") != nullptr;
#endif
    if (gGfxConvertOnlyGroup >= 0) force = true;
    if (sConv.sawOffscreen || force) {
        sConv.mainFb = nullptr;
        sConv.mainFbWidth = 0;
        std::memset(sConv.segments, 0, sizeof(sConv.segments));
        sConv.softWalk(dlAddr);
        dl = convertOnce(dlAddr);
        if (!sConv.mainFbWidth) sConv.mainFbWidth = sConv.convMainFbWidth;
    }
    if (stats) {
        stats->commands = sConv.cmdCount;
        stats->vertices = sConv.vtxCount;
        stats->errors = sConv.errors;
        stats->bytes = static_cast<uint32_t>(sConv.used);
    }
    return dl;
}

const std::vector<const uint8_t*>& GfxConvertTextures() { return sConv.textures; }
bool GfxConvertSawOffscreen() { return sConv.sawOffscreen; }
bool GfxConvertScanOffscreen(uint32_t dlAddr, uint32_t* fbWidth) { return sConv.scanOffscreen(dlAddr, fbWidth); }
uint32_t GfxConvertLastScanCount() { return sConv.scanCount; }
uint32_t GfxConvertLastScanTris() { return sConv.scanTris; }
bool GfxConvertLastScanClears() { return sConv.scanClears; }
bool GfxConvertLastScanCovers() { return sConv.scanCovers; }
uint32_t GfxConvertFramebufferWidth() { return sConv.mainFbWidth; }
void GfxConvertMeshStats(uint32_t* out) {
    for (int i = 0; i < 4; i++) out[i] = sConv.meshStats[i];
    out[4] = sConv.meshBlocks; out[5] = sConv.meshBlockTris; out[6] = sConv.meshPlanFails;
    for (auto& x : sConv.meshStats) x = 0;
    sConv.meshBlocks = sConv.meshBlockTris = sConv.meshPlanFails = 0;
}
void GfxConvertSkinStats(uint32_t* batches, uint32_t* tris) {
    *batches = sConv.skinBatches; *tris = sConv.skinTris; sConv.skinBatches = sConv.skinTris = 0;
}
void GfxConvertSoftStats(uint32_t* rects, uint32_t* fills, uint32_t* tris) {
    *rects = sConv.soft.mRects; *fills = sConv.soft.mFills; *tris = sConv.soft.mTris;
}

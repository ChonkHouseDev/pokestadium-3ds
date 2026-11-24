#include <time.h>
// Pokemon Stadium 3DS - host (little-endian): carga el juego (bloque ARM big-endian BE8), le da
// servicios del 3DS y dibuja con el interprete Fast3D de libultraship + el backend citro3d del port
// de Mario Kart 64 3DS (EstebanPdN).
//
// Archivos en la SD: sdmc:/3ds/pokestadium/baserom.z64 (US 1.0, .z64). Registro: sdmc:/3ds/pokestadium/log.txt
extern "C" void PsSkinSetMerge3DS(bool);
extern "C" void PsSkinSetGpuLit3DS(bool);
extern "C" void PsSkinSetRetain3DS(bool);
extern "C" void PsSkinSetRetainMin3DS(int);
extern "C" void PsSkinSetRetainFog3DS(bool);
extern "C" int gPsFbOrient3DS;
extern "C" unsigned long gPsFbCopies3DS;
extern "C" unsigned long gPsFbStats3DS[4];
extern "C" uint32_t gPsFbCreated3DS, gPsFbReused3DS;
extern "C" int gPsFbStage3DS;
extern "C" unsigned long gPsFbInval3DS;
extern "C" unsigned long long gPsRetainStats3DS[6];
extern "C" unsigned long long gPsRetainWhy3DS[8];
extern "C" unsigned long long gPsRetainSkip3DS[8];
extern "C" unsigned int gPsMatDbg3DS[4];
extern "C" unsigned int gPsTexDbg3DS[8];
extern "C" unsigned int gPsAtlasStats3DS[4];
extern "C" bool gPsAtlasEnabled3DS;
extern "C" bool gPsAtlasMerge3DS;
extern "C" bool gPsAtlasPointOk3DS;
extern "C" bool gPsAtlasDbg3DS;
extern "C" unsigned int gPsAtlasOnlyW3DS;
extern "C" unsigned int gPsAtlasOnlyH3DS;
extern "C" int gPsUvLog3DS;
extern "C" int gPsSkinFogLog3DS;
extern "C" unsigned int gPsBumpOp3DS[256];
extern "C" void PsSkinSetRange3DS(int, int);
extern "C" void PsSkinFrame3DS();
#include <3ds.h>
#include <stdexcept>
#include <citro3d.h>
extern "C" u32 host_flag(const char* name);
#ifdef PS_BLACKBOX
extern "C" { static void ps_capture_now(void); }
static void ps_box_fatal_begin(void);
static void ps_box_fatal_line(const char* t);
extern "C" { static void ps_box_stk_lines(void); }
extern "C" { char PsAddrTag3DS(u32 a, u32* rom); extern volatile u32 gPsBadStk3DS[6], gPsBadStkN3DS, gPsBadStkRes3DS, gPsBadStkSet3DS; }
extern "C" { extern volatile uint32_t gPsBoxCmd[4][3]; extern volatile uint32_t gPsBoxCmdN; extern volatile u32 gPsBadPtrN3DS, gPsBadPtr3DS, gPsBadPtrCmd3DS; }
#endif
extern "C" int gPsRectLog3DS;
extern "C" { extern volatile u32 gPsTriNow3DS, gPsTriLast3DS, gPsSkipDraw3DS; }
extern "C" uint32_t gPsBadRetain3DS, gPsSlot0Fallback3DS, gPsTmplFallback3DS; extern "C" uint64_t gPsRetainWhy3DS[8];
extern "C" { extern bool gPsSkinActive3DS, gPsSkinLit3DS, gPsRetainNoDraw3DS; extern void* gPsRetainCaptureDst3DS; }
extern "C" int gPsTevLog3DS; // sklog.txt: entradas del combinador (gfx_citro3d)
#include <sys/stat.h>
#include <dirent.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#include "gfx_citro3d.h"
#include "gfx_window_manager_3ds.h"
#include "fast/interpreter.h"
#include "fast/debug/GfxDebugger.h"
#include "gfx_convert.h"
#ifdef PS_BLACKBOX
#include "blackbox.h"
#endif
#include "native_geometry_3ds.hpp"
#include "blob_info.h"

// libultra define u32/s32 como int y libctru como int32_t (long en devkitARM): se renombran solo aqui
#define s32 lus_s32
#define u32 lus_u32
#define vs32 lus_vs32
#define vu32 lus_vu32
extern "C" {
#include "libultraship/libultra/gbi.h"
}
#undef s32
#undef u32
#undef vs32
#undef vu32
#include "blob_bin.h"

namespace Fast {
void GfxSetInstance(std::shared_ptr<Interpreter> interpreter);
}

#define ROM_PATH "sdmc:/3ds/pokestadium/baserom.z64"
#define LOG_PATH "sdmc:/3ds/pokestadium/log.txt"
#define ROM_SIZE 33554432
#define ROM_PATH_ROMFS "romfs:/baserom.z64"
// RomFS del .cia (o del .3dsx): se monta una sola vez. Ahi pueden venir la ROM del usuario y sus imagenes.
static bool ps_romfs(void) {
    static int ok = -1;
    if (ok < 0) ok = R_SUCCEEDED(romfsInit()) ? 1 : 0;
    return ok == 1;
}

extern "C" {
u32 __ctru_heap_size = 16 * 1024 * 1024;
u32 __ctru_linear_heap_size = 20 * 1024 * 1024;
}

static FILE* sRom;
static FILE* sLog;
static Fast::GfxWindowBackend3DS sWindow;
static Fast::GfxRenderingAPICitro3D sRenderer;

// Medicion del backend sin envolverlo (el interprete de MK64 hace static_cast al backend de citro3d):
// se copia la vtable del objeto y se cambian algunas entradas por funciones que miden y llaman a la original.
static void logf(const char* fmt, ...);
struct BackendTimes { u64 draw, upload, shader; u32 draws, uploads, shaders; };
static BackendTimes sBT;
typedef void (*DrawFn)(Fast::GfxRenderingAPI*, float*, size_t, size_t);
typedef void (*UploadFn)(Fast::GfxRenderingAPI*, const uint8_t*, uint32_t, uint32_t);
typedef Fast::ShaderProgram* (*NewShaderFn)(Fast::GfxRenderingAPI*, uint64_t, uint64_t);
static DrawFn sOrigDraw; static UploadFn sOrigUpload; static NewShaderFn sOrigNewShader;
static void wrapDraw(Fast::GfxRenderingAPI* s, float* v, size_t l, size_t t) {
    u64 a = svcGetSystemTick(); sOrigDraw(s, v, l, t); sBT.draw += svcGetSystemTick() - a; sBT.draws++;
}
static void wrapUpload(Fast::GfxRenderingAPI* s, const uint8_t* b, uint32_t w, uint32_t h) {
    u64 a = svcGetSystemTick(); sOrigUpload(s, b, w, h); sBT.upload += svcGetSystemTick() - a; sBT.uploads++;
}
static Fast::ShaderProgram* wrapNewShader(Fast::GfxRenderingAPI* s, uint64_t x, uint64_t y) {
    u64 a = svcGetSystemTick(); auto p = sOrigNewShader(s, x, y); sBT.shader += svcGetSystemTick() - a; sBT.shaders++; return p;
}
// Imagen original del N64 (4:3) centrada en los 400 px de la pantalla de arriba, con bordes negros:
// todo lo que dibuja el interprete (3D y 2D) pasa por el viewport y el scissor, se comprimen a 320 px.
typedef void (*RectFn)(Fast::GfxRenderingAPI*, int, int, int, int);
static RectFn sOrigViewport, sOrigScissor;
static inline void pillar(int& x, int& w) {
    int x0 = 40 + (x * 4) / 5, x1 = 40 + ((x + w) * 4) / 5;
    x = x0; w = x1 - x0;
}
static bool sPillarOn;
static bool sScissorRaw;
static int sLogRects; // depuracion (modo repeticion): registra viewport/scissor que llegan al backend
extern "C" bool PsFbFlushNow3DS(void);
extern "C" bool gPsClearFb3DS; // limpiarfb.txt: borrar el color en cada cuadro
static u32 sPsFbSyncFrames;
extern "C" bool gPsFbActive3DS; // dibujando en una imagen aparte (G_PS_FB): sus coordenadas ya son de esa imagen
static void wrapViewport(Fast::GfxRenderingAPI* s, int x, int y, int w, int h) {
    if (sLogRects > 0) { sLogRects--; logf("[vp] %d %d %d %d\n", x, y, w, h); }
    if (gPsFbActive3DS) { sOrigViewport(s, x, y, w, h); return; }
    if (sPillarOn) pillar(x, w);
    sOrigViewport(s, x, y, w, h);
}
static void wrapScissor(Fast::GfxRenderingAPI* s, int x, int y, int w, int h) {
    if (sLogRects > 0) { sLogRects--; logf("[sc] %d %d %d %d\n", x, y, w, h); }
    if (gPsFbActive3DS) { sOrigScissor(s, x, y, w, h); return; }
    if (sPillarOn) pillar(x, w);
    // El backend de citro3d refleja la X del scissor al dibujar a la escena (en MK64 solo se usa a pantalla
    // completa y no se nota). Stadium recorta zonas chicas (texto que se desplaza): se compensa aqui.
    if (!sScissorRaw) x = 400 - (x + w);
    sOrigScissor(s, x, y, w, h);
}

template <typename M> static int vslot(M m) { struct { u32 ptr, adj; } p; memcpy(&p, &m, 8); return (p.adj & 1) ? (int)(p.ptr / 4) : -1; }
static void hook_backend_timing(void) {
    void*** obj = reinterpret_cast<void***>(&sRenderer);
    static void* table[128];
    memcpy(table, (*obj) - 2, sizeof(table)); // incluye offset-to-top y typeinfo
    int d = vslot(&Fast::GfxRenderingAPI::DrawTriangles), u = vslot(&Fast::GfxRenderingAPI::UploadTexture),
        n = vslot(&Fast::GfxRenderingAPI::CreateAndLoadNewShader);
    if (d < 0 || u < 0 || n < 0 || d > 120 || u > 120 || n > 120) { logf("[b] no pude medir el backend\n"); return; }
    sOrigDraw = (DrawFn)table[2 + d]; table[2 + d] = (void*)wrapDraw;
    sOrigUpload = (UploadFn)table[2 + u]; table[2 + u] = (void*)wrapUpload;
    sOrigNewShader = (NewShaderFn)table[2 + n]; table[2 + n] = (void*)wrapNewShader;
    int vp = vslot(&Fast::GfxRenderingAPI::SetViewport), sc = vslot(&Fast::GfxRenderingAPI::SetScissor);
    const bool kPillarbox = false; // Miguel prefiere la imagen estirada a los 400 px de la 3DS
    sPillarOn = kPillarbox;
    if (vp > 0 && vp < 120 && sc > 0 && sc < 120) {
        sOrigViewport = (RectFn)table[2 + vp]; table[2 + vp] = (void*)wrapViewport;
        sOrigScissor = (RectFn)table[2 + sc]; table[2 + sc] = (void*)wrapScissor;
    }
    *obj = table + 2;
}
static std::shared_ptr<Fast::Interpreter> sInterp;
static const std::unordered_map<Mtx*, MtxF> sNoReplacements;
static u32 sFrames;
struct TexState { u32 hash; u32 frame; };
static std::unordered_map<const u8*, TexState> sTexHash; // texturas de la memoria del juego que se vigilan
static u32 sTexFaults; // cuadros que no cupieron en la cache de texturas
extern "C" uint32_t gPsFenceWhy3DS[4]; // esperas al GPU para reusar texturas: ok, sin cuadro, sin destino, fallo
static u32 sFreed[8][2]; static volatile u32 sFreedN; static u32 sFlushes; // rangos grandes que solto el juego (cambio de modo) // limpiezas completas al cambiar de modo
static char sTexFaultWhy[48]; // el ultimo: por que, cuantas texturas y en que cuadro (caja negra)
static u32 sGpuFaults; // cuadros que no cupieron en el bufer de comandos del GPU
static u64 sTAudioHle;
static u32 sAudioUnder, sAudioDrops2, sAudioFreqSeen;
static u64 sAudioSamplesRep;
static u32 sExcTraps, sExcLowRam;
static volatile bool sWatchQuit;
// HOME / suspension: mientras la app esta en segundo plano el juego no dibuja; los vigilantes no deben contarlo
// como bloqueo ni tocar la pantalla. sAptExiting: el sistema pidio cerrar (cerrar desde el menu HOME).
static volatile bool sAptPaused, sAptExiting;
static aptHookCookie sAptCookie;
static const char* sExitReason = "el juego termino";

// ---------------- compilacion de diagnostico (PS_DIAG=1 build_3ds.sh) ----------------
// Verificacion de la cache de modelos siempre encendida, registro con bufer en memoria que se vacia a la SD cada
// pocos segundos (no en cada linea) y resumen final. La compilacion normal no cambia: sigue igual que antes.
#ifdef PS_DIAG
static constexpr bool kDiag = true;
#else
static constexpr bool kDiag = false;
#endif
#ifndef PS_BUILD_REV
#define PS_BUILD_REV "?"
#endif
static constexpr u64 kDiagFlushMs = 5000;
static u64 sDiagLastFlush;
struct DiagTotals {
    u64 verifyOk, verifyBad, hits, misses, inputMisses, baked, invalidated, evicted, idle, allocFails, budgetFails;
    u32 peakKB, minLinearKB = 0xFFFFFFFFu, windows, slowWindows;
    u64 ms, frames, drawn, convMs, worstWindowMs, renderMs;
    u32 worstConvMs, gpuPeakKB;
    u64 gpuBuilt, gpuRebuilt, gpuAllocFails;
    u64 f2[16];
} static sDiag;
static bool sDiagSummaryDone;
extern "C" uint64_t gPsResStats3DS[16];
extern "C" uint64_t gPsResBackend3DS[4];
extern "C" uint32_t gPsHudStats3DS[8];
extern "C" bool gPsHudDbg3DS;
static uint32_t sHudTot[4];
extern "C" { static void intro_summary(void); static void snd_line(const char* tag); }
static u32 sAudPads, sAudPadSamples, sAudPadTot[2]; // silencios para rehacer la reserva (veces, muestras)
static u32 sLatTot[3]; // retraso del sonido: suma de promedios por ventana, ventanas, maximo (ms)
static u32 sBurst[2]; // rafagas (ver burst_note)
static u32 sDrawBurst[2]; // acelerones vistos: 5 cuadros DIBUJADOS en menos de 100 ms (lo normal 133); [0] total, [1] en el intro
static s32 sIntroFollow[2]; // intro: lo que la imagen se adelanto y lo que espero para seguir a la musica (ms)
// Cuadros de atraso que el juego puede recuperar corriendo de mas (igual que ps_catchup_frames del juego):
// 3 normal, 12 con la musica del intro sonando, alcance.txt = N para comparar
static u32 host_catchup_frames() {
    static int sN = -1;
    if (sN < 0) { const u32 v = host_flag("alcance"); sN = v == 0xFFFFFFFFu ? 3 : (int)v; if (sN < 1) sN = 1; if (sN > 30) sN = 30; }
    if (BLOB_INTRO && __builtin_bswap32(*(const volatile u32*)BLOB_INTRO) == 1 && sN < 12) return 12;
    return (u32)sN;
}
static u32 sAudTot[4], sSpeedTot[3]; // audio: cortes, tirados, suma de %, ventanas; velocidad: ventanas rapidas, lentas, la mas corta (ms)
extern "C" bool gPsResVerify3DS;

// ---------------- seguimiento de bloqueos ----------------
// Cada servicio del host anota en que etapa esta el hilo principal (una escritura a memoria, sin E/S). El vigilante
// del nucleo 1 lo lee: si el juego deja de dibujar dice donde se quedo cada hilo aunque el principal no responda.
enum MainStage : u32 {
    ST_INIT, ST_GAME, ST_PUMP, ST_PAD, ST_SLEEP, ST_GFX, ST_GFX_CONV, ST_GFX_GPUWAIT, ST_GFX_RUN, ST_GFX_END, ST_GFX_FB,
    ST_AUD_PUSH, ST_AUD_SUBMIT, ST_AUD_POLL, ST_AUD_PENDING, ST_ROM, ST_RES, ST_SAVE, ST_FLAG, ST_MEMEV, ST_PRINT, ST_LOG_OUT,
    ST_EXIT, ST_COUNT
};
static const char* const kStageName[ST_COUNT] = {
    "inicio", "juego (codigo del N64)", "pump/aptMainLoop", "control", "sleep", "graficos", "graficos: convertir",
    "graficos: esperar GPU (StartFrame)", "graficos: interprete", "graficos: EndFrame", "graficos: copia de imagenes",
    "audio: entregar a NDSP", "audio: mandar tarea al nucleo 1", "audio: estado de la tarea", "audio: pendiente",
    "ROM", "assets", "partida", "bandera", "aviso de memoria", "print", "registro: stdout (3dslink)", "saliendo" };
static volatile u32 sStMain = ST_INIT, sStMainTick, sStMainSeq;
static volatile u32 sStAud, sStAudTick, sStAudTasks; // nucleo 1: 0 esperando tarea, 1 sintetizando
static volatile u32 sHostOtherThread;                // servicios del host llamados desde un hilo que no es el principal
static volatile u32 sBeatHi, sBeatLo;                // latidos en el nucleo 0 (prioridad alta y mas baja que el juego)
// el hilo principal no se creo con threadCreate: threadGetCurrent() es nullptr solo ahi
static inline bool on_main_thread() { return threadGetCurrent() == nullptr; }
// reparto del tiempo del hilo principal: por etapa del host y, dentro del juego, por hilo del N64
static u64 sAccTick, sAccStage[ST_COUNT], sAccThr[32];
static u32 sAccCurThr;
static inline void stage_set(u32 st) {
    const u64 tk = svcGetSystemTick();
    if (sStMain == ST_GAME) sAccThr[sAccCurThr & 31] += tk - sAccTick;
    else sAccStage[sStMain < ST_COUNT ? sStMain : 0] += tk - sAccTick;
    sAccTick = tk;
    sStMain = st;
    sStMainTick = (u32)(tk >> 18); // ~1 ms (2^18 tics = 0.98 ms), sin division
    sStMainSeq++;
}
struct StageScope {
    bool main;
    explicit StageScope(u32 st) : main(on_main_thread()) {
        if (main) stage_set(st);
        else sHostOtherThread++;
    }
    ~StageScope() { if (main) stage_set(ST_GAME); }
};
#define STAGE(st) StageScope stageScope_(st)

// Diagnostico: el registro va a un bufer en RAM; el vigilante (nucleo 1) lo escribe en la SD cada 5 s. El hilo
// principal nunca toca la SD (ni se queda esperando por ella) mientras corre el juego.
static RecursiveLock sLogLock, sFileLock;
static char sLogRing[128 * 1024], sLogOut[128 * 1024];
static u32 sLogLen, sLogDropped;
static volatile bool sLogNoStdout; // el vigilante lo enciende al ver un bloqueo: stdout (red) puede ser lo trabado
// Sin PS_LOGS=1 no hay registro de ningun tipo: ni archivo en la SD, ni salida a la terminal de 3dslink, ni red al
// salir, ni reportes en pantalla. Solo queda la caja negra (PS_BLACKBOX=1), que escribe directo a la pantalla de abajo.
#ifdef PS_LOGS
static constexpr bool kLogs = true;
// la repeticion convierte en el hilo principal (no en el del juego): con listas de 32 saltos (Articuno) la pila
// de 32 KB no alcanzaba
extern "C" { u32 __stacksize__ = 512 * 1024; }
#else
static constexpr bool kLogs = false;
#endif
static void log_store(const char* b) {
    if (!kLogs) return;
    const u32 n = (u32)strlen(b);
    RecursiveLock_Lock(&sLogLock);
    if (sLogLen + n <= sizeof sLogRing) { memcpy(sLogRing + sLogLen, b, n); sLogLen += n; }
    else sLogDropped += n;
    RecursiveLock_Unlock(&sLogLock);
}
static void log_write_out() {
    if (!sLog) return;
    RecursiveLock_Lock(&sFileLock);
    RecursiveLock_Lock(&sLogLock);
    const u32 n = sLogLen, d = sLogDropped;
    memcpy(sLogOut, sLogRing, n);
    sLogLen = 0;
    sLogDropped = 0;
    RecursiveLock_Unlock(&sLogLock);
    if (n) fwrite(sLogOut, 1, n, sLog);
    if (d) fprintf(sLog, "[log] %lu bytes perdidos (bufer lleno)\n", (unsigned long)d);
    fflush(sLog);
    RecursiveLock_Unlock(&sFileLock);
}

// Ultimas lineas del registro: se muestran en la pantalla de abajo si el juego se cierra
#define LOG_RING 256
static char sRing[LOG_RING][41];
static int sRingPos, sRingCount;
static void ring_add(const char* s) {
    static char cur[41];
    static int len;
    for (; *s; s++) {
        if (*s == '\n' || len == 40) {
            cur[len] = 0;
            memcpy(sRing[sRingPos], cur, len + 1);
            sRingPos = (sRingPos + 1) % LOG_RING;
            if (sRingCount < LOG_RING) sRingCount++;
            len = 0;
            if (*s == '\n') continue;
        }
        cur[len++] = *s;
    }
}

static int sLinkSock = -1;
static void link_diag_init(void) {
    if (!kLogs) return;
    if (!__3dslink_host.s_addr) return;
    void* b = memalign(0x1000, 0x80000);
    if (!b) return;
    if (R_FAILED(socInit((u32*)b, 0x80000))) { free(b); return; }
    sLinkSock = link3dsConnectToHost(false, false);
}
static void logf(const char* fmt, ...) {
    if (!kLogs) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ring_add(buf);
    // lanzado con "3dslink -s": lo del cierre ([apt], [host]) se ve tambien en la terminal de la PC
    if (sLinkSock >= 0 && (!strncmp(buf, "[apt]", 5) || !strncmp(buf, "[host]", 6))) send(sLinkSock, buf, strlen(buf), 0);
    {
        log_store(buf); // las dos compilaciones: el vigilante del nucleo 1 escribe el registro en la SD
        if (!sLogNoStdout) {
            const bool main = on_main_thread();
            const u32 prev = sStMain;
            if (main) stage_set(ST_LOG_OUT);
            fputs(buf, stdout); // via 3dslink -s llega a la terminal de la PC
            if (main) stage_set(prev);
        }
        return;
    }
    fputs(buf, stdout); // via 3dslink -s llega a la terminal de la PC
    if (sLog) { fputs(buf, sLog); fflush(sLog); }
}
// diagnostico: la escritura periodica la hace el vigilante; aqui solo la forzada (salida, excepcion, abort)
static void diag_flush(bool force) {
    if (!sLog || !force) return;
    sDiagLastFlush = svcGetSystemTick();
    log_write_out();
}
// resumen al terminar (salida normal, excepcion, atorado o abort); solo una vez
static bool sBlobReady; // el resumen lee memoria del juego: antes de reservarla (ROM que falta, etc.) no hay nada que leer
static void diag_summary(const char* reason) {
    if (sDiagSummaryDone || !sBlobReady) return;
    sDiagSummaryDone = true;
    const DiagTotals& d = sDiag;
    if (!kDiag) {
        // compilacion normal: solo rendimiento (para comparar F1 contra F2 en la consola)
        logf("\n======== [rend] RESUMEN ========\n");
        logf("[rend] salida: %s, ruta %s, %lu cuadros, %lu s medidos\n", reason, gGfxConvertF2 ? "F2" : "F1", (unsigned long)d.frames, (unsigned long)(d.ms / 1000));
        logf("[rend] FPS promedio: %lu.%lu dibujados (%lu.%lu del juego); ventana mas lenta %lu ms/60f\n",
             (unsigned long)(d.ms ? d.drawn * 1000 / d.ms : 0), (unsigned long)(d.ms ? d.drawn * 10000 / d.ms % 10 : 0),
             (unsigned long)(d.ms ? d.frames * 1000 / d.ms : 0), (unsigned long)(d.ms ? d.frames * 10000 / d.ms % 10 : 0), (unsigned long)d.worstWindowMs);
        logf("[rend] por cada 60 cuadros: conversion %lu ms, dibujo %lu ms\n", (unsigned long)(d.windows ? d.convMs / d.windows : 0),
             (unsigned long)(d.windows ? d.renderMs / d.windows : 0));
        logf("[rend] F2: %lu lotes, %lu dibujos residentes, %lu por el camino anterior; copias %lu, max %lu KB\n", (unsigned long)d.f2[0],
             (unsigned long)d.f2[14], (unsigned long)d.f2[13], (unsigned long)d.gpuBuilt, (unsigned long)d.gpuPeakKB);
        logf("[rend] capa del HUD %s: %lu cuadros copiados, %lu capturas, %lu dibujados completos, %lu cortadas por z\n", gGfxConvertHud ? "activa" : "apagada",
             (unsigned long)sHudTot[0], (unsigned long)sHudTot[1], (unsigned long)sHudTot[2], (unsigned long)sHudTot[3]);
        logf("[rend] audio: %lu cortes, %lu tirados, %lu silencios (%lu ms)\n", (unsigned long)sAudTot[0], (unsigned long)sAudTot[1],
             (unsigned long)sAudPadTot[0], (unsigned long)sAudPadTot[1]);
    if (BLOB_AUD_SYNC) {
        auto bl = [](u32 x) { return __builtin_bswap32(*(const volatile u32*)x); };
        const u32 fq = bl(BLOB_AUD_SYNC + 20) ? bl(BLOB_AUD_SYNC + 20) : 32000;
        logf("[rend] sincronia: reloj del juego %lu ms, sonido armado %lu ms; sin cuadros %lu ms, ajuste %ld ms, tirado %lu ms\n",
             (unsigned long)((u64)bl(BLOB_AUD_SYNC) * 1000 / 60), (unsigned long)((u64)bl(BLOB_AUD_SYNC + 4) * 1000 / fq),
             (unsigned long)((u64)bl(BLOB_AUD_SYNC + 8) * 1000 / fq), (long)((s64)(s32)bl(BLOB_AUD_SYNC + 16) * 1000 / (s32)fq),
             (unsigned long)((u64)bl(BLOB_AUD_SYNC + 12) * 1000 / fq));
        logf("[rend] retraso del sonido contra la imagen: %lu ms en promedio, %lu ms maximo; descartado %lu ms\n",
             (unsigned long)(sLatTot[1] ? sLatTot[0] / sLatTot[1] : 0), (unsigned long)sLatTot[2], (unsigned long)((u64)bl(BLOB_AUD_SYNC + 36) * 1000 / fq));
    }
        logf("[rend] velocidad: %lu ventanas rapidas (>105%%), %lu lentas (<90%%) de %lu; la mas rapida %lu%%\n", (unsigned long)sSpeedTot[0],
             (unsigned long)sSpeedTot[1], (unsigned long)sAudTot[3], (unsigned long)(sSpeedTot[2] ? 200000 / sSpeedTot[2] : 0));
        logf("[rend] rafagas (10 cuadros a mas de 150%%): %lu; los 10 mas rapidos %lu ms (lo normal: 333); alcance %lu cuadros\n", (unsigned long)sBurst[0],
             (unsigned long)sBurst[1], (unsigned long)host_catchup_frames());
        logf("[rend] acelerones vistos (5 cuadros dibujados en menos de 100 ms; lo normal 133): %lu, de ellos %lu en el intro\n",
             (unsigned long)sDrawBurst[0], (unsigned long)sDrawBurst[1]);
        intro_summary();
        snd_line("[snd]");
        logf("================================\n");
        diag_flush(true);
        return;
    }
    logf("\n======== [diag] RESUMEN ========\n");
    logf("[rend] audio: %lu cortes, %lu tirados, %lu silencios (%lu ms)\n", (unsigned long)sAudTot[0], (unsigned long)sAudTot[1],
         (unsigned long)sAudPadTot[0], (unsigned long)sAudPadTot[1]);
    if (BLOB_AUD_SYNC) {
        auto bl = [](u32 x) { return __builtin_bswap32(*(const volatile u32*)x); };
        const u32 fq = bl(BLOB_AUD_SYNC + 20) ? bl(BLOB_AUD_SYNC + 20) : 32000;
        logf("[rend] sincronia: reloj del juego %lu ms, sonido armado %lu ms; sin cuadros %lu ms, ajuste %ld ms, tirado %lu ms\n",
             (unsigned long)((u64)bl(BLOB_AUD_SYNC) * 1000 / 60), (unsigned long)((u64)bl(BLOB_AUD_SYNC + 4) * 1000 / fq),
             (unsigned long)((u64)bl(BLOB_AUD_SYNC + 8) * 1000 / fq), (long)((s64)(s32)bl(BLOB_AUD_SYNC + 16) * 1000 / (s32)fq),
             (unsigned long)((u64)bl(BLOB_AUD_SYNC + 12) * 1000 / fq));
        logf("[rend] retraso del sonido contra la imagen: %lu ms en promedio, %lu ms maximo; descartado %lu ms\n",
             (unsigned long)(sLatTot[1] ? sLatTot[0] / sLatTot[1] : 0), (unsigned long)sLatTot[2], (unsigned long)((u64)bl(BLOB_AUD_SYNC + 36) * 1000 / fq));
    }
    logf("[rend] velocidad: %lu ventanas rapidas (>105%%), %lu lentas (<90%%) de %lu; la mas rapida %lu%%\n", (unsigned long)sSpeedTot[0],
         (unsigned long)sSpeedTot[1], (unsigned long)sAudTot[3], (unsigned long)(sSpeedTot[2] ? 200000 / sSpeedTot[2] : 0));
    logf("[rend] rafagas (10 cuadros a mas de 150%%): %lu; los 10 mas rapidos %lu ms (lo normal: 333); alcance %lu cuadros\n", (unsigned long)sBurst[0],
         (unsigned long)sBurst[1], (unsigned long)host_catchup_frames());
    logf("[rend] acelerones vistos (5 cuadros dibujados en menos de 100 ms; lo normal 133): %lu, de ellos %lu en el intro\n",
         (unsigned long)sDrawBurst[0], (unsigned long)sDrawBurst[1]);
    logf("[diag] salida: %s, %lu cuadros, %lu s medidos\n", reason, (unsigned long)d.frames, (unsigned long)(d.ms / 1000));
    logf("[diag] FPS promedio: %lu.%lu dibujados (%lu.%lu del juego); ventana mas lenta %lu ms/60f; ventanas lentas (>4 s) %lu de %lu\n",
         (unsigned long)(d.ms ? d.drawn * 1000 / d.ms : 0), (unsigned long)(d.ms ? d.drawn * 10000 / d.ms % 10 : 0),
         (unsigned long)(d.ms ? d.frames * 1000 / d.ms : 0), (unsigned long)(d.ms ? d.frames * 10000 / d.ms % 10 : 0),
         (unsigned long)d.worstWindowMs, (unsigned long)d.slowWindows, (unsigned long)d.windows);
    logf("[diag] conversion: %lu ms por cada 60 cuadros en promedio, peor %lu ms\n", (unsigned long)(d.windows ? d.convMs / d.windows : 0),
         (unsigned long)d.worstConvMs);
    logf("[diag] cache: %lu aciertos %lu fallos (%lu%% aciertos), %lu horneados, %lu invalidados, %lu sacados, %lu ociosos, %lu entradas distintas\n",
         (unsigned long)d.hits, (unsigned long)d.misses, (unsigned long)(d.hits + d.misses ? d.hits * 100 / (d.hits + d.misses) : 0),
         (unsigned long)d.baked, (unsigned long)d.invalidated, (unsigned long)d.evicted, (unsigned long)d.idle, (unsigned long)d.inputMisses);
    logf("[diag] memoria: cache max %lu KB; memoria lineal libre minima %lu KB; errores de asignacion %lu, sin presupuesto %lu\n",
         (unsigned long)d.peakKB, (unsigned long)(d.minLinearKB == 0xFFFFFFFFu ? 0 : d.minLinearKB), (unsigned long)d.allocFails,
         (unsigned long)d.budgetFails);
    logf("[diag] dibujo: %lu ms por cada 60 cuadros en promedio\n", (unsigned long)(d.windows ? d.renderMs / d.windows : 0));
    logf("[diag] F2 (ruta %s): %lu lotes residentes, %lu dibujos, %lu lotes por el camino anterior; copias %lu (rehechas %lu, sin memoria %lu), max %lu KB\n",
         gGfxConvertF2 ? "F2" : "F1", (unsigned long)d.f2[0], (unsigned long)d.f2[14], (unsigned long)d.f2[13], (unsigned long)d.gpuBuilt,
         (unsigned long)d.gpuRebuilt, (unsigned long)d.gpuAllocFails, (unsigned long)d.gpuPeakKB);
    logf("[diag] verificacion ModelCache: %lu iguales %lu distintos\n", (unsigned long)d.verifyOk, (unsigned long)d.verifyBad);
    logf("[diag] verificacion F2: %lu vertices iguales %lu distintos\n", (unsigned long)d.f2[10], (unsigned long)d.f2[11]);
    if (d.verifyBad || d.f2[11])
        logf("[diag] RESULTADO: FALLO - %lu de ModelCache y %lu de F2 distintos (ver lineas \"verificacion fallo\")\n", (unsigned long)d.verifyBad, (unsigned long)d.f2[11]);
    else if (!d.verifyOk) logf("[diag] RESULTADO: SIN DATOS - no hubo comparaciones (no se llego a ver modelos)\n");
    else if (gGfxConvertF2 && !d.f2[10]) logf("[diag] RESULTADO: SIN DATOS DE F2 - ModelCache OK pero ningun lote fue por F2\n");
    else logf("[diag] RESULTADO: OK - todas las comparaciones fueron iguales\n");
    intro_summary();
    snd_line("[snd]");
    logf("================================\n");
    diag_flush(true);
}

extern "C" void PsLog3DS(const char* fmt, ...) {
    if (!kLogs) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logf("%s", buf);
}

// el GPU no termino en 5 s (frame_begin_3ds.hpp sale del programa despues de esto)
extern "C" void Mk64Gpu3DSFenceFailure(const char* why) { logf("[gpu] se trabo: %s\n", why ? why : "?"); }
static inline u32 be32(const void* p) { u32 v; memcpy(&v, p, 4); return __builtin_bswap32(v); }

// ---------------- servicios para el juego (se llaman via trampolin, en modo LE) ----------------
extern "C" {
void host_print(const char* s) { STAGE(ST_PRINT); logf("%s", s); }
u32 host_ticks_lo(void) { return (u32)svcGetSystemTick(); }
u32 host_ticks_hi(void) { return (u32)(svcGetSystemTick() >> 32); }
void host_sleep_us(u32 us) { STAGE(ST_SLEEP); svcSleepThread((s64)us * 1000); }

// Cache de la ROM por bloques: el audio del juego lee muestras de la ROM en pedacitos cada cuadro
// (en el N64 el cartucho es instantaneo); con fseek/fread a la SD eso se comia casi todo el CPU.
#define ROMC_BLOCK 0x4000u          // 16 KB
#define ROMC_COUNT 320u             // 5 MB
struct RomBlock { u32 tag; u32 used; };
static u8* sRomCache;
static RomBlock sRomTags[ROMC_COUNT];
static u32 sRomTick, sRomHits, sRomMiss;
static u64 sTRom;
static u64 sPkRom; // total sin reiniciar (detector de picos)
static bool rom_raw(u32 off, void* dst, u32 size) {
    return sRom && fseek(sRom, off, SEEK_SET) == 0 && fread(dst, 1, size, sRom) == size;
}
static const u8* rom_block(u32 blk) {
    u32 victim = 0, oldest = ~0u;
    for (u32 i = 0; i < ROMC_COUNT; i++) {
        if (sRomTags[i].tag == blk + 1) { sRomTags[i].used = ++sRomTick; sRomHits++; return sRomCache + i * ROMC_BLOCK; }
        if (sRomTags[i].used < oldest) { oldest = sRomTags[i].used; victim = i; }
    }
    sRomMiss++;
    u8* p = sRomCache + victim * ROMC_BLOCK;
    if (!rom_raw(blk * ROMC_BLOCK, p, ROMC_BLOCK)) { sRomTags[victim].tag = 0; return nullptr; }
    sRomTags[victim].tag = blk + 1;
    sRomTags[victim].used = ++sRomTick;
    return p;
}
// Segunda ROM (opcional): Pokemon Stadium japones de 1998, para la entrada de batalla original. La pone el usuario
// en la SD; si no esta, todo sigue igual. Se pide con el bit 30 de la direccion (PS_ROM_JP en port/ps_jp.c).
#define ROM_JP_PATH "sdmc:/3ds/pokestadium/baserom_jp.z64"
#define ROM_JP_BIT 0x40000000u
static FILE* sRomJp;
static int sRomJpState; // 0 sin intentar, 1 abierta, -1 no esta
static u32 rom_jp_read(u32 off, void* dst, u32 size) {
    if (!sRomJpState) {
        sRomJp = ps_romfs() ? fopen("romfs:/baserom_jp.z64", "rb") : nullptr; // dentro del .cia (lo arma el usuario con su ROM)
        if (!sRomJp) sRomJp = fopen(ROM_JP_PATH, "rb");
        sRomJpState = sRomJp ? 1 : -1;
        if (sRomJp) { fseek(sRomJp, 0, SEEK_END); const long n = ftell(sRomJp); logf("ROM japonesa: %s (%ld MB)\n", ROM_JP_PATH, n >> 20); if (n != 0x1000000) { fclose(sRomJp); sRomJp = nullptr; sRomJpState = -1; logf("ROM japonesa: no mide 16 MB, se ignora\n"); } }
        else logf("ROM japonesa: no esta (%s)\n", ROM_JP_PATH);
    }
    if (!sRomJp || off + size > 0x1000000u) return 0;
    if (fseek(sRomJp, off, SEEK_SET) != 0) return 0;
    return (u32)fread(dst, 1, size, sRomJp);
}
// Paquete de idioma (lang_xx.bin, lo arma el usuario): dentro del .cia/.3dsx o en la SD. Lo elige la pantalla Profile.
#define ROM_LANG_BIT 0x20000000u
static FILE* sLangF;
static __attribute__((unused)) bool lang_open(const char* code) { // "" = ingles (sin paquete)
    if (sLangF) { fclose(sLangF); sLangF = nullptr; }
    if (!code || !code[0]) return true;
    char p[96];
    snprintf(p, sizeof p, "romfs:/lang_%s.bin", code);
    sLangF = ps_romfs() ? fopen(p, "rb") : nullptr;
    if (!sLangF) { snprintf(p, sizeof p, "sdmc:/3ds/pokestadium/lang_%s.bin", code); sLangF = fopen(p, "rb"); }
    return sLangF != nullptr;
}
static u32 lang_read(u32 off, void* dst, u32 size) {
    if (!sLangF || fseek(sLangF, off, SEEK_SET) != 0) return 0;
    return (u32)fread(dst, 1, size, sLangF);
}
u32 host_rom_read(u32 off, void* dst, u32 size) {
    if (off & ROM_JP_BIT) return rom_jp_read(off & ~ROM_JP_BIT, dst, size);
    if (off & ROM_LANG_BIT) return lang_read(off & ~ROM_LANG_BIT, dst, size);
    u64 t = svcGetSystemTick();
    u32 done = 0;
    if (!sRomCache && size <= 0x10000) { sRomCache = (u8*)malloc(ROMC_BLOCK * ROMC_COUNT); }
    if (sRomCache && size <= 0x10000) {
        while (done < size) {
            u32 a = off + done, blk = a / ROMC_BLOCK, in = a % ROMC_BLOCK;
            u32 n = ROMC_BLOCK - in;
            if (n > size - done) n = size - done;
            const u8* b = rom_block(blk);
            if (!b) break;
            memcpy((u8*)dst + done, b + in, n);
            done += n;
        }
    } else if (rom_raw(off, dst, size)) {
        done = size;
    }
    { const u64 d = svcGetSystemTick() - t; sTRom += d; sPkRom += d; }
    return done;
}
// el juego avisa que un rango de su RAM cambio (ps_mem_event); la lectura por PI la avisa el host
void host_mem_event(u32 kind, u32 addr, u32 size) {
    if (kind == 9) { // cambio de hilo del N64 (addr = id): solo para el reparto de tiempo
        if (on_main_thread()) { stage_set(ST_GAME); sAccCurThr = addr; }
        return;
    }
    STAGE(ST_MEMEV);
    GfxConvertMemEvent(kind, addr, size);
    // el juego solto un pedazo grande de su memoria (salir de un modo, empezar o terminar una pelea): todo lo que el
    // port tenia guardado de lo que habia ahi (modelos horneados y texturas) se tira completo en el siguiente cuadro.
    // Al volver se carga de nuevo lo que se vaya viendo. Abrir/cerrar la ficha o que salga un Pokemon nuevo suelta menos de 2 MB y no entra aqui.
    if (kind == 5 && size >= 2048u * 1024u && sFreedN < 8) { sFreed[sFreedN][0] = addr; sFreed[sFreedN][1] = addr + size; sFreedN++; }
}
u32 host_rom_read_game(u32 off, void* dst, u32 size) {
    STAGE(ST_ROM);
    u32 got = host_rom_read(off, dst, size);
    host_mem_event(8, (u32)(uintptr_t)dst, size);
    return got;
}

// ---------------- verificacion del intro ----------------
// En el intro los Pokemon brincan al ritmo de la musica: ahi se ve cualquier desfase. El juego avisa (gPsIntro,
// 3D140.c) cuando empieza el intro y cuando arranca la musica; aqui se mide cada ~2 s cuanto va la musica que se
// oye contra la imagen, y con la cruz (izquierda/derecha) se puede adelantar o atrasar la musica 34 ms por toque
// para encontrar a ojo cuanto falta. Todo queda en el registro y en el resumen.
static u64 sIntroT0;          // cuando arranco la musica (ticks)
static s32 sIntroNudges;      // toques de la cruz (negativo: musica adelantada)
static s32 sIntroDes[4];      // desfase musica - imagen (ms): suma, ventanas, minimo, maximo
static u32 sIntroLast[4];     // ultima medicion: real ms, imagen ms, musica ms, reserva ms
static bool sIntroSeen, sIntroDone;
static u32 sIntroAuto[2];     // cuadros de adelanto con que se mando la musica, toques del ajuste automatico
static u32 intro_be(u32 a) { return __builtin_bswap32(*(const volatile u32*)a); }
static void intro_summary(void) {
    if (!sIntroSeen) { logf("[intro] no se midio (no paso por el intro con musica)\n"); return; }
    logf("[intro] musica %lu ms, imagen %lu ms en %lu ms reales (velocidad %lu%%)\n", (unsigned long)sIntroLast[2], (unsigned long)sIntroLast[1],
         (unsigned long)sIntroLast[0], (unsigned long)(sIntroLast[0] ? (u64)sIntroLast[1] * 100 / sIntroLast[0] : 0));
    logf("[intro] desfase (musica - imagen): %ld ms en promedio, de %ld a %ld; a mano: musica %s %ld ms\n",
         (long)(sIntroDes[1] ? sIntroDes[0] / sIntroDes[1] : 0), (long)sIntroDes[2], (long)sIntroDes[3],
         sIntroNudges >= 0 ? "adelantada" : "atrasada", (long)((sIntroNudges < 0 ? -sIntroNudges : sIntroNudges) * 69 / 2));
    if (host_flag("sonidoreloj") == 1)
        logf("[intro] la musica se mando %lu cuadros antes; ajuste automatico: %ld toques de 34 ms (negativo adelanta)\n",
             (unsigned long)sIntroAuto[0], (long)(s32)sIntroAuto[1]);
    else
        logf("[intro] la musica se mando %lu cuadros antes; la imagen sigue a la musica: se adelanto %ld ms y espero %ld ms en total\n",
             (unsigned long)sIntroAuto[0], (long)sIntroFollow[0], (long)sIntroFollow[1]);
}
// Sonidos que no salen: cada lugar donde el juego tira algo en silencio (gPsSnd, 3D140.c)
static void snd_line(const char* tag) {
#ifdef BLOB_SND
    if (!BLOB_SND) return;
    u32 v[20];
    for (int i = 0; i < 20; i++) v[i] = intro_be(BLOB_SND + 4 * i);
    logf("%s efectos: %lu pedidos, %lu arrancados, %lu rechazados, %lu sin canal | cola llena %lu (max %lu de 1024) | voces: sin virtual %lu, sin fisica %lu, robadas %lu\n",
         tag, (unsigned long)v[11], (unsigned long)v[10], (unsigned long)v[12], (unsigned long)v[3], (unsigned long)v[2], (unsigned long)v[9],
         (unsigned long)v[4], (unsigned long)v[5], (unsigned long)v[15]);
    logf("%s cambios de voz perdidos %lu (minimo libre %lu) | muestras sin buffer %lu | cambio de banco con efectos en cola %lu | max comandos RSP %lu\n",
         tag, (unsigned long)v[0], (unsigned long)v[1], (unsigned long)v[6], (unsigned long)v[7], (unsigned long)v[8]);
    // narrador y gritos: si salen a su velocidad duran 100 % (mas = estirados o repetidos por falta de muestras)
    logf("%s voces (narrador y gritos): %lu, duracion %lu%% de la suya, se quedaron sin muestras %lu veces, nivel maximo %lu de 32767%s\n",
         tag, (unsigned long)v[18], (unsigned long)(v[16] ? (u64)v[17] * 100 / v[16] : 0), (unsigned long)v[13], (unsigned long)v[19],
         v[18] && !v[19] ? " (MUDAS)" : "");
#else
    (void)tag;
#endif
}
static void intro_keys(void) {
    if (!BLOB_INTRO) return;
    const u32 st = intro_be(BLOB_INTRO);
    if (st == 2 && sIntroT0) { sIntroT0 = 0; sIntroDone = false; } // otra vuelta del intro: se mide de nuevo
    if (st == 1 && !sIntroT0) { sIntroT0 = svcGetSystemTick(); sIntroSeen = true; sIntroNudges = 0; memset(sIntroDes, 0, sizeof sIntroDes); logf("[intro] arranco la musica. Cruz: izquierda adelanta la musica, derecha la atrasa (34 ms por toque)\n"); }
    if (st == 0) {
        if (sIntroT0 && !sIntroDone) { sIntroDone = true; logf("[intro] termino el intro\n"); intro_summary(); }
        return;
    }
    static u32 sPrevHeld; // flancos propios: otro hidScanInput entre medio se comeria los de hidKeysDown
    const u32 heldNow = hidKeysHeld(), kd = heldNow & ~sPrevHeld;
    sPrevHeld = heldNow;
    const s32 d = ((kd & KEY_DRIGHT) ? 1 : 0) - ((kd & KEY_DLEFT) ? 1 : 0);
    if (d && st == 1) {
        // un toque: se aplica ya (pendientes) y corre el objetivo del ajuste automatico (a mano), para que no lo deshaga
        for (u32 off : { 12u, 20u }) {
            volatile u32* p = (volatile u32*)(BLOB_INTRO + off);
            *p = __builtin_bswap32((u32)((s32)__builtin_bswap32(*p) + d));
        }
        sIntroNudges -= d;
        logf("[intro] ajuste a mano: musica %s %ld ms\n", sIntroNudges >= 0 ? "adelantada" : "atrasada", (long)((sIntroNudges < 0 ? -sIntroNudges : sIntroNudges) * 69 / 2));
    }
}

#if defined(PS_BLACKBOX) && !defined(PS_BOXLITE)
// Con caja negra la pantalla de abajo es solo de la caja: lo que dibuja el juego ahi (cartuchos, batalla, perfil) no
// se presenta, para que si la consola se congela se quede a la vista lo ultimo que escribio la caja.
static inline void ps_no_swap(gfxScreen_t, bool) {}
#define gfxScreenSwapBuffers ps_no_swap
#endif

// La hora del dia del juego: la del 3DS (Clock) o la que se escogio en Options (Day, Sunset, Night). La opcion vive en
// el juego (gPsTimeMode) y se guarda en la SD (hora.cfg). horaprueba.txt = hora, para probar en el emulador.
static int ps_hour_now(void) {
    static int sH = -2, sLoaded, sSaved = -1;
    if (sH == -2) { const u32 v = host_flag("horaprueba"); sH = v < 24 ? (int)v : -1; }
    int mode = 0;
    if (BLOB_TIMEMODE) {
        if (!sLoaded) {
            sLoaded = 1;
            FILE* f = fopen("sdmc:/3ds/pokestadium/hora.cfg", "rb");
            int v = 0;
            if (f) { if (fscanf(f, "%d", &v) != 1) v = 0; fclose(f); }
            if (v < 0 || v > 3) v = 0;
            *(volatile u32*)BLOB_TIMEMODE = __builtin_bswap32((u32)v);
            sSaved = v;
        }
        mode = (int)__builtin_bswap32(*(volatile u32*)BLOB_TIMEMODE);
        if (mode < 0 || mode > 3) mode = 0;
        if (mode != sSaved) {
            FILE* f = fopen("sdmc:/3ds/pokestadium/hora.cfg", "wb");
            if (f) { fprintf(f, "%d\n", mode); fclose(f); }
            sSaved = mode;
        }
    }
    if (mode == 1) return 12;
    if (mode == 2) return 18;
    if (mode == 3) return 22;
    if (sH >= 0) return sH;
    const time_t tt = time(nullptr); struct tm tmv; localtime_r(&tt, &tmv);
    return tmv.tm_hour;
}
#include "gb_saves.inc"
#if defined(PS_BLACKBOX) && !defined(PS_BOXLITE)
#undef gfxScreenSwapBuffers
#endif

u32 host_pump(void) {
    STAGE(ST_PUMP);
    gb_tick();
    gb_bat_tick();
    pf_kb_run(); // el teclado del nombre en Profile (si se pidio)
    if (!aptMainLoop()) { sExitReason = "aptMainLoop"; return 0; }
#ifdef PS_BLACKBOX
    { static int sCapAt = -2; if (sCapAt == -2) sCapAt = (int)host_flag("capturaprueba");
      if (sCapAt > 0 && (int)sFrames >= sCapAt) { sCapAt = -1; ps_capture_now(); } }
#endif
    { // rectlog.txt = cuadro: desde ahi se anotan en log.txt los siguientes 600 rectangulos con textura (solo con logs)
      static int sRectAt = -2; if (sRectAt == -2) sRectAt = (int)host_flag("rectlog");
      if (sRectAt > 0 && (int)sFrames >= sRectAt) { sRectAt = -1; gPsRectLog3DS = 600; logf("[rect] desde el cuadro %lu\n", (unsigned long)sFrames); } }
    { // fbvigia.txt = cuadro: desde ahi, 300 cuadros, se anota si la memoria de las dos imagenes del juego (640x480) cambia
      // fuera del dibujo (el juego escribiendo directo): por franjas de 40 renglones, izquierda (x<120) y derecha (x>=520)
      static int sFbAt = -2, sFbLeft = 0; static u32 sFbH[2][12][2];
      if (sFbAt == -2) sFbAt = (int)host_flag("fbvigia");
      if (sFbAt > 0 && (int)sFrames >= sFbAt) { sFbAt = -1; sFbLeft = 300; }
      if (sFbLeft > 0) {
          sFbLeft--;
          static const u32 kFb[2] = { 0x0AA7F000u, 0x0A9E8FC0u };
          for (int f = 0; f < 2; f++) for (int b = 0; b < 12; b++) for (int side = 0; side < 2; side++) {
              u32 h = 2166136261u;
              for (int y = b * 40; y < b * 40 + 40; y++) {
                  const u32* row = (const u32*)(kFb[f] + y * 1280 + (side ? 1040 : 0));
                  for (int x = 0; x < 60; x++) h = (h ^ row[x]) * 16777619u;
              }
              if (sFbH[f][b][side] && sFbH[f][b][side] != h) logf("[fbvigia] cuadro %lu imagen %d franja %d %s cambio\n", (unsigned long)sFrames, f, b, side ? "der" : "izq");
              sFbH[f][b][side] = h;
          }
      } }
    if (!sWindow.IsRunning()) { sExitReason = "ventana cerrada"; return 0; }
    hidScanInput();
    u32 held = hidKeysHeld();
    if (kDiag && (held & KEY_START) && (held & KEY_SELECT)) { sExitReason = "START+SELECT"; return 0; } // salir (solo diagnostico)
    intro_keys();
    return 1;
}

// OSContPad[4] del juego en big-endian: u16 button, s8 stick_x, s8 stick_y, u8 errno (+1 de relleno)
void host_read_pad(u8* pads) {
    STAGE(ST_PAD);
    u32 k = hidKeysHeld();
    // Prueba del cuelgue: L + R juntos y X / Y / B prenden o apagan el envio al GPU de un tipo de dibujo
    // (X: modelos con huesos, Y: geometria nativa, B: dibujos planos). Mientras L+R esten presionados el juego no
    // recibe botones. Lo que este apagado sale en el aviso de "GPU busy" si se vuelve a trabar.
    if ((k & KEY_L) && (k & KEY_R)) {
        const u32 d = hidKeysDown();
        if (d & KEY_X) gPsSkipDraw3DS ^= 1u;
        if (d & KEY_Y) gPsSkipDraw3DS ^= 2u;
        if (d & KEY_B) gPsSkipDraw3DS ^= 4u;
        k = 0;
    }
    circlePosition c;
    hidCircleRead(&c);
    u16 b = 0;
    if (k & KEY_A) b |= 0x8000;      // A
    if (k & KEY_B) b |= 0x4000;      // B
    if (k & KEY_Y) b |= 0x2000;      // Z (antes en L; C izquierda queda en el circle pad y el C-stick)
    if (k & KEY_START) b |= 0x1000;  // START
    if (k & KEY_DUP) b |= 0x0800;
    if (k & KEY_DDOWN) b |= 0x0400;
    if (k & KEY_DLEFT) b |= 0x0200;
    if (k & KEY_DRIGHT) b |= 0x0100;
    if (k & (KEY_L | KEY_ZL)) b |= 0x0020; // L
    if (k & KEY_R) b |= 0x0010;      // R
    // Botones C (amarillos): en la bolita (el Old 3DS no tiene C-stick ni ZR, asi que C derecha no tenia
    // boton). bolitastick.txt regresa la bolita al stick del N64. X/Y/SELECT siguen como C arriba/izq/abajo.
    static int sCpadStick = -1;
    if (sCpadStick < 0) sCpadStick = host_flag("bolitastick") != 0xFFFFFFFFu;
    const u32 cU = KEY_X | KEY_CSTICK_UP | (sCpadStick ? 0 : KEY_CPAD_UP);
    const u32 cD = KEY_SELECT | KEY_CSTICK_DOWN | (sCpadStick ? 0 : KEY_CPAD_DOWN);
    const u32 cL = KEY_CSTICK_LEFT | (sCpadStick ? 0 : KEY_CPAD_LEFT);
    const u32 cR = KEY_ZR | KEY_CSTICK_RIGHT | (sCpadStick ? 0 : KEY_CPAD_RIGHT);
    if (k & cU) b |= 0x0008; // C arriba
    if (k & cD) b |= 0x0004; // C abajo
    if (k & cL) b |= 0x0002; // C izquierda
    if (k & cR) b |= 0x0001; // C derecha
    b |= gb_bat_buttons(); // lo tocado en el tablero de batalla de la pantalla de abajo
    b |= gb_sel_buttons(); // y en la eleccion de Pokemon antes de la batalla
    memset(pads, 0, 6 * 4);
    pads[0] = b >> 8;
    pads[1] = b & 0xFF;
    pads[2] = sCpadStick ? (u8)(s8)(c.dx * 80 / 156) : 0;
    pads[3] = sCpadStick ? (u8)(s8)(c.dy * 80 / 156) : 0;
    // los demas controles no existen, salvo el 2 cuando su Transfer Pak tiene cartucho (sin botones)
    for (int i = 1; i < 4; i++) pads[i * 6 + 4] = (i < GB_SLOTS && sGb[i].present) ? 0 : 0x08; // CONT_NO_RESPONSE_ERROR
}

// Tarea de graficos del RSP: convertir la display list y dibujarla
// Tiempos del host (ticks del ARM11) para ver donde se va cada frame
// Rafagas: 10 cuadros seguidos del juego en menos de 222 ms (mas de 150 % del ritmo real). Es lo que se ve como
// "acelerado": el juego alcanzando al reloj despues de un atoron. [0] rafagas, [1] los 10 cuadros mas rapidos (ms)
static void burst_note(u64 now) {
    static u64 sT[10];
    static u32 sI, sN;
    static bool sIn;
    const u64 old = sT[sI];
    sT[sI] = now;
    sI = (sI + 1) % 10;
    if (sN < 10) { sN++; return; }
    const u32 ms = (u32)((now - old) * 1000 / 268111856ull);
    if (!sBurst[1] || ms < sBurst[1]) sBurst[1] = ms;
    const bool fast = ms < 222;
    if (fast && !sIn) {
        sBurst[0]++;
        static u32 sLogged;
        if (sLogged < 40) { sLogged++; logf("[rafaga] cuadro %lu: 10 cuadros en %lu ms (alcance %lu)\n", (unsigned long)sFrames, (unsigned long)ms, (unsigned long)host_catchup_frames()); }
    }
    sIn = fast;
}
static u32 sSkipped;
static u32 sSkipNo[4]; // cuadros atrasados que no se saltaron: dibujan a textura, pocos triangulos (2D), cambio de tamano sin borrar, tope de 3 seguidos
static u64 sTTexHash;
extern "C" uint64_t gPsProf[24] __attribute__((weak));
extern "C" uint64_t gPsOpTicks3DS[256] __attribute__((weak));
extern "C" uint32_t gPsOpCount3DS[256] __attribute__((weak));
// Triangulos que el interprete dibujo en el CPU por motivo: lista dinamica, luces, niebla, texgen, elegible, rect
extern "C" { uint64_t gMk64InterpretedTriangles3DS[6]; }
extern "C" uint64_t gPsSkinStats3DS[10];
extern "C" uint32_t gPsProfCount[24] __attribute__((weak));
extern "C" uint32_t gPsFlushWhy[32] __attribute__((weak));
static u64 sTConv, sTRender, sTAudio, sTRes, sTPerfStart, sTRun, sTWait;
// Picos: totales que nunca se reinician, para sacar cuanto se fue en cada etapa dentro de UN cuadro lento
static u64 sPkRes, sPkAudio, sPkHle, sPkCR; // sPkRom va junto a sTRom
static u32 sPkResN, sPkResKB, sPkResMiss;
static u32 sResCBytes, sResCHits, sResCAdds, sResCEvict; // cache de assets (ver res_get_impl)
static u32 sPkResTop[3], sPkResTopKB[3]; // los 3 assets mas grandes descomprimidos en el cuadro (offset en la ROM)
#define TICKS_MS(t) ((u32)((t) / 268111))

// Volcado de frames para el visor (tools/viewer): si existe sdmc:/3ds/pokestadium/dump.txt con
// numeros de frame separados por comas, se guarda dump_NNNNN.psfd (memoria del juego + DL) en esos frames.
static void dump_frame_if_listed(u32 dl) {
    static char list[512];
    static int init;
    if (!init) {
        init = 1;
        FILE* f = fopen("sdmc:/3ds/pokestadium/dump.txt", "rb");
        if (f) { list[0] = ','; size_t n = fread(list + 1, 1, sizeof(list) - 3, f); list[n + 1] = ','; list[n + 2] = 0; fclose(f); }
    }
    // (Antes, tocar la pantalla de abajo tambien grababa el cuadro: 14 MB a la SD, casi 3 s con el juego detenido, las
    // primeras 8 veces. Con el tablero tactil eso era "la paralisis" de los primeros toques. Quitado; lo que dejo en la
    // SD se borra al arrancar, ver dump_cleanup.)
    if (!list[0]) return;
    {
        char key[16];
        snprintf(key, sizeof key, ",%lu,", (unsigned long)(sFrames + 1));
        if (!strstr(list, key)) return;
    }
    char path[96];
    snprintf(path, sizeof path, "sdmc:/3ds/pokestadium/dump_%05lu.psfd", (unsigned long)(sFrames + 1));
    FILE* f = fopen(path, "wb");
    if (!f) return;
    u32 hdr[4] = { __builtin_bswap32(0x50534644u), __builtin_bswap32(BLOB_BASE), __builtin_bswap32(BLOB_SIZE), __builtin_bswap32(dl) };
    fwrite(hdr, 4, 4, f);
    fwrite((const void*)BLOB_BASE, 1, BLOB_SIZE, f);
    fclose(f);
    logf("[dump] frame %lu\n", (unsigned long)(sFrames + 1));
}

// Los volcados que dejaron los toques (dump_NNNNN.psfd, 14 MB cada uno) no los pidio nadie: si no hay dump.txt, se borran
static void dump_cleanup(void) {
    struct stat st;
    if (stat("sdmc:/3ds/pokestadium/dump.txt", &st) == 0) return; // hay volcados pedidos a proposito: no se toca nada
    DIR* d = opendir("sdmc:/3ds/pokestadium");
    if (!d) return;
    static char names[64][24];
    int n = 0;
    while (struct dirent* e = readdir(d)) {
        const size_t len = strlen(e->d_name);
        if (n < 64 && len >= 15 && len <= 17 && !strncmp(e->d_name, "dump_", 5) && !strcmp(e->d_name + len - 5, ".psfd")) snprintf(names[n++], sizeof names[0], "%s", e->d_name);
    }
    closedir(d);
    u32 kb = 0, gone = 0;
    for (int i = 0; i < n; i++) {
        char path[96];
        snprintf(path, sizeof path, "sdmc:/3ds/pokestadium/%s", names[i]);
        if (stat(path, &st) == 0 && remove(path) == 0) { kb += (u32)(st.st_size / 1024); gone++; }
    }
    if (gone) logf("[dump] se borraron %lu volcados que dejaron los toques en la pantalla (%lu MB)%s\n", (unsigned long)gone, (unsigned long)(kb / 1024), n == 64 ? "; quedan mas, se borran en el siguiente arranque" : "");
}

static bool sReplayMode = false; // repeticion: sin saltar cuadros (para medir igual)
bool gPsDebtDecay3DS = false; // perdonar atraso: el juego se quedaba atras del audio
extern "C" void host_dbg_line(const char* s) { logf("%s", s); }
extern const uint8_t* gGfxConvertNotScreen;
void host_gfx_task(const u8* task) {
    gGfxConvertNotScreen = BLOB_NOT_SCREEN ? (const uint8_t*)(uintptr_t)__builtin_bswap32(*(const volatile u32*)BLOB_NOT_SCREEN) : nullptr;
    STAGE(ST_GFX);
    u32 dl = be32(task + 48); // OSTask.t.data_ptr
    // Picos: si entre una tarea de graficos y la siguiente pasaron mas de 150 ms, se anota en
    // log.txt (y en la bitacora en pantalla) en que se fue ese tiempo. "juego" es lo que queda: la logica
    // del juego en el bloque y las esperas. Con eso se ve si un tiron es la SD, descomprimir, audio o dibujo.
    {
        static u64 sLast, sRom0, sRes0, sAud0, sHle0, sCR0;
        static u32 sN0, sKB0, sMiss0, sPicos;
        const u64 now = svcGetSystemTick();
        if (sLast) {
            const u64 dt = now - sLast;
            // solo picos de verdad (>150 ms) y a lo mas uno por segundo: escribir en la SD cada cuadro
            // tambien cuesta y en batalla se disparaba casi siempre (eso es lentitud pareja, no picos; ver [t])
            static u64 sLastPicoLog;
            if (dt > 150ull * 268111ull && now - sLastPicoLog > 268111ull * 1000) {
                sLastPicoLog = now;
                sPicos++;
                const u64 rom = sPkRom - sRom0, res = sPkRes - sRes0, aud = sPkAudio - sAud0, hle = sPkHle - sHle0, cr = sPkCR - sCR0;
                const u64 known = res + aud + cr; // rom va dentro de res casi siempre; hle corre en el nucleo 1
                logf("[pico] #%lu cuadro %lu: %lu ms | assets %lu ms (%lu pedidos, %lu nuevos, %lu KB) rom %lu | audio %lu | conv+dib %lu | juego %lu | hle1 %lu\n",
                     (unsigned long)sPicos, (unsigned long)sFrames, (unsigned long)TICKS_MS(dt), (unsigned long)TICKS_MS(res),
                     (unsigned long)(sPkResN - sN0), (unsigned long)(sPkResMiss - sMiss0), (unsigned long)(sPkResKB - sKB0),
                     (unsigned long)TICKS_MS(rom), (unsigned long)TICKS_MS(aud), (unsigned long)TICKS_MS(cr),
                     (unsigned long)TICKS_MS(dt > known ? dt - known : 0), (unsigned long)TICKS_MS(hle));
                if (sPkResMiss != sMiss0)
                    logf("[pico]   assets mas grandes: %07lx %luKB, %07lx %luKB, %07lx %luKB\n",
                         (unsigned long)sPkResTop[0], (unsigned long)sPkResTopKB[0], (unsigned long)sPkResTop[1],
                         (unsigned long)sPkResTopKB[1], (unsigned long)sPkResTop[2], (unsigned long)sPkResTopKB[2]);
            }
        }
        sLast = now; sRom0 = sPkRom; sRes0 = sPkRes; sAud0 = sPkAudio; sHle0 = sPkHle; sCR0 = sPkCR;
        sN0 = sPkResN; sKB0 = sPkResKB; sMiss0 = sPkResMiss;
        for (int i = 0; i < 3; i++) sPkResTop[i] = sPkResTopKB[i] = 0;
    }
    dump_frame_if_listed(dl);
    if (!sReplayMode) burst_note(svcGetSystemTick());
    // Salto de cuadros: el juego avanza a 30 cuadros por segundo (sus animaciones van por cuadro).
    // Si el 3DS va atrasado mas de un cuadro, la logica sigue pero este cuadro no se dibuja (maximo 2
    // seguidos), asi el intro y el audio van a su velocidad real aunque se vean menos cuadros.
    // Se decide antes de convertir la DL: un cuadro saltado no paga la conversion (antes ~14 ms).
    // Los cuadros que dibujan a una textura (menus) nunca se saltan; eso se ve con un recorrido rapido.
    bool skipFrame = false;
    {
        constexpr u64 kFrame = 268111ull * 100 / 3; // 33.3 ms en ticks del ARM11
        static u64 sExpected;
        static u32 sSkipRun;
        u64 now = svcGetSystemTick();
        // Una carga (mas de 1 s atrasado) reinicia el reloj. Un atraso menor se recupera saltando cuadros (hasta 3
        // seguidos), pero solo hasta 6 cuadros (0.2 s): con 1 s, al terminar una escena pesada el juego corria al
        // doble para alcanzar al reloj y se veia acelerado. Lo que no se recupera se pierde, y el sonido (que
        // sigue al reloj del juego) hace una pausa del mismo tamano.
        // (ahora el tope es de 3 cuadros fuera del intro: ps_catchup_frames, port/os/os_events.c)
        const u64 capF = host_catchup_frames();
        if (!sExpected || now > sExpected + 30 * kFrame) sExpected = now;
        else if (now > sExpected + capF * kFrame) sExpected = now - capF * kFrame;
        bool behind = now > sExpected + kFrame / 2; // mas de medio cuadro atras: este se puede saltar (antes 1: el atraso oscilaba mas)
        // con mucho atraso (3 cuadros o mas) se saltan hasta 6 seguidos: la imagen da un brinco y queda al corriente,
        // en vez de verse acelerada mientras alcanza al reloj
        u32 maxRun = now > sExpected + 3 * kFrame ? 6 : 3;
#ifdef BLOB_DEBT
        // Intro: el reloj que manda es el del juego, que ahi sigue a la musica (3D140.c, os_events.c): se adelanta o
        // espera para ir con lo que se oye, y este reloj del host no se entera. Con el atraso que trae el juego en
        // este cuadro se decide; con 3 cuadros o mas, hasta 12 seguidos sin dibujar (un brinco, no un acelerado).
        if (BLOB_DEBT && BLOB_INTRO && __builtin_bswap32(*(const volatile u32*)BLOB_INTRO) != 0) {
            const s32 debtUs = (s32)__builtin_bswap32(*(const volatile u32*)BLOB_DEBT);
            behind = debtUs > 16667;
            maxRun = debtUs > 100000 ? 12 : 3;
        }
#endif
        sExpected += kFrame;
        uint32_t scanW = 0;
        // El color se conserva entre cuadros como en el N64, y algunas pantallas (seleccion) dibujan el fondo una
        // sola vez y luego solo lo que cambia: un cuadro distinto al anterior (otra cantidad de comandos) no se salta
        static u32 sPrevScan;
        bool same = false;
        const bool offs = !sReplayMode && behind && GfxConvertScanOffscreen(dl, &scanW);
        if (behind && offs) sSkipNo[0]++;
        if (behind && !offs && sSkipRun >= maxRun) sSkipNo[3]++;
        // Al terminar la escena del VS japones no se salta ningun cuadro por un rato: la pantalla que sigue pone parte
        // de su estado de dibujo solo en sus primeros cuadros, y si justo esos se saltaban (venimos atrasados por la
        // carga) se quedaba con el estado de la escena y salia blanquecina.
        static bool sJpWas;
        static u32 sNoSkipUntil;
        const bool jpOn = BLOB_JP_ON && intro_be(BLOB_JP_ON) != 0;
        if (sJpWas && !jpOn) sNoSkipUntil = (u32)sFrames + 45;
        sJpWas = jpOn;
        if (!sReplayMode && behind && sSkipRun < maxRun && !offs && (u32)sFrames >= sNoSkipUntil) {
            const u32 n = GfxConvertLastScanCount();
            // y solo en escenas 3D (muchos triangulos): los menus 2D dibujan por partes encima de lo anterior
            // (cambian un panel en uno o dos cuadros) y ademas son ligeros, no hace falta saltarlos
            // ... salvo los que tapan toda la pantalla con rectangulos opacos (la pantalla de titulo): se dibujan completos
            // (los que tapan toda la pantalla solo si son iguales al anterior: al cerrar el panel de la renta el juego
            // repone el fondo en dos cuadros asi y luego solo dibuja la lista; si se saltaba el ultimo, el panel se quedaba)
            same = sPrevScan && ((GfxConvertLastScanTris() >= 64 && ((n + n / 32 >= sPrevScan && n <= sPrevScan + sPrevScan / 32) || GfxConvertLastScanClears())) ||
                                 (GfxConvertLastScanCovers() && n == sPrevScan));
            if (!same) sSkipNo[GfxConvertLastScanTris() < 64 ? 1 : 2]++;
            sPrevScan = n;
        } else if (!sReplayMode) {
            if (!behind) GfxConvertScanOffscreen(dl, &scanW);
            sPrevScan = GfxConvertLastScanCount();
        }
        if (same) {
            sSkipRun++;
            sSkipped++;
            skipFrame = true;
        } else {
            sSkipRun = 0;
        }
        { // la hora del 3DS para el juego (fondo del mapa); fondoprueba.txt fuerza un fondo (para probar)
          static int sBg = -2; if (sBg == -2) { const u32 v = host_flag("fondoprueba"); sBg = v < 64 ? (int)v : 0; }
          if (BLOB_HOUR) *(volatile u32*)BLOB_HOUR = __builtin_bswap32((u32)ps_hour_now());
          if (BLOB_BGTEST) *(volatile u32*)BLOB_BGTEST = __builtin_bswap32((u32)sBg); }
        // Pantalla gigante del estadio (fragment62): la imagen de la pelea (gb_jumbo_render), copiada a lo
        // mas 5 veces por segundo y solo si cambio. Se borran sus texturas del cache para que el juego vea la nueva.
#if defined(BLOB_JUMBO) && defined(BLOB_JUMBO_ON) && defined(BLOB_BAT_ON)
        {
            static u64 sJbAt;
            static u32 sJbSum;
            const u64 nowJ = svcGetSystemTick();
            const bool inBat = BLOB_BAT_ON && intro_be(BLOB_BAT_ON) != 0;
            if (!inBat) { if (*(volatile u32*)BLOB_JUMBO_ON) *(volatile u32*)BLOB_JUMBO_ON = 0; sJbSum = 0; }
            else if (sJbReady && nowJ - sJbAt > 268111856ull / 5) { // la imagen de la pelea que arma gb_jumbo_render
                sJbAt = nowJ;
                const u8* fb = sJbImg;
                {
                    const u32 sum = sJbSerial;
                    if (sum != sJbSum) {
                        sJbSum = sum;
                        u8* tex = (u8*)BLOB_JUMBO;
                        for (int x = 0; x < 320; x++) { // por columna, como esta guardada la pantalla
                            const u8* col = fb + x * 240 * 3;
                            for (int y = 0; y < 240; y++) {
                                const u8* q = col + (239 - y) * 3; // B, G, R
                                const u16 c = (u16)((q[2] >> 3) << 11 | (q[1] >> 3) << 6 | (q[0] >> 3) << 1 | 1);
                                u8* d = tex + (((y >> 5) * 10 + (x >> 5)) * 1024 + (y & 31) * 32 + (x & 31)) * 2;
                                d[0] = (u8)(c >> 8); d[1] = (u8)c;
                            }
                        }
                        for (int t = 0; t < 80; t++) sInterp->TextureCacheDelete(tex + t * 2048);
                        *(volatile u32*)BLOB_JUMBO_ON = __builtin_bswap32(1u);
                    }
                }
            }
        }
#endif
        { // aeprobe.txt = 1: cada 30 cuadros, lo que el juego tiene en D_800AE540 (equipos de los jugadores y registrados)
          static int sAe = -2; if (sAe == -2) sAe = (int)host_flag("aeprobe");
          if (sAe == 1 && BLOB_AE540 && sFrames % 30 == 0) {
              const u8* a = (const u8*)BLOB_AE540;
              char b[400]; int k = snprintf(b, sizeof b, "[ae] %lu gs %lx h %02x%02x%02x%02x", (unsigned long)sFrames, (unsigned long)(BLOB_GAME_STATE ? intro_be(BLOB_GAME_STATE) : 0), a[0], a[1], a[2], a[3]);
              for (int p = 0; p < 4; p++) { const u8* q = a + 4 + p * 0x21C; k += snprintf(b + k, sizeof b - k, " | P%d %02x%02x%02x %lx:", p, q[0], q[1], q[2], (unsigned long)intro_be((u32)(uintptr_t)(q + 0x18))); for (int j = 0; j < 6; j++) k += snprintf(b + k, sizeof b - k, "%d,", q[0x1C + j * 0x54]); }
              for (int p = 0; p < 4; p++) { const u8* q = a + 0x874 + p * 0x248; k += snprintf(b + k, sizeof b - k, " | R%d %02x%02x%02x:", p, q[1], q[2], q[3]); for (int j = 0; j < 6; j++) k += snprintf(b + k, sizeof b - k, "%d,", q[0x28 + j * 0x54]); }
              k += snprintf(b + k, sizeof b - k, " | t"); for (int j = 0x11E4; j < 0x11F8; j++) k += snprintf(b + k, sizeof b - k, "%02x", a[j]);
              logf("%s\n", b);
          } }
        { // framelog.txt = cuadro: desde ahi, 400 cuadros, una linea por cuadro (comandos, triangulos, si se salto)
          static int sFlAt = -2, sFlLeft = 0;
          if (sFlAt == -2) sFlAt = (int)host_flag("framelog");
          if (sFlAt > 0 && (int)sFrames >= sFlAt) { sFlAt = -1; sFlLeft = 400; }
          if (sFlLeft > 0) { sFlLeft--; logf("[cuadro] %lu cmds %lu tri %lu atras %d %s\n", (unsigned long)sFrames, (unsigned long)GfxConvertLastScanCount(), (unsigned long)GfxConvertLastScanTris(), (int)behind, skipFrame ? "SALTADO" : ""); } }
        // El juego manda cuadros al ritmo del VI: un atraso viejo (una carga) no se recupera nunca y se
        // quedaba saltando 2 de cada 3 cuadros aunque sobrara tiempo. Se perdona una parte del atraso
        // en cada cuadro; si de verdad vamos lentos, el atraso vuelve a crecer y se sigue saltando.
        if (gPsDebtDecay3DS && now > sExpected) sExpected += (now - sExpected) / 4;
        if (!sReplayMode && !skipFrame) {
            // acelerones vistos: 5 cuadros dibujados seguidos en menos de 100 ms (a 30 por segundo son 133)
            static u64 sDT[4];
            static u32 sDI, sDN;
            static bool sDIn;
            const u64 old = sDT[sDI];
            sDT[sDI] = now;
            sDI = (sDI + 1) % 4;
            if (sDN < 4) sDN++;
            else {
                const bool fast = now - old < 268111ull * 100;
                if (fast && !sDIn) {
                    sDrawBurst[0]++;
                    const bool intro = BLOB_INTRO && __builtin_bswap32(*(const volatile u32*)BLOB_INTRO) != 0;
                    if (intro) sDrawBurst[1]++;
                    static u32 sLogged;
                    if (sLogged < 40) { sLogged++; logf("[aceleron] cuadro %lu: 5 dibujados en %lu ms%s\n", (unsigned long)sFrames, (unsigned long)TICKS_MS(now - old), intro ? " (intro)" : ""); }
                }
                sDIn = fast;
            }
        }
    }
    GfxConvertStats st = {};
    u64 t0 = svcGetSystemTick();
#ifdef PS_HANG_TEST // solo para probar el vigilante en el emulador (no se entrega)
    if (sFrames == 600) { stage_set(ST_GFX_GPUWAIT); for (volatile u32 i = 0;; i++) {} }
#endif
    stage_set(ST_GFX_CONV);
    u32 nativeDl = skipFrame ? 0 : GfxConvertFrame(dl, &st);
    stage_set(ST_GFX);
    u64 t1 = svcGetSystemTick();
    sTConv += t1 - t0;
    sPkCR += t1 - t0;
    sFrames++;
#ifdef PS_BLACKBOX
    gBoxFrame = sFrames;
#endif
    if ((sFrames % 60) == 0) {
        u64 now = svcGetSystemTick();
        if (sTPerfStart) {
            logf("[t] 60f en %lums (%lu sin dibujar): conv %lu render %lu ndsp %lu assets %lu hle1 %lu | atrasado sin saltar: textura %lu, 2D %lu, cambia %lu, tope %lu\n",
                 (unsigned long)TICKS_MS(now - sTPerfStart), (unsigned long)sSkipped,
                 (unsigned long)TICKS_MS(sTConv), (unsigned long)TICKS_MS(sTRender), (unsigned long)TICKS_MS(sTAudio),
                 (unsigned long)TICKS_MS(sTRes), (unsigned long)TICKS_MS(sTAudioHle), (unsigned long)sSkipNo[0], (unsigned long)sSkipNo[1],
                 (unsigned long)sSkipNo[2], (unsigned long)sSkipNo[3]);
            memset(sSkipNo, 0, sizeof sSkipNo);
            if (host_flag("scandbg") == 1) logf("[scan] rects por ciclo: 1c %lu 2c %lu copia %lu relleno %lu | cuadros que tapan todo %lu\n", (unsigned long)gGfxScanDbg[0], (unsigned long)gGfxScanDbg[1], (unsigned long)gGfxScanDbg[2], (unsigned long)gGfxScanDbg[3], (unsigned long)gGfxScanDbg[4]);
            memset(gGfxScanDbg, 0, sizeof gGfxScanDbg);
            sTAudioHle = 0;
            {
                char b[400];
                int k = snprintf(b, sizeof b, "[cpu] juego por hilo:");
                for (int i = 0; i < 32; i++)
                    if (TICKS_MS(sAccThr[i])) k += snprintf(b + k, sizeof b - k, " h%d %lu", i, (unsigned long)TICKS_MS(sAccThr[i]));
                k += snprintf(b + k, sizeof b - k, " | host:");
                for (int i = 0; i < ST_COUNT; i++)
                    if (i != ST_GAME && TICKS_MS(sAccStage[i]) && k < 330) k += snprintf(b + k, sizeof b - k, " %.14s %lu", kStageName[i], (unsigned long)TICKS_MS(sAccStage[i]));
                logf("%s\n", b);
#if BLOB_ACMD_TICKS
                {
                    static const char* const nm[16] = { "nop", "ADPCM", "CLEAR", "ENVMIX", "LOADB", "RESAMP", "SAVEB", "n7", "n8", "SETVOL", "DMOVE", "LOADADP", "MIXER", "INTERL", "02B0", "SETLOOP" };
                    k = snprintf(b, sizeof b, "[acmd] ms/n:");
                    for (int i = 0; i < 16; i++) {
                        volatile u32* t = (volatile u32*)(BLOB_ACMD_TICKS + i * 4); volatile u32* n = (volatile u32*)(BLOB_ACMD_N + i * 4);
                        const u32 tv = __builtin_bswap32(*t), nv = __builtin_bswap32(*n);
                        if (nv) k += snprintf(b + k, sizeof b - k, " %s %lu/%lu", nm[i], (unsigned long)(tv / 268111), (unsigned long)nv);
                        *t = 0; *n = 0;
                    }
                    logf("%s\n", b);
                }
#endif
                memset(sAccThr, 0, sizeof sAccThr);
                memset(sAccStage, 0, sizeof sAccStage);
            }
            {
                // % del sonido que el juego genero contra el que hacia falta en ese tiempo (100% = al ritmo real)
                extern u64 sAudioSamplesPub;
                const u64 ms = TICKS_MS(now - sTPerfStart);
                const u64 need = (u64)sAudioFreqSeen * ms / 1000;
                logf("[a] audio %lu%% juego %lu%% del ritmo real, %lu veces sin sonido, %lu tirados, %lu silencios (%lu ms)\n",
                     (unsigned long)(need ? sAudioSamplesPub * 100 / need : 0), (unsigned long)(ms ? 200000 / ms : 0),
                     (unsigned long)sAudioUnder, (unsigned long)sAudioDrops2, (unsigned long)sAudPads,
                     (unsigned long)(sAudioFreqSeen ? (u64)sAudPadSamples * 1000 / sAudioFreqSeen : 0));
                if (BLOB_AUD_SYNC) {
                    // sincronia: cuanto sonido se armo contra lo que avanzo el reloj del juego (acumulado, en ms)
                    auto be_load = [](u32 a) { return __builtin_bswap32(*(const volatile u32*)a); };
                    const u32 vi = be_load(BLOB_AUD_SYNC), gen = be_load(BLOB_AUD_SYNC + 4), fb = be_load(BLOB_AUD_SYNC + 8), drop = be_load(BLOB_AUD_SYNC + 12);
                    const s32 adj = (s32)be_load(BLOB_AUD_SYNC + 16);
                    const u32 fq = be_load(BLOB_AUD_SYNC + 20) ? be_load(BLOB_AUD_SYNC + 20) : 32000;
                    const long clockMs = (long)((u64)vi * 1000 / 60), genMs = (long)((u64)gen * 1000 / fq);
                    // retraso del sonido contra la imagen: lo que habia en la salida cuando se armo cada cuadro de sonido
                    static u32 sLvSum0, sLvN0;
                    const u32 lvSum = be_load(BLOB_AUD_SYNC + 24), lvN = be_load(BLOB_AUD_SYNC + 28), lvMax = be_load(BLOB_AUD_SYNC + 32);
                    const u32 dN = lvN - sLvN0, lvAvg = dN ? (lvSum - sLvSum0) / dN : 0;
                    sLvSum0 = lvSum; sLvN0 = lvN;
                    *(volatile u32*)(BLOB_AUD_SYNC + 32) = 0;
                    sLatTot[0] += (u32)((u64)lvAvg * 1000 / fq); sLatTot[1]++;
                    if ((u32)((u64)lvMax * 1000 / fq) > sLatTot[2]) sLatTot[2] = (u32)((u64)lvMax * 1000 / fq);
                    if (BLOB_INTRO && intro_be(BLOB_INTRO) == 1 && sIntroT0) {
                        // intro con musica: lo que se oye (armado - lo que aun espera en la salida) contra la imagen
                        const u32 realMs = (u32)TICKS_MS(svcGetSystemTick() - sIntroT0);
                        const u32 imgMs = (u32)((u64)(vi - intro_be(BLOB_INTRO + 4)) * 1000 / 60);
                        const u32 musMs = (u32)((u64)(gen - intro_be(BLOB_INTRO + 8)) * 1000 / fq);
                        const u32 resMs = (u32)((u64)lvAvg * 1000 / fq);
                        const s32 des = (s32)musMs - (s32)resMs - (s32)imgMs;
                        sIntroLast[0] = realMs; sIntroLast[1] = imgMs; sIntroLast[2] = musMs; sIntroLast[3] = resMs;
                        if (!sIntroDes[1] || des < sIntroDes[2]) sIntroDes[2] = des;
                        if (!sIntroDes[1] || des > sIntroDes[3]) sIntroDes[3] = des;
                        sIntroDes[0] += des; sIntroDes[1]++;
                        sIntroAuto[0] = intro_be(BLOB_INTRO + 24); sIntroAuto[1] = intro_be(BLOB_INTRO + 28);
                        sIntroFollow[0] = (s32)intro_be(BLOB_INTRO + 28); sIntroFollow[1] = (s32)intro_be(BLOB_INTRO + 16);
                        logf("[intro] real %lu ms | imagen %lu ms | musica armada %lu ms, en la salida %lu ms | desfase (musica - imagen) %ld ms | sin dibujar %lu de 60\n",
                             (unsigned long)realMs, (unsigned long)imgMs, (unsigned long)musMs, (unsigned long)resMs, (long)des, (unsigned long)sSkipped);
                    }
                    {   // sonidos tirados: solo cuando cambia algo
                        static u32 sSndPrev;
                        u32 sum = 0;
                        if (BLOB_SND) for (int i : { 0, 2, 3, 4, 5, 6, 7, 12, 13 }) sum += intro_be(BLOB_SND + 4 * i);
                        if (sum != sSndPrev) { sSndPrev = sum; snd_line("[snd]"); }
                    }
                    logf("[s] reloj del juego %ld ms, sonido armado %ld ms (diferencia %ld) | sin cuadros %lu ms, tirado %lu ms, ajuste %ld ms, silencios %lu ms | retraso del sonido %lu ms (max %lu), descartado %lu ms, objetivo %lu ms, minimo %lu ms\n",
                         clockMs, genMs, genMs - clockMs, (unsigned long)((u64)fb * 1000 / fq), (unsigned long)((u64)drop * 1000 / fq),
                         (long)((s64)adj * 1000 / (s32)fq), (unsigned long)sAudPadTot[1], (unsigned long)((u64)lvAvg * 1000 / fq),
                         (unsigned long)((u64)lvMax * 1000 / fq), (unsigned long)((u64)be_load(BLOB_AUD_SYNC + 36) * 1000 / fq),
                         (unsigned long)be_load(BLOB_AUD_SYNC + 40), (unsigned long)((u64)be_load(BLOB_AUD_SYNC + 44) * 1000 / fq));
                }
                sAudPadTot[0] += sAudPads; sAudPadTot[1] += sAudioFreqSeen ? (u32)((u64)sAudPadSamples * 1000 / sAudioFreqSeen) : 0;
                sAudPads = sAudPadSamples = 0;
                // para el resumen: cortes de sonido y ventanas en que el juego fue mas rapido o mas lento que el ritmo real
                sAudTot[0] += sAudioUnder; sAudTot[1] += sAudioDrops2; sAudTot[2] += (u32)(need ? sAudioSamplesPub * 100 / need : 0); sAudTot[3]++;
                if (ms && ms < 1900) sSpeedTot[0]++;
                if (ms > 2200) sSpeedTot[1]++;
                if (ms && (!sSpeedTot[2] || ms < sSpeedTot[2])) sSpeedTot[2] = (u32)ms;
                sAudioSamplesPub = 0;
            }
            sAudioUnder = sAudioDrops2 = 0;
            static u64 lastTri, lastDraw, lastTex, lastTexB, lastVtxB;
            u64 tri = sRenderer.GetTriangleCount(), draw = sRenderer.GetDrawCallCount();
            u64 tex = sRenderer.GetTextureCacheUploadCount(), texB = sRenderer.GetTextureCacheUploadBytes();
            u64 vtxB = sRenderer.GetVertexUploadBytes();
            logf("[r] run %lu espera %lu tri %lu draws %lu tex %lu/%luKB vtx %luKB\n", (unsigned long)TICKS_MS(sTRun),
                 (unsigned long)(sTWait / 1000), (unsigned long)(tri - lastTri), (unsigned long)(draw - lastDraw),
                 (unsigned long)(tex - lastTex), (unsigned long)((texB - lastTexB) / 1024), (unsigned long)((vtxB - lastVtxB) / 1024));
            lastTri = tri; lastDraw = draw; lastTex = tex; lastTexB = texB; lastVtxB = vtxB;
            sTRun = sTWait = 0;
            logf("[rom] %lu ms, %lu aciertos %lu fallos; assets en cache %lu KB, %lu reusados %lu nuevos %lu sacados\n", (unsigned long)TICKS_MS(sTRom),
                 (unsigned long)sRomHits, (unsigned long)sRomMiss, (unsigned long)(sResCBytes / 1024), (unsigned long)sResCHits,
                 (unsigned long)sResCAdds, (unsigned long)sResCEvict);
            sResCHits = sResCAdds = sResCEvict = 0;
            sTRom = 0; sRomHits = sRomMiss = 0;
            {
                const mc::Stats m = GfxConvertModelCacheStats();
                {
                    const u64 wms = TICKS_MS(now - sTPerfStart), cms = TICKS_MS(sTConv);
                    DiagTotals& d = sDiag;
                    d.verifyOk += m.verifyOk; d.verifyBad += m.verifyBad; d.hits += m.hits; d.misses += m.misses; d.inputMisses += m.inputMisses;
                    d.baked += m.baked; d.invalidated += m.invalidated; d.evicted += m.evicted; d.idle += m.idleDropped;
                    d.allocFails += m.allocFails; d.budgetFails += m.budgetFails;
                    if (m.peakBytes / 1024 > d.peakKB) d.peakKB = m.peakBytes / 1024;
                    const u32 lin = (u32)(linearSpaceFree() / 1024);
                    if (lin < d.minLinearKB) d.minLinearKB = lin;
                    d.windows++; d.ms += wms; d.frames += 60; d.drawn += 60 - (sSkipped < 60 ? sSkipped : 60); d.convMs += cms;
                    if (wms > d.worstWindowMs) d.worstWindowMs = wms;
                    if (wms > 4000) d.slowWindows++;
                    if (cms > d.worstConvMs) d.worstConvMs = (u32)cms;
                    d.renderMs += TICKS_MS(sTRender);
                    if (m.gpuBytes / 1024 > d.gpuPeakKB) d.gpuPeakKB = m.gpuBytes / 1024;
                    d.gpuBuilt += m.gpuBuilt; d.gpuRebuilt += m.gpuRebuilt; d.gpuAllocFails += m.gpuAllocFails;
                    if (kDiag && (d.windows % 10) == 0)
                        logf("[diag] f%lu acumulado: FPS %lu, verificacion %lu iguales %lu distintos, cache %lu/%lu aciertos/fallos, %lu invalidados, "
                             "%lu KB en cache (max %lu), lineal libre %lu KB, errores de asignacion %lu\n",
                             (unsigned long)sFrames, (unsigned long)(d.ms ? d.drawn * 1000 / d.ms : 0), (unsigned long)d.verifyOk,
                             (unsigned long)d.verifyBad, (unsigned long)d.hits, (unsigned long)d.misses, (unsigned long)d.invalidated,
                             (unsigned long)(m.bytes / 1024), (unsigned long)d.peakKB, (unsigned long)lin, (unsigned long)d.allocFails);
                }
                logf("[mc] aciertos %lu fallos %lu horneados %lu | invalidados %lu (%lu avisos de %lu) sacados %lu ociosos %lu reemplazos %lu | "
                     "entradas distintas %lu | sin cache: grande %lu vacio %lu sin memoria %lu presupuesto %lu | %lu lotes %lu KB (max %lu KB)%s\n",
                     (unsigned long)m.hits, (unsigned long)m.misses, (unsigned long)m.baked, (unsigned long)m.invalidated, (unsigned long)m.eventHits,
                     (unsigned long)m.events, (unsigned long)m.evicted, (unsigned long)m.idleDropped, (unsigned long)m.replaced,
                     (unsigned long)m.inputMisses, (unsigned long)m.uncacheable[mc::UC_TOO_BIG], (unsigned long)m.uncacheable[mc::UC_EMPTY],
                     (unsigned long)m.allocFails, (unsigned long)m.budgetFails, (unsigned long)m.entries, (unsigned long)(m.bytes / 1024),
                     (unsigned long)(m.peakBytes / 1024), "");
                if (m.verifyOk || m.verifyBad) logf("[mc] verificacion: %lu iguales %lu distintos\n", (unsigned long)m.verifyOk, (unsigned long)m.verifyBad);
                // F2: lotes dibujados desde buffers residentes y por que otros fueron por el camino anterior
                const uint64_t* f = gPsResStats3DS;
                if (f[0] || f[13] || f[2]) logf("[f2] %s lotes %lu grupos %lu dibujos %lu tri %lu | anterior: lotes %lu (sin copia %lu cerca %lu) grupos: huesos %lu texgen %lu "
                     "luzcpu %lu sombra2 %lu material %lu | copias %lu (rehechas %lu, sin memoria %lu) %lu KB%s\n",
                     gGfxConvertF2 ? "on" : "APAGADO", (unsigned long)f[0], (unsigned long)f[1], (unsigned long)gPsResBackend3DS[0],
                     (unsigned long)gPsResBackend3DS[1], (unsigned long)f[13], (unsigned long)f[2], (unsigned long)f[3], (unsigned long)f[4],
                     (unsigned long)f[5], (unsigned long)f[6], (unsigned long)f[7], (unsigned long)f[8], (unsigned long)m.gpuBuilt,
                     (unsigned long)m.gpuRebuilt, (unsigned long)m.gpuAllocFails, (unsigned long)(m.gpuBytes / 1024), "");
                {   // capa del HUD
                    uint32_t* h = gPsHudStats3DS; uint32_t* c = gGfxConvertHudStats;
                    if (c[0] || c[2] || c[1] || c[3]) logf("[hud] %s tramos %lu (no aptos %lu, sin tramo %lu) | copiados %lu capturas %lu dibujados %lu | con z %lu sin destino %lu llaves nuevas %lu\n",
                         gGfxConvertHud ? "on" : "APAGADA", (unsigned long)c[0], (unsigned long)c[2], (unsigned long)c[1], (unsigned long)h[0], (unsigned long)h[1],
                         (unsigned long)h[2], (unsigned long)h[4], (unsigned long)h[3], (unsigned long)h[5]);
                    if (c[3]) logf("[hud] tramos sin convertir (ya en su capa): %lu\n", (unsigned long)c[3]);
                    if (h[7] || h[6]) logf("[hud] llaves nuevas solo por estado: %lu; video libre al crear %lu KB\n", (unsigned long)h[7], (unsigned long)h[6]);
                    if (gGfxConvertHudWhy) { logf("[hud] no aptos por: %04lX\n", (unsigned long)gGfxConvertHudWhy); gGfxConvertHudWhy = 0; }
                    sHudTot[0] += h[0]; sHudTot[1] += h[1]; sHudTot[2] += h[2]; sHudTot[3] += h[4];
                    memset(h, 0, 8 * sizeof(uint32_t)); memset(c, 0, 4 * sizeof(uint32_t));
                }
                if (f[10] || f[11]) logf("[f2] verificacion: %lu vertices iguales %lu distintos\n", (unsigned long)f[10], (unsigned long)f[11]);
                for (int i = 0; i < 16; i++) sDiag.f2[i] += f[i];
                sDiag.f2[14] += gPsResBackend3DS[0];
                sDiag.f2[15] += gPsResBackend3DS[1];
                for (auto& x : gPsResStats3DS) x = 0;
                gPsResBackend3DS[0] = gPsResBackend3DS[1] = 0;
            }
            logf("[x] excepciones: mips %lu ram baja %lu\n", (unsigned long)sExcTraps, (unsigned long)sExcLowRam);
            sExcTraps = sExcLowRam = 0;
            logf("[b] draw %lu(%lu) tex %lu(%lu) shader %lu(%lu)\n", (unsigned long)TICKS_MS(sBT.draw),
                 (unsigned long)sBT.draws, (unsigned long)TICKS_MS(sBT.upload), (unsigned long)sBT.uploads,
                 (unsigned long)TICKS_MS(sBT.shader), (unsigned long)sBT.shaders);
            sBT = {};
        }
        if (gPsProf) {
            static const char* kN[21] = { "flush", "shader", "texlk", "teximp", "mtx", "vtx", "tri", "trect", "frect", "texdel",
                                          "Bdraw", "Bcomb", "Bvert", "Bc3d", "Bpack", "Bpre", "Tmat", "Tvtx", "Skin", "Sapp", "Sbeg" };
            char line[400]; int o = 0;
            for (int i = 0; i < 21; i++) {
                o += snprintf(line + o, sizeof line - o, "%s %lu/%lu ", kN[i], (unsigned long)TICKS_MS(gPsProf[i]), (unsigned long)gPsProfCount[i]);
                gPsProf[i] = 0; gPsProfCount[i] = 0;
            }
            logf("[p] %s hash %lu\n", line, (unsigned long)TICKS_MS(sTTexHash));
            if (gPsOpTicks3DS) { // los 10 opcodes mas caros (ms inclusivos / veces)
                char ol[300]; int oo = 0;
                for (int k = 0; k < 10; k++) {
                    int best = -1;
                    for (int i = 0; i < 256; i++) if (gPsOpTicks3DS[i] && (best < 0 || gPsOpTicks3DS[i] > gPsOpTicks3DS[best])) best = i;
                    if (best < 0) break;
                    oo += snprintf(ol + oo, sizeof ol - oo, "%02X %lu/%lu ", best, (unsigned long)TICKS_MS(gPsOpTicks3DS[best]), (unsigned long)gPsOpCount3DS[best]);
                    gPsOpTicks3DS[best] = 0;
                }
                for (int i = 0; i < 256; i++) { gPsOpTicks3DS[i] = 0; gPsOpCount3DS[i] = 0; }
                logf("[op] %s\n", ol);
            }
            if (gPsFlushWhy) {
                char fl[200]; int fo = 0;
                for (int i = 0; i < 32; i++) if (gPsFlushWhy[i]) { fo += snprintf(fl + fo, sizeof fl - fo, "%d:%lu ", i, (unsigned long)gPsFlushWhy[i]); gPsFlushWhy[i] = 0; }
                logf("[f] %s\n", fl);
            }
        }
        {
            auto& c = ::mk64_3ds::gNativeGeometry3DS.counters;
            logf("[n] gpu %lu cpu: ineleg %lu matriz %lu cerca %lu apagado %lu\n", (unsigned long)c.nativeTriangles,
                 (unsigned long)c.fallbackIneligible, (unsigned long)c.fallbackMixedMatrix, (unsigned long)c.fallbackNearEye,
                 (unsigned long)c.fallbackDisabled);
            c = {};
            uint64_t* k = gMk64InterpretedTriangles3DS;
            {
                int top[4] = { -1, -1, -1, -1 };
                for (int o = 0; o < 256; o++)
                    for (int t = 0; t < 4; t++)
                        if (top[t] < 0 || gPsBumpOp3DS[o] > gPsBumpOp3DS[top[t]]) {
                            for (int u = 3; u > t; u--) top[u] = top[u - 1];
                            top[t] = o;
                            break;
                        }
                logf("[m] mat si %lu no %lu rect %lu; cambios %02X:%lu %02X:%lu %02X:%lu %02X:%lu\n", (unsigned long)gPsMatDbg3DS[0],
                     (unsigned long)gPsMatDbg3DS[1], (unsigned long)gPsMatDbg3DS[2], top[0], (unsigned long)gPsBumpOp3DS[top[0]], top[1],
                     (unsigned long)gPsBumpOp3DS[top[1]], top[2], (unsigned long)gPsBumpOp3DS[top[2]], top[3], (unsigned long)gPsBumpOp3DS[top[3]]);
                memset(gPsMatDbg3DS, 0, sizeof(uint32_t) * 4);
                memset(gPsBumpOp3DS, 0, sizeof(uint32_t) * 256);
            }
            { uint32_t ms[8]; GfxConvertMeshStats(ms); logf("[c] fijas: %lu lotes %lu tri, %lu listas no; tri en listas %lu\n", (unsigned long)ms[4], (unsigned long)ms[5], (unsigned long)ms[6], (unsigned long)ms[1]); }
            logf("[tx] cambios %u misma pag %u; atlas subidas %u sin lugar %u paginas %u\n", gPsTexDbg3DS[0], gPsTexDbg3DS[6],
                 gPsAtlasStats3DS[0], gPsAtlasStats3DS[1], gPsAtlasStats3DS[2]);
            gPsAtlasStats3DS[0] = gPsAtlasStats3DS[1] = 0;
            memset(gPsTexDbg3DS, 0, sizeof(unsigned int) * 8);
            logf("[s] cpu: dinamica %lu luces %lu niebla %lu texgen %lu elegible %lu rect %lu\n", (unsigned long)k[0],
                 (unsigned long)k[1], (unsigned long)k[2], (unsigned long)k[3], (unsigned long)k[4], (unsigned long)k[5]);
            for (int i = 0; i < 6; i++) k[i] = 0;
            uint32_t sb, st;
            GfxConvertSkinStats(&sb, &st);
            logf("[k] skin: conv %lu lotes %lu tri; dib %lu lotes %lu tri %lu seg %lu mtx; no %lu/%lu/%lu/%lu/%lu gpu %lu\n", (unsigned long)sb, (unsigned long)st,
                 (unsigned long)gPsSkinStats3DS[0], (unsigned long)gPsSkinStats3DS[1], (unsigned long)gPsSkinStats3DS[2],
                 (unsigned long)gPsSkinStats3DS[3], (unsigned long)gPsSkinStats3DS[4], (unsigned long)gPsSkinStats3DS[5],
                 (unsigned long)gPsSkinStats3DS[6], (unsigned long)gPsSkinStats3DS[7], (unsigned long)gPsSkinStats3DS[8],
                 (unsigned long)gPsSkinStats3DS[9]);
            for (auto& x : gPsSkinStats3DS) x = 0;
            logf("[r] memoria lineal libre %lu KB\n", (unsigned long)(linearSpaceFree() / 1024));
            logf("[r] retenidas: %lu envios de %lu lotes, %lu capturados, %lu fallas, %lu cerca del ojo, %lu KB\n",
                 (unsigned long)gPsRetainStats3DS[0], (unsigned long)gPsRetainStats3DS[2], (unsigned long)gPsRetainStats3DS[1],
                 (unsigned long)gPsRetainStats3DS[3], (unsigned long)gPsRetainStats3DS[4], (unsigned long)(gPsRetainStats3DS[5] / 1024));
            logf("[r] sin retener: chicos %lu apagado %lu sin lugar %lu fallo %lu esperando %lu se rindio %lu | capturas %lu\n",
                 (unsigned long)gPsRetainSkip3DS[0], (unsigned long)gPsRetainSkip3DS[1], (unsigned long)gPsRetainSkip3DS[2], (unsigned long)gPsRetainSkip3DS[3],
                 (unsigned long)gPsRetainSkip3DS[4], (unsigned long)gPsRetainSkip3DS[5], (unsigned long)gPsRetainSkip3DS[6]);
            for (auto& x : gPsRetainSkip3DS) x = 0;
            logf("[r] fallas: niebla %lu texgen %lu luz %lu cpu %lu detras %lu cerca %lu copia %lu mem %lu\n",
                 (unsigned long)gPsRetainWhy3DS[0], (unsigned long)gPsRetainWhy3DS[1], (unsigned long)gPsRetainWhy3DS[2], (unsigned long)gPsRetainWhy3DS[3],
                 (unsigned long)gPsRetainWhy3DS[4], (unsigned long)gPsRetainWhy3DS[5], (unsigned long)gPsRetainWhy3DS[6], (unsigned long)gPsRetainWhy3DS[7]);
            for (auto& x : gPsRetainWhy3DS) x = 0;
            for (int i = 0; i < 5; i++) gPsRetainStats3DS[i] = 0;
        }
        sTTexHash = 0;
        sSkipped = 0;
        sTPerfStart = now; sTConv = sTRender = sTAudio = sTRes = 0;
        diag_flush(false);
    }
    if (sFrames <= 3 || (sFrames % 300) == 0 || st.errors) {
        logf("[gfx] frame %lu: dl %08lx -> %08lx, %lu comandos, %lu vertices, %lu errores, %lu KB\n", sFrames, dl, nativeDl,
             st.commands, st.vertices, st.errors, st.bytes / 1024);
    }
    sInterp->HandleWindowEvents();
    if (nativeDl == 0) return;
    // Stadium cambia a 640x480 en los menus (osViSetMode alta resolucion): el interprete debe saber
    // el tamano nativo o solo dibuja la esquina superior izquierda al doble.
    {
        static u32 sNativeW = 320;
        u32 w = GfxConvertFramebufferWidth();
        if (w >= 320 && w != sNativeW) {
            sNativeW = w;
            sInterp->SetNativeDimensions((float)w, w >= 640 ? 480.0f : 240.0f);
            logf("[vi] resolucion nativa %lux%lu\n", (unsigned long)w, (unsigned long)(w >= 640 ? 480 : 240));
        }
        u32 r, f, tr;
        GfxConvertSoftStats(&r, &f, &tr);
        static u32 sSoftLogs;
        // dumpsoft.txt: grabar los primeros cuadros que dibujan triangulos a una textura (para trabajar sobre ellos)
        {
            static int sDumpSoft = -1, sDumpSoftN = 0;
            static u32 sDumpSoftFirst = 0;
            if (sDumpSoft < 0) sDumpSoft = host_flag("dumpsoft") != 0xFFFFFFFFu;
            if (sDumpSoft && tr > 0 && !sDumpSoftFirst) sDumpSoftFirst = sFrames;
            // tambien unos cuadros despues: ahi el juego usa como textura lo que dibujo
            const bool after = sDumpSoftFirst && sFrames > sDumpSoftFirst && sFrames - sDumpSoftFirst <= 40 && ((sFrames - sDumpSoftFirst) % 8) == 0;
            if (sDumpSoft && (tr > 0 || after) && sDumpSoftN < 10) {
                sDumpSoftN++;
                char path[96];
                snprintf(path, sizeof path, "sdmc:/3ds/pokestadium/soft_%05lu.psfd", (unsigned long)sFrames);
                if (FILE* df = fopen(path, "wb")) {
                    u32 hdr[4] = { __builtin_bswap32(0x50534644u), __builtin_bswap32(BLOB_BASE), __builtin_bswap32(BLOB_SIZE), __builtin_bswap32(dl) };
                    fwrite(hdr, 4, 4, df);
                    fwrite((const void*)BLOB_BASE, 1, BLOB_SIZE, df);
                    fclose(df);
                    logf("[dump] soft frame %lu (%lu tri)\n", (unsigned long)sFrames, (unsigned long)tr);
                }
            }
        }
        if ((r || f || tr) && sSoftLogs < 20) {
            sSoftLogs++;
            logf("[soft] frame %lu: %lu rect %lu relleno %lu tri fuera de pantalla\n", sFrames, (unsigned long)r, (unsigned long)f, (unsigned long)tr);
        }
    }
    if (skipFrame) return;
    // El cache de texturas del interprete es por direccion. Stadium reescribe texturas en el mismo lugar
    // (letras de los textos, fondos JPEG, paletas): si el contenido cambio, se borra del cache.
    // Solo en cuadros que se dibujan, una vez por textura, y con muestreo: texturas chicas completas,
    // grandes por 64 palabras repartidas + inicio y final (antes costaba ~14 ms por cuadro).
    {
        static u32 sInval;
        u64 th0 = svcGetSystemTick();
        struct HashTimer { u64 t0; ~HashTimer() { sTTexHash += svcGetSystemTick() - t0; } } hashTimer{ th0 };
        // memoria grande que solto el juego (salir de un modo, empezar o terminar una pelea): las texturas que estaban ahi
        // se borran del cache una por una, igual que cuando cambia su contenido (los modelos ya los tira el aviso mismo)
        if (sFreedN) {
            const u32 n = sFreedN;
            for (auto it = sTexHash.begin(); it != sTexHash.end();) {
                const u32 a = (u32)it->first;
                bool hit = false;
                for (u32 r = 0; r < n && !hit; r++) hit = a >= sFreed[r][0] && a < sFreed[r][1];
                if (hit && !GfxConvertIsGpuTarget(it->first)) { sInterp->TextureCacheDelete(it->first); it = sTexHash.erase(it); } else ++it;
            }
            sFreedN = 0;
            sFlushes++;
        }
        for (const u8* p : GfxConvertTextures()) {
            u32 a = (u32)p;
            if (a < BLOB_BASE || a >= BLOB_BASE + BLOB_SIZE) continue;
            auto it = sTexHash.find(p);
            if (it != sTexHash.end() && it->second.frame == sFrames) continue; // ya revisada en este cuadro
            u32 n = BLOB_BASE + BLOB_SIZE - a;
            if (n > 4096) n = 4096;
            const u32* w = (const u32*)(a & ~3u);
            u32 words = n / 4, h = 2166136261u;
            if (words <= 128) {
                for (u32 i = 0; i < words; i++) h = (h ^ w[i]) * 16777619u;
            } else {
                for (u32 i = 0; i < 16; i++) h = (h ^ w[i]) * 16777619u;
                for (u32 i = 0; i < 16; i++) h = (h ^ w[words - 1 - i]) * 16777619u;
                u32 step = words / 64;
                for (u32 i = 0; i < 64; i++) h = (h ^ w[i * step + (i & 3)]) * 16777619u;
            }
            if (it == sTexHash.end()) { sTexHash.emplace(p, TexState{ h, sFrames }); continue; }
            it->second.frame = sFrames;
            if (it->second.hash != h) { it->second.hash = h; sInterp->TextureCacheDelete(p); sInval++; }
        }
        if ((sFrames % 300) == 0) {
            logf("[tex] %lu invalidadas, %lu rastreadas; imagenes GPU: %lu creadas, %lu reusadas; VRAM libre %lu KB\n", (unsigned long)sInval, (unsigned long)sTexHash.size(),
                 (unsigned long)gPsFbCreated3DS, (unsigned long)gPsFbReused3DS, (unsigned long)(vramSpaceFree() / 1024));
            sInval = 0;
        }
    }
    u64 t2 = svcGetSystemTick();
    PsSkinFrame3DS();
    stage_set(ST_GFX_GPUWAIT);
    sInterp->StartFrame();
    sTWait += sRenderer.GetFrameBeginWaitMicroseconds();
    u64 t3 = svcGetSystemTick();
    stage_set(ST_GFX_RUN);
    {   // La escena del VS japones deja su estado de dibujo (luces, niebla, colores y modos) y la pantalla que sigue usa
        // lo que encuentre: se guarda al empezar la escena y se repone al terminar, como si la escena no hubiera pasado.
        static bool was;
        struct Keep {
            decltype(sInterp->mRdp->env_color) env, prim, fog, blend, fill;
            uint32_t oml, omh, geo, xgeo; uint64_t comb; uint8_t lod;
            decltype(sInterp->mRsp->fog_mul) fogMul, fogOff;
            decltype(sInterp->mRsp->texture_scaling_factor) tsf;
            decltype(sInterp->mRsp->current_lights) lights;
            decltype(sInterp->mRsp->lookat) lookat;
            uint8_t numLights;
        };
        static Keep k;
        const bool on = BLOB_JP_ON && intro_be(BLOB_JP_ON) != 0;
        if (on != was) {
            auto* r = sInterp->mRsp; auto* d = sInterp->mRdp;
            logf("[jp] dibujo %s: luces %d amb %d env %d %d %d %d prim %d %d %d %d niebla %d %d %d %d geo %08lx oml %08lx\n", on ? "antes" : "despues", (int)r->current_num_lights,
                 r->current_num_lights > 0 ? r->current_lights[r->current_num_lights - 1].l.col[0] : -1,
                 d->env_color.r, d->env_color.g, d->env_color.b, d->env_color.a, d->prim_color.r, d->prim_color.g, d->prim_color.b, d->prim_color.a,
                 d->fog_color.r, d->fog_color.g, d->fog_color.b, d->fog_color.a, (unsigned long)r->geometry_mode, (unsigned long)d->other_mode_l);
            if (on) {
                k.env = d->env_color; k.prim = d->prim_color; k.fog = d->fog_color; k.blend = d->blend_color; k.fill = d->fill_color;
                k.oml = d->other_mode_l; k.omh = d->other_mode_h; k.comb = d->combine_mode; k.lod = d->prim_lod_fraction;
                k.geo = r->geometry_mode; k.xgeo = r->extra_geometry_mode; k.fogMul = r->fog_mul; k.fogOff = r->fog_offset; k.tsf = r->texture_scaling_factor;
                memcpy(&k.lights, &r->current_lights, sizeof k.lights); memcpy(&k.lookat, &r->lookat, sizeof k.lookat); k.numLights = r->current_num_lights;
            } else {
                d->env_color = k.env; d->prim_color = k.prim; d->fog_color = k.fog; d->blend_color = k.blend; d->fill_color = k.fill;
                d->other_mode_l = k.oml; d->other_mode_h = k.omh; d->combine_mode = k.comb; d->prim_lod_fraction = k.lod;
                r->geometry_mode = k.geo; r->extra_geometry_mode = k.xgeo; r->fog_mul = k.fogMul; r->fog_offset = k.fogOff; r->texture_scaling_factor = k.tsf;
                memcpy(&r->current_lights, &k.lights, sizeof k.lights); memcpy(&r->lookat, &k.lookat, sizeof k.lookat); r->current_num_lights = k.numLights;
                r->lights_changed = true;
            }
        }
        was = on;
    }
    bool texFault = false;
    gPsSkinActive3DS = false; gPsSkinLit3DS = false; gPsRetainNoDraw3DS = false; gPsRetainCaptureDst3DS = nullptr;
    try {
        sInterp->Run(reinterpret_cast<::Gfx*>(nativeDl), sNoReplacements);
    } catch (const std::bad_alloc& e) {
        // Un cuadro con mas texturas distintas de las que caben en la cache (o sin memoria para una): antes esto
        // cerraba el juego. Se deja el cuadro como va, se vacia la cache y el siguiente empieza limpio.
        snprintf(sTexFaultWhy, sizeof sTexFaultWhy, "%.24s, %lu tex, c%lu", e.what(), (unsigned long)GfxConvertTextures().size(), (unsigned long)sFrames);
        if (sTexFaults++ < 5) logf("[tex] cuadro %lu incompleto: %s (%lu texturas distintas en la lista; esperas ok %lu, sin cuadro %lu, sin destino %lu, fallo %lu)\n", (unsigned long)sFrames, e.what(), (unsigned long)GfxConvertTextures().size(),
                                   (unsigned long)gPsFenceWhy3DS[0], (unsigned long)gPsFenceWhy3DS[1], (unsigned long)gPsFenceWhy3DS[2], (unsigned long)gPsFenceWhy3DS[3]);
        sInterp->mBufVboLen = 0;
        sInterp->mBufVboNumTris = 0;
        sInterp->mRdp->textures_changed[0] = sInterp->mRdp->textures_changed[1] = true;
        texFault = true;
    } catch (const std::length_error& e) {
        // El cuadro no cupo en el bufer de comandos del GPU (o en el de vertices): antes esto cerraba el juego. Se deja
        // el cuadro como va y el siguiente empieza limpio.
        if (sGpuFaults++ < 5) logf("[gpu] cuadro %lu incompleto: %s\n", (unsigned long)sFrames, e.what());
        sInterp->mBufVboLen = 0;
        sInterp->mBufVboNumTris = 0;
        sInterp->mRdp->textures_changed[0] = sInterp->mRdp->textures_changed[1] = true;
    }
    // Si una excepcion (cache de texturas lleno, bufer del GPU) sale a media tanda de un modelo con huesos, el interprete
    // no llega a apagar la bandera de huesos: los dibujos siguientes (rectangulos 2D del HUD) se mandaban al GPU como si
    // fueran de un modelo y lo colgaban (captura de la consola con Earthquake: un rectangulo de la barra de vida con la
    // bandera de huesos encendida). Al terminar cada cuadro, con o sin excepcion, se apagan.
    gPsSkinActive3DS = false; gPsSkinLit3DS = false; gPsRetainNoDraw3DS = false; gPsRetainCaptureDst3DS = nullptr;
    gPsTriLast3DS = gPsTriNow3DS; gPsTriNow3DS = 0;
    sTRun += svcGetSystemTick() - t3;
    stage_set(ST_GFX_END);
    sInterp->EndFrame();
    // Cache de texturas lleno: se vacia, pero sin las imagenes que dibujo el GPU (retratos con el modelo): esas solo
    // existen en el cache, y borrarlas dejaba el retrato vacio hasta salir de la pantalla. Si vuelve a llenarse luego
    // luego, se vacia todo como antes.
    if (texFault) {
        static u32 sLastFault;
        if (sFrames - sLastFault < 30) sInterp->TextureCacheClear();
        else for (auto it = sTexHash.begin(); it != sTexHash.end();) {
            if (!GfxConvertIsGpuTarget(it->first)) { sInterp->TextureCacheDelete(it->first); it = sTexHash.erase(it); } else ++it;
        }
        sLastFault = sFrames;
    }
    stage_set(ST_GFX_FB);
    // Imagenes aparte (G_PS_FB): el juego las lee o las copia en cuanto este cuadro "termina", asi que se
    // espera al GPU y se copian a su memoria antes de regresar (solo en los cuadros que las dibujaron)
    if (PsFbFlushNow3DS()) sPsFbSyncFrames++;
    { const u64 d = svcGetSystemTick() - t2; sTRender += d; sPkCR += d; }
}

#ifdef PS_MEMPROF // solo para perfilar: quien llama a memset/memcpy/memcmp y con cuantos bytes
extern "C" {
void* __real_memset(void*, int, size_t); void* __real_memcpy(void*, const void*, size_t); int __real_memcmp(const void*, const void*, size_t);
struct MemProfEnt { u32 ra; u32 calls; u64 bytes; };
static MemProfEnt sMemProf[3][256];
static inline void memprof(int k, u32 ra, size_t n) {
    if (!on_main_thread()) return;
    MemProfEnt* t = sMemProf[k];
    u32 h = (ra >> 2) & 255;
    for (int i = 0; i < 256; i++, h = (h + 1) & 255) {
        if (t[h].ra == ra || t[h].ra == 0) { t[h].ra = ra; t[h].calls++; t[h].bytes += n; return; }
    }
}
void* __wrap_memset(void* p, int c, size_t n) { memprof(0, (u32)__builtin_return_address(0), n); return __real_memset(p, c, n); }
void* __wrap_memcpy(void* d, const void* s, size_t n) { memprof(1, (u32)__builtin_return_address(0), n); return __real_memcpy(d, s, n); }
int __wrap_memcmp(const void* a, const void* b, size_t n) { memprof(2, (u32)__builtin_return_address(0), n); return __real_memcmp(a, b, n); }
}
static void memprof_dump() {
    static const char* const nm[3] = { "memset", "memcpy", "memcmp" };
    for (int k = 0; k < 3; k++)
        for (int r = 0; r < 8; r++) {
            int best = -1;
            for (int i = 0; i < 256; i++) if (sMemProf[k][i].ra && (best < 0 || sMemProf[k][i].bytes > sMemProf[k][best].bytes)) best = i;
            if (best < 0) break;
            logf("[mem] %s ra %08lX llamadas %lu KB %lu\n", nm[k], (unsigned long)sMemProf[k][best].ra, (unsigned long)sMemProf[k][best].calls,
                 (unsigned long)(sMemProf[k][best].bytes / 1024));
            sMemProf[k][best].ra = 1; sMemProf[k][best].bytes = 0;
        }
    __real_memset(sMemProf, 0, sizeof sMemProf);
}
#endif
// ---------------- audio del RSP (HLE) en el nucleo 1 ----------------
// La sintesis de audio es casi la mitad del tiempo de CPU: en el Old 3DS se manda al nucleo 1 (el del sistema,
// que presta hasta 80%) y el juego sigue en paralelo, como con el RSP real.
void ps_call_blob(u32 entry, u32 arg0);
static Thread sAudThread;
static LightEvent sAudStart;
// Fila de tareas del lado del host (hasta 3): el juego las forma y el hilo del nucleo 1 las corre una tras otra y
// entrega el sonido de cada una al terminar, sin esperar a que el nucleo 0 pregunte. Antes el nucleo 0 hacia la
// entrega y arrancaba la siguiente: mientras dibujaba un cuadro (50 ms o mas en la consola) el sonido ya procesado
// no salia y el nucleo 1 se quedaba esperando.
struct AudJob { u32 fn, task; const u8* out; u32 outSize; };
static AudJob sAudJobs[8]; // hasta 6 pendientes (PS_AUD_Q en blob_host.c)
static volatile u32 sAudPut, sAudDone; // formadas y terminadas (totales); las pendientes son sAudPut - sAudDone
#define sAudState (sAudPut - sAudDone)
static volatile bool sAudQuit;
static LightLock sAudPushLock;
static void audio_push_impl(const u8* buf, u32 size, u32 freq);
static void audio_worker(void*) {
    __asm__ volatile("vmsr fpscr, %0" ::"r"(0x03000000u));
    while (true) {
        sStAud = 0; sStAudTick = (u32)(svcGetSystemTick() >> 18);
        LightEvent_Wait(&sAudStart);
        if (sAudQuit) break;
        while (sAudDone != sAudPut && !sAudQuit) {
            const AudJob job = sAudJobs[sAudDone & 7];
            sStAud = 1; sStAudTick = (u32)(svcGetSystemTick() >> 18);
            u64 t = svcGetSystemTick();
            ps_call_blob(job.fn, job.task);
            sStAudTasks++;
            { const u64 d = svcGetSystemTick() - t; sTAudioHle += d; sPkHle += d; }
            if (job.out && job.outSize) {
                LightLock_Lock(&sAudPushLock);
                audio_push_impl(job.out, job.outSize, sAudioFreqSeen ? sAudioFreqSeen : 32000);
                LightLock_Unlock(&sAudPushLock);
            }
            __sync_synchronize();
            sAudDone = sAudDone + 1;
            __sync_synchronize();
        }
    }
}
// Banderas en la SD (ver ps_flag en blob_host.c): numero dentro del archivo, 1 si esta vacio, ~0 si no existe
static u32 sBootKeys = 0; // botones presionados al iniciar (L: ruta F1, R: sin capa del HUD, X: audio sin fila)
extern "C" u32 host_flag(const char* name) {
    STAGE(ST_FLAG);
    if ((sBootKeys & KEY_X) && !strcmp(name, "audioformar")) return 0;
    char path[96];
    snprintf(path, sizeof(path), "sdmc:/3ds/pokestadium/%s.txt", name);
    FILE* f = fopen(path, "rb");
    if (!f) return 0xFFFFFFFFu;
    char buf[16] = {};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    char* end = nullptr;
    long v = strtol(buf, &end, 10);
    return end == buf ? 1u : (u32)v;
}
static void audio_worker_start(void) {
    LightLock_Init(&sAudPushLock);
    u32 prev = 0;
    APT_GetAppCpuTimeLimit(&prev);
    // El port de SM64 vio que 70% en el nucleo 1 corre mejor que 80% en Old 3DS (el sistema tambien usa ese nucleo).
    // nucleo.txt con un numero lo cambia.
    u32 first = host_flag("nucleo");
    if (first == 0xFFFFFFFFu || first < 40 || first > 80) first = 70;
    const u32 limits[] = { first, 70, 60, 50, 40 };
    bool ok = false;
    for (u32 l : limits) {
        if (R_SUCCEEDED(APT_SetAppCpuTimeLimit(l))) {
            u32 got = 0;
            if (R_SUCCEEDED(APT_GetAppCpuTimeLimit(&got)) && got >= 40) { logf("Audio: nucleo 1 al %lu%%\n", (unsigned long)got); ok = true; break; }
        }
    }
    if (!ok) { logf("Audio: sin nucleo 1, va en el principal\n"); return; }
    LightEvent_Init(&sAudStart, RESET_ONESHOT);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    sAudThread = threadCreate(audio_worker, nullptr, 64 * 1024, prio - 2 > 0x18 ? prio - 2 : 0x18, 1, false);
    if (!sAudThread) logf("Audio: no pude crear el hilo del nucleo 1\n");
}
// out/outSize: donde deja la tarea su sonido (s16 estereo big-endian); el nucleo 1 lo entrega al terminarla
u32 host_audio_task(const u8* task, u32 fn, const u8* out, u32 outSize) {
    STAGE(ST_AUD_SUBMIT);
    if (!sAudThread || sAudPut - sAudDone >= 6) return 0;
    sAudJobs[sAudPut & 7] = AudJob{ fn, (u32)task, out, outSize };
    __sync_synchronize();
    sAudPut = sAudPut + 1;
    __sync_synchronize();
    LightEvent_Signal(&sAudStart);
    return 1;
}
// total de tareas terminadas (el juego lleva la cuenta de las que formo)
u32 host_audio_task_state(void) {
    STAGE(ST_AUD_POLL);
    return sAudDone;
}

// ---------------- cache de texturas del interprete ----------------
// MK64 usa 256 texturas en el Old 3DS; el fondo del titulo de Stadium son 300 cuadritos de 16x16 (JPEG),
// cada uno su propia textura: con 256 se pisaban entre si (logo movido, fondo negro).
extern "C" size_t Mk64Graphics3DSTextureCacheCapacity(void) { return 512; }
// Un cuadro que necesita mas de 512 puede crecer hasta 1024 (el tope del interprete): el cruce entre dos fondos al
// elegir copa en el Stadium o el gimnasio son 600 mosaicos mas lo demas; con 512 no cabia y el juego se cerraba.
extern "C" size_t Mk64Graphics3DSTextureCacheSpillCapacity(void) { return 1024; }
// ... pero el interprete de Stadium tiene tope de 512 (TEXTURE_CACHE_MAX_SIZE), asi que pasado eso la salida es esperar
// a que el GPU acabe con lo ya mandado y reusar esas texturas en el mismo cuadro. Sin esta funcion (MK64 la pone en su
// runtime, que Stadium no usa) el interprete tiraba "working-set limit": el cuadro se cortaba (lo que faltaba no se
// dibujaba: retratos vacios) y se vaciaba todo el cache. Pasaba en la renta y en las pantallas de eleccion.
extern "C" bool Mk64Graphics3DSFenceTextureReuseBatch(void) { return sRenderer.FenceTextureReuseBatch(); }

// ---------------- partida guardada (flash de 128 KB) ----------------
#define SAVE_PATH "sdmc:/3ds/pokestadium/pokestadium.fla"
void host_save_load(u8* buf, u32 size) {
    STAGE(ST_SAVE);
    FILE* f = fopen(SAVE_PATH, "rb");
    if (!f) { logf("Partida: no hay %s (nueva)\n", SAVE_PATH); return; }
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    logf("Partida: cargada (%lu bytes)\n", (unsigned long)n);
}
void host_save_store(const u8* buf, u32 size) {
    STAGE(ST_SAVE);
    FILE* f = fopen(SAVE_PATH ".tmp", "wb");
    if (!f) { logf("Partida: no pude escribir\n"); return; }
    size_t n = fwrite(buf, 1, size, f);
    fclose(f);
    if (n == size) { remove(SAVE_PATH); rename(SAVE_PATH ".tmp", SAVE_PATH); logf("Partida: guardada\n"); }
}

// ---------------- audio: NDSP (requiere sdmc:/3ds/dspfirm.cdc, se saca con DSP1) ----------------
#define AUD_BUFS 48
#define AUD_BUF_BYTES 0x1800 // 1536 muestras: cabe un cuadro de audio al maximo (1472)
static bool sAudioOk;
static ndspWaveBuf sWave[AUD_BUFS];
static s16* sWaveData;
static int sWaveNext;
static u32 sAudioFreq;

static void audio_init(void) {
    if (R_FAILED(ndspInit())) { logf("Audio: ndspInit fallo (falta sdmc:/3ds/dspfirm.cdc? correr DSP1)\n"); return; }
    sWaveData = (s16*)linearAlloc(AUD_BUFS * AUD_BUF_BYTES);
    if (!sWaveData) { ndspExit(); return; }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = { 1.0f, 1.0f };
    ndspChnSetMix(0, mix);
    memset(sWave, 0, sizeof(sWave));
    for (int i = 0; i < AUD_BUFS; i++) { sWave[i].data_vaddr = (u8*)sWaveData + i * AUD_BUF_BYTES; sWave[i].status = NDSP_WBUF_DONE; }
    sAudioOk = true;
    logf("Audio: NDSP listo\n");
}

void host_audio_push(const u8* buf, u32 size, u32 freq) {
    STAGE(ST_AUD_PUSH);
    u64 t = svcGetSystemTick();
    if (size == 0) { if (freq) sAudioFreqSeen = freq; return; } // solo avisa la frecuencia
    if (!buf) { sAudPads++; sAudPadSamples += size / 4; } // silencio para rehacer la reserva
    LightLock_Lock(&sAudPushLock);
    audio_push_impl(buf, size, freq);
    LightLock_Unlock(&sAudPushLock);
    { const u64 d = svcGetSystemTick() - t; sTAudio += d; sPkAudio += d; }
}
// Medicion: muestras entregadas por el juego (para saber si el audio va al ritmo real) y veces que se vacio la cola
u64 sAudioSamplesPub;
#define sAudioSamples sAudioSamplesPub
static u64 sSimPushed, sSimStart; // sin NDSP (emulador sin firmware): una salida simulada que consume al ritmo real
static u32 audio_out_pending(void);
u32 host_audio_pending(void) {
    STAGE(ST_AUD_PENDING);
    // lo formado en el nucleo 1 que todavia no se entrega tambien cuenta
    u32 b = audio_out_pending();
    for (u32 k = sAudDone; k != sAudPut; k++) b += sAudJobs[k & 7].outSize;
    return b;
}
static u32 audio_out_pending(void) {
    // bytes que faltan por tocar (lo que el N64 llamaba AI_LEN): los buffers en cola completos + la mitad del que suena
    if (!sAudioOk) {
        if (!sSimStart || !sAudioFreqSeen) return 0;
        const u64 played = (svcGetSystemTick() - sSimStart) * sAudioFreqSeen / 268111856ull;
        if (played >= sSimPushed) { sSimPushed = 0; sSimStart = svcGetSystemTick(); return 0; } // se vacio: reinicia
        return (u32)((sSimPushed - played) * 4);
    }
    u32 b = 0;
    for (int i = 0; i < AUD_BUFS; i++) {
        if (sWave[i].status == NDSP_WBUF_QUEUED) b += sWave[i].nsamples * 4;
        else if (sWave[i].status == NDSP_WBUF_PLAYING) b += sWave[i].nsamples * 2;
    }
    return b;
}
static int audio_queued(void) {
    int q = 0;
    for (int i = 0; i < AUD_BUFS; i++) if (sWave[i].status == NDSP_WBUF_QUEUED || sWave[i].status == NDSP_WBUF_PLAYING) q++;
    return q;
}
static void audio_push_impl(const u8* buf, u32 size, u32 freq) {
    sAudioSamples += size / 4;
    {   // solo para el emulador (volcaraudio.txt): lo que sale a la bocina, a un archivo, para revisarlo
        static int sOn = -1;
        static FILE* sDump;
        if (sOn < 0) { sOn = host_flag("volcaraudio") == 1; if (sOn) sDump = fopen("sdmc:/3ds/pokestadium/audio.raw", "wb"); }
        if (sDump && size) {
            static u32 sBytes, sMark;
            if (buf) fwrite(buf, 1, size, sDump);
            else { static const u8 z[256] = {}; for (u32 k = 0; k < size; k += sizeof z) fwrite(z, 1, size - k < sizeof z ? size - k : sizeof z, sDump); }
            sBytes += size;
            static FILE* sIdx;
            if (!sIdx) sIdx = fopen("sdmc:/3ds/pokestadium/audio.idx", "w");
            if (sIdx && sFrames >= sMark) { sMark = sFrames + 10; fprintf(sIdx, "%lu %lu\n", (unsigned long)sFrames, (unsigned long)sBytes); fflush(sIdx); fflush(sDump); }
        }
    }
    if (!sAudioOk || size == 0) {
        if (freq) sAudioFreqSeen = freq;
        if (!sAudioOk && size) {
            if (!sSimStart || audio_out_pending() == 0) { if (sSimStart) sAudioUnder++; sSimStart = svcGetSystemTick(); sSimPushed = 0; }
            sSimPushed += size / 4;
        }
        return;
    }
    sAudioFreqSeen = freq;
    if (freq != sAudioFreq) { sAudioFreq = freq; ndspChnSetRate(0, (float)freq); }
    if (audio_queued() == 0 && sAudioFreq) sAudioUnder++;
    while (size > 0) {
        ndspWaveBuf* w = &sWave[sWaveNext];
        if (w->status != NDSP_WBUF_DONE && w->status != NDSP_WBUF_FREE) { sAudioDrops2++; return; } // cola llena: se tira
        u32 n = size > AUD_BUF_BYTES ? AUD_BUF_BYTES : size;
        // muestras big-endian del juego -> little-endian
        const u32* src = (const u32*)buf;
        u32* dst = (u32*)w->data_vaddr;
        if (!buf) memset(dst, 0, n);
        else for (u32 i = 0; i < n / 4; i++) { u32 v = src[i]; dst[i] = ((v & 0xFF00FF00u) >> 8) | ((v & 0x00FF00FFu) << 8); }
        DSP_FlushDataCache(dst, n);
        w->nsamples = n / 4;
        ndspChnWaveBufAdd(0, w);
        sWaveNext = (sWaveNext + 1) % AUD_BUFS;
        if (buf) buf += n;
        size -= n;
    }
}

// Antes de salir: registro en la pantalla de abajo hasta que se presione A (o 60 s)
static void show_report(void) {
    static PrintConsole con;
    consoleInit(GFX_BOTTOM, &con);
    int total = sRingCount;
    const int rows = 27;
    int top = total > rows ? total - rows : 0; // primera linea visible (0 = la mas vieja)
    int shown = -1;
    for (int f = 0; f < 36000 && aptMainLoop(); f++) {
        hidScanInput();
        u32 k = hidKeysDown();
        if ((f & 3) == 0) k |= hidKeysHeld() & (KEY_DUP | KEY_DDOWN); // mantener presionado = repetir
        if (k & KEY_A) break;
        if (k & KEY_DUP) top--;
        if (k & KEY_DDOWN) top++;
        if (k & KEY_L) top -= rows;
        if (k & KEY_R) top += rows;
        if (top > total - rows) top = total - rows;
        if (top < 0) top = 0;
        if (top != shown) {
            shown = top;
            printf("\x1b[2J\x1b[0;0H");
            int first = (sRingPos + LOG_RING - total) % LOG_RING;
            for (int i = 0; i < rows && top + i < total; i++) printf("%.39s\n", sRing[(first + top + i) % LOG_RING]);
            printf("\x1b[28;0H-- %d-%d de %d  arriba/abajo L/R  A=salir", top + 1, top + rows < total ? top + rows : total, total);
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

// Detener los hilos propios antes de salir: un proceso que sale con hilos vivos (audio en el nucleo 1) puede
// dejar la consola congelada al cerrar. Se esperan hasta 1 s cada uno.
static Thread sVigil;
static void stop_threads(void) {
    Thread self = threadGetCurrent();
    sAudQuit = true;
    if (sAudThread) LightEvent_Signal(&sAudStart);
    if (sAudThread && sAudThread != self) {
        const Result r = threadJoin(sAudThread, 1000000000ULL);
        logf("[host] hilo de audio: %s\n", R_SUCCEEDED(r) ? "detenido" : "no respondio");
        if (R_SUCCEEDED(r)) { threadFree(sAudThread); sAudThread = nullptr; }
    }
    if (sVigil && sVigil != self) {
        const Result r = threadJoin(sVigil, 1000000000ULL);
        logf("[host] vigilante: %s\n", R_SUCCEEDED(r) ? "detenido" : "no respondio");
        if (R_SUCCEEDED(r)) { threadFree(sVigil); sVigil = nullptr; }
    }
    svcSleepThread(250000000LL); // latidos (100 ms) y vigilante simple (1 s, sin espera)
    if (sLog) fflush(sLog);
}

static void apt_hook(APT_HookType t, void*) {
    switch (t) {
    case APTHOOK_ONSUSPEND: case APTHOOK_ONSLEEP: sAptPaused = true; logf("[apt] %s\n", t == APTHOOK_ONSUSPEND ? "HOME (suspendido)" : "dormir"); break;
    case APTHOOK_ONRESTORE: case APTHOOK_ONWAKEUP: sAptPaused = false; logf("[apt] de vuelta\n"); break;
    case APTHOOK_ONEXIT: sAptExiting = true; sAptPaused = true; logf("[apt] cerrar desde HOME\n"); break;
    default: break;
    }
    if (sLog) fflush(sLog);
}

static bool sGfxUp; // la pantalla ya se inicio (si no, show_report no tiene donde escribir)
// El GPU lleva mas de 5 s sin terminar lo que se le mando (ver el parche de frame_begin_3ds.hpp en build_3ds.sh). No se
// sale del proceso: se sigue esperando y mientras tanto se atiende HOME, para poder cerrar el juego sin apagar la
// consola. Si el GPU termina, el juego sigue. what: 1 cola del GPU, 2 presentacion, 0 ya paso (ms = lo que duro).
volatile u32 gPsStallCount, gPsStallLastMs, gPsStallNow, gPsStallWhat;
void host_exit_game(void);
volatile u32 gPsStallFixed;
volatile u32 gPsCapState, gPsCapBytes; // 0 nada, 1 armada, 2 en la SD, 3 enviada a la PC, 4 fallo el envio
extern "C" u32 PsBoxCapture(u8* out, u32 cap);
#ifdef PS_BLACKBOX
static void ps_capture_now(void) {
            // memoria lineal (hay mas libre y menos partida que en el monton normal); si no hay 3 MB, con 1
            u32 cap = 3 * 1024 * 1024;
            u8* blob = (u8*)linearAlloc(cap);
            if (!blob) { cap = 1024 * 1024; blob = (u8*)linearAlloc(cap); }
            const u32 n = blob ? PsBoxCapture(blob, cap) : 0;
            gPsCapState = blob ? 1 : 5; gPsCapBytes = n;
            if (n) {
                FILE* f = fopen("sdmc:/3ds/pokestadium/gpuhang.bin", "wb");
                if (f) { fwrite(blob, 1, n, f); fclose(f); gPsCapState = 2; }
                int sock = -1;
                if (__3dslink_host.s_addr) {
                    void* sb = memalign(0x1000, 0x80000);
                    if (sb && R_SUCCEEDED(socInit((u32*)sb, 0x80000))) sock = link3dsConnectToHost(false, false);
                }
                if (sock >= 0) {
                    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                    char line[96];
                    bool ok = true;
                    for (u32 o = 0; o < n && ok; o += 57) {
                        const u32 len = n - o < 57 ? n - o : 57;
                        int m = 0;
                        memcpy(line, "PSCAP ", 6); m = 6;
                        for (u32 q = 0; q < len; q += 3) {
                            const u32 b0 = blob[o + q], b1 = q + 1 < len ? blob[o + q + 1] : 0, b2 = q + 2 < len ? blob[o + q + 2] : 0;
                            line[m++] = kB64[b0 >> 2]; line[m++] = kB64[((b0 & 3) << 4) | (b1 >> 4)];
                            line[m++] = q + 1 < len ? kB64[((b1 & 15) << 2) | (b2 >> 6)] : '=';
                            line[m++] = q + 2 < len ? kB64[b2 & 63] : '=';
                        }
                        line[m++] = '\n';
                        for (int sent = 0; sent < m;) { const int r = send(sock, line + sent, m - sent, 0); if (r <= 0) { ok = false; break; } sent += r; }
                    }
                    const int m = snprintf(line, sizeof line, "PSCAP-FIN %lu\n", (unsigned long)n);
                    if (ok) send(sock, line, m, 0);
                    closesocket(sock);
                    gPsCapState = ok ? 3 : 4;
                }
            }
            if (blob) linearFree(blob);
}
#endif
void PsGpuStall3DS(int what, unsigned ms) {
    // Nota: no hay rescate posible desde aqui. Con el GPU colgado se cuelga tambien el modulo de video del sistema
    // (3dbrew, GPU/Internal Registers), asi que cualquier llamada a el (reiniciar el GPU, escribir registros, HOME) se
    // queda esperando para siempre.
    if (what == 0) { gPsStallLastMs = ms; gPsStallNow = 0; gPsStallFixed = gPsStallFixed + 1; return; }
#ifdef PS_BLACKBOX
    {   // Captura: la lista que colgo al GPU, con el estado y los datos de cada dibujo, se manda a la PC por la conexion
        // de 3dslink (texto en base64) y se guarda en la SD. Solo una vez.
        static bool sDone;
        if (what == 1 && ms >= 3000 && !sDone && on_main_thread()) {
            sDone = true;
            ps_capture_now();
        }
    }
#endif
    if (ms < 5000) return;
    if (!gPsStallNow) { gPsStallCount = gPsStallCount + 1; gPsStallWhat = (u32)what; }
    gPsStallNow = ms;
#if !defined(PS_BLACKBOX) || defined(PS_BOXLITE)
    {   // aviso en la pantalla de abajo (la escribe el CPU, no pasa por el GPU): cuanto lleva y como salir
        static u32 sShown = ~0u;
        const u32 sec = ms / 1000;
        if (on_main_thread() && sec != sShown && gb_screen_begin()) {
            sShown = sec;
            char t[48];
            for (int y = 0; y < 40; y++) for (int x = 0; x < 320; x++) { u8* q = sGbFb + ((x * 240) + (239 - (100 + y))) * 3; q[0] = 0; q[1] = 0; q[2] = 0; }
            snprintf(t, sizeof t, "GPU busy: %lu s (%s)%s%s%s", (unsigned long)sec, what == 2 ? "screen" : "draw",
                     (gPsSkipDraw3DS & 1u) ? " -huesos" : "", (gPsSkipDraw3DS & 2u) ? " -nativa" : "", (gPsSkipDraw3DS & 4u) ? " -planos" : "");
            gb_text(60, 106, t, 0xFFFFFF);
            gb_text(60, 122, "Power off to exit", 0xFFD040);
            gfxFlushBuffers();
            gfxScreenSwapBuffers(GFX_BOTTOM, false);
        }
    }
#endif
    static u32 sPump;
    if ((sPump++ % 50) == 0 && on_main_thread() && !aptMainLoop()) { sExitReason = "aptMainLoop"; host_exit_game(); }
}
// Red de seguridad antes de cada subida de uniformes al GPU: 3dbrew (GPU/Internal Registers) documenta que un parametro
// invalido como un NaN cuelga al GPU y con el al modulo de video del sistema. En la consola se vio con infinitos que
// venian de una textura de lado 0 (1/0). Cualquier componente que no sea finito se pone en 0 y se cuenta.
extern "C" {
// triangulos que llegan al GPU en el cuadro (todos los caminos de dibujo pasan por aqui)
volatile u32 gPsTriNow3DS, gPsTriLast3DS;
volatile u32 gPsSkipDraw3DS; // tipos de dibujo apagados (prueba del cuelgue)
void __real_C3D_DrawArrays(GPU_Primitive_t primitive, int first, int size);
static inline bool ps_skip_draw() {
    const u32 m = gPsSkipDraw3DS;
    if (!m) return false;
    const u16 bu = C3D_BoolUnifs[GPU_VERTEX_SHADER];
    if ((m & 1u) && (bu & 0x20)) return true;                 // modelos con huesos
    if ((m & 2u) && (bu & 0x01) && !(bu & 0x20)) return true; // geometria nativa
    if ((m & 4u) && !(bu & 0x21)) return true;                // dibujos planos
    return false;
}
void __wrap_C3D_DrawArrays(GPU_Primitive_t primitive, int first, int size) { if (ps_skip_draw()) return; gPsTriNow3DS = gPsTriNow3DS + (u32)size / 3; __real_C3D_DrawArrays(primitive, first, size); }
void __real_C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void* indices);
void __wrap_C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void* indices) { if (ps_skip_draw()) return; gPsTriNow3DS = gPsTriNow3DS + (u32)count / 3; __real_C3D_DrawElements(primitive, count, type, indices); }
volatile u32 gPsBadUnif3DS, gPsBadUnifReg3DS;
// Red de seguridad (todas las builds): un gSPVertex con direccion fuera de la memoria del juego se salta en vez de
// tronar la consola (abort de datos). Se guarda la direccion y el comando para la caja negra.
extern u32 __ctru_heap, __ctru_linear_heap;
extern char __end__[];
volatile u32 gPsBadPtrN3DS, gPsBadPtr3DS, gPsBadPtrCmd3DS, gPsBadPtrW0_3DS;
int PsPtrOk3DS(const void* p, u32 len) {
    const u32 a = (u32)p, e = a + len;
    if (!p || e < a) return 0;
    if (a >= BLOB_BASE && e <= BLOB_BASE + BLOB_SIZE) return 1;
    if (a >= __ctru_heap && e <= __ctru_heap + __ctru_heap_size) return 1;
    if (a >= __ctru_linear_heap && e <= __ctru_linear_heap + __ctru_linear_heap_size) return 1;
    if (a >= 0x00100000u && e <= (u32)__end__) return 1;
    return 0;
}
volatile u32 gPsBadStk3DS[6], gPsBadStkN3DS, gPsBadStkRes3DS, gPsBadStkSet3DS;
void PsBadStk3DS(const u32* stk, u32 depth, u32 res) {
    if (gPsBadStkSet3DS) return; // se queda la primera vez: es la que dice de donde viene
    for (u32 i = 0; i < 6; i++) gPsBadStk3DS[i] = stk[i];
    gPsBadStkN3DS = depth; gPsBadStkRes3DS = res; gPsBadStkSet3DS = 1;
#ifdef PS_BLACKBOX
    // volcado para analizar en la PC: cabecera (pila) + 64 KB alrededor de la lista donde se encontro
    if (FILE* f = fopen("sdmc:/3ds/pokestadium/dirmala.bin", "wb")) {
        u32 h[16] = { 0x50534D4Cu, gPsBadPtr3DS, gPsBadPtrCmd3DS, gPsBadPtrW0_3DS, stk[0], stk[1], stk[2], stk[3], stk[4], stk[5], depth, 0, 0, 0, 0, 0 };
        u32 a = (gPsBadPtrCmd3DS & ~0xFFFu) - 0x8000u;
        if (a < __ctru_heap) a = __ctru_heap;
        if (a + 0x10000u > __ctru_heap + __ctru_heap_size) a = __ctru_heap + __ctru_heap_size - 0x10000u;
        if (!(gPsBadPtrCmd3DS >= __ctru_heap && gPsBadPtrCmd3DS < __ctru_heap + __ctru_heap_size)) a = gPsBadPtrCmd3DS & ~0xFFFu;
        h[11] = a;
        fwrite(h, 4, 16, f);
        fwrite((const void*)a, 1, 0x10000, f);
        fclose(f);
    }
#endif
}
void PsBadPtr3DS(const void* p, const void* cmd, u32 w0) {
    gPsBadPtrN3DS = gPsBadPtrN3DS + 1; gPsBadPtr3DS = (u32)p; gPsBadPtrCmd3DS = (u32)cmd; gPsBadPtrW0_3DS = w0;
}
void __real_C3D_UpdateUniforms(GPU_SHADER_TYPE type);
void __wrap_C3D_UpdateUniforms(GPU_SHADER_TYPE type) {
    for (int i = 0; i < C3D_FVUNIF_COUNT; i++) {
        if (!C3D_FVUnifDirty[type][i]) continue;
        float* c = C3D_FVUnif[type][i].c;
        for (int k = 0; k < 4; k++) {
            u32 u; memcpy(&u, &c[k], 4);
            if ((u & 0x7F800000u) == 0x7F800000u) { c[k] = 0.0f; gPsBadUnif3DS = gPsBadUnif3DS + 1; gPsBadUnifReg3DS = (u32)i; }
        }
    }
    __real_C3D_UpdateUniforms(type);
}
} // extern "C"
void host_exit_game(void) {
    static volatile int sExiting;
    if (__sync_lock_test_and_set(&sExiting, 1)) { for (;;) svcSleepThread(1000000000LL); } // otro hilo ya esta saliendo
    if (on_main_thread()) stage_set(ST_EXIT);
    sWatchQuit = true;
#ifdef PS_BLACKBOX
    if (strcmp(sExitReason, "aptMainLoop") != 0 && strcmp(sExitReason, "excepcion") != 0) {
        // la caja negra: por que se salio el juego, en la pantalla de abajo, hasta que apaguen la consola
        char b[64];
        struct mallinfo mi = mallinfo();
        const u32 freeB = (__ctru_heap_size > (u32)mi.arena ? __ctru_heap_size - (u32)mi.arena : 0) + (u32)mi.fordblks;
        ps_box_fatal_begin();
        ps_box_fatal_line("EL JUEGO SE SALIO");
        snprintf(b, sizeof b, "%.60s", sExitReason); ps_box_fatal_line(b);
        snprintf(b, sizeof b, "cuadro %lu", (unsigned long)sFrames); ps_box_fatal_line(b);
        snprintf(b, sizeof b, "libre %lu KB lineal %lu KB", (unsigned long)(freeB / 1024), (unsigned long)(linearSpaceFree() / 1024)); ps_box_fatal_line(b);
        snprintf(b, sizeof b, "texturas %lu gpu %lu", (unsigned long)sTexFaults, (unsigned long)sGpuFaults); ps_box_fatal_line(b);
        snprintf(b, sizeof b, "ROM japonesa: %s", sRomJp ? "abierta" : "NO"); ps_box_fatal_line(b);
        ps_box_fatal_line("Toma foto y apaga la consola");
        for (;;) svcSleepThread(1000000000LL);
    }
#endif
    // lanzado con "3dslink -s": los pasos del cierre se ven en la terminal de la PC. En un fallo no se toca la red:
    // el registro se muestra en la pantalla de abajo.
    const bool crashed = !strcmp(sExitReason, "excepcion");
    if (!crashed) link_diag_init();
    logf("[host] salir: %s (frames %lu)\n", sExitReason, (unsigned long)sFrames);
    {   // cuanta memoria quedaba: si faltaba, hay modelos o texturas que no se dibujaron
        struct mallinfo mi = mallinfo();
        const u32 freeB = (__ctru_heap_size > (u32)mi.arena ? __ctru_heap_size - (u32)mi.arena : 0) + (u32)mi.fordblks;
        logf("[host] memoria libre al salir: %lu KB (lineal %lu KB), cuadros incompletos por texturas: %lu, por comandos del GPU: %lu\n", (unsigned long)(freeB / 1024), (unsigned long)(linearSpaceFree() / 1024), (unsigned long)sTexFaults, (unsigned long)sGpuFaults);
        logf("[host] retraso del sonido contra la imagen: %lu ms en promedio, %lu ms maximo (%lu medidas)\n", (unsigned long)(sLatTot[1] ? sLatTot[0] / sLatTot[1] : 0), (unsigned long)sLatTot[2], (unsigned long)sLatTot[1]);
    }
    if (sLog) fflush(sLog);
    stop_threads();
    logf("[host] hilos detenidos\n");
    if (sTexFaults) logf("[tex] cuadros incompletos por demasiadas texturas: %lu\n", (unsigned long)sTexFaults);
    if (on_main_thread()) { gb_flush("al salir"); gb_close(); } // la partida de Game Boy, si quedo algo sin guardar
    gb_summary();
    diag_summary(sExitReason);
    logf("[host] partidas y resumen listos\n");
    if (sAudioOk) { ndspExit(); sAudioOk = false; }
    logf("[host] sonido cerrado\n");
    if (sLog) fflush(sLog);
    // registro en pantalla solo en diagnostico y si la app sigue al frente (cerrar desde HOME: salir ya)
    if (kLogs && (kDiag || crashed) && !sAptExiting && strcmp(sExitReason, "aptMainLoop") != 0) show_report();
    // Soltar la pantalla antes de salir: sin gfxExit el sistema no recupera el GPU y el cierre desde HOME (o la vuelta
    // al Homebrew Launcher) se queda colgado. C3D_Fini no hace nada si citro3d no se llego a iniciar.
    // Al cerrar desde HOME ya no se tiene el GPU: C3D_Fini esperaria para siempre a que termine lo que quedo en cola.
    if (sGfxUp) { if (!sAptExiting) C3D_Fini(); logf("[host] cerrando pantalla\n"); if (sLog) fflush(sLog); gfxExit(); sGfxUp = false; }
    logf("[host] fin\n");
    if (sRom) fclose(sRom);
    if (sRomJp) fclose(sRomJp);
    if (sLog) fclose(sLog);
    if (ps_romfs()) romfsExit();
    if (sLinkSock >= 0) { const char* m = "[host] saliendo\n"; send(sLinkSock, m, strlen(m), 0); close(sLinkSock); sLinkSock = -1; socExit(); }
    _exit(0); // sin pasar por los destructores globales (el interprete grafico tocaria un GPU que ya no se tiene)
}

// Assets: PERS-SZP (Yay0 + parches) descomprimido aqui; lo demas crudo. Todo queda en big-endian.
static std::vector<u8> sResBuf;
static void yay0(const u8* src, u8* dst, u32 avail) {
    u32 size = be32(src + 4);
    const u8* link = src + be32(src + 8);
    const u8* chunk = src + be32(src + 12);
    const u8* maskp = src + 16;
    u8* end = dst + size;
    u32 mask = 0;
    int bits = 0;
    (void)avail;
    while (dst < end) {
        if (bits == 0) { mask = be32(maskp); maskp += 4; bits = 32; }
        if (mask & 0x80000000u) {
            *dst++ = *chunk++;
        } else {
            u32 l = ((u32)link[0] << 8) | link[1];
            link += 2;
            u32 dist = (l & 0xFFF) + 1, n = l >> 12;
            n = n ? n + 2 : (u32)(*chunk++) + 0x12;
            u8* from = dst - dist;
            while (n-- && dst < end) *dst++ = *from++;
        }
        mask <<= 1;
        bits--;
    }
}
// outSize apunta a memoria del juego (big-endian): se escribe con bswap
static const void* res_get_impl(u32 rom, u32 size, u32 type, u32* outSize);
const void* host_res_get(u32 rom, u32 size, u32 type, u32* outSizeBE) {
    STAGE(ST_RES);
    u32 n = 0;
    u64 t = svcGetSystemTick();
    const void* p = res_get_impl(rom, size, type, &n);
    { const u64 d = svcGetSystemTick() - t; sTRes += d; sPkRes += d; }
    sPkResN++;
    *outSizeBE = __builtin_bswap32(n);
    return p;
}
// Cache de assets ya preparados (LRU). Antes solo se recordaba el ultimo: el audio pide muestras por DMA
// cada cuadro (otro asset), asi que un modelo que se carga por pedazos mientras suena musica volvia a leer
// de la SD (y a descomprimir) el asset completo en cada pedazo. El juego siempre copia lo que recibe
// (ps_assets.c), asi que se puede devolver la copia guardada. Solo se guarda si queda heap de sobra.
// Bandera assetcache.txt = KB del cache (0 lo apaga). Por defecto 2048.
struct ResCacheEnt { u32 rom, size, type, out, used; u8* data; };
#define RESC_N 48u
static ResCacheEnt sResC[RESC_N];
static u32 sResCTick, sResCBudget = 2048u * 1024u;
static bool heap_has_room(u32 bytes) {
    // newlib: lo pedido al sistema (arena) sale del heap fijo de __ctru_heap_size; lo libre es lo que
    // falta por pedir mas lo libre dentro de la arena. Se exige dejar 3 MB para libultraship y texturas.
    struct mallinfo mi = mallinfo();
    const u32 freeB = (__ctru_heap_size > (u32)mi.arena ? __ctru_heap_size - (u32)mi.arena : 0) + (u32)mi.fordblks;
    return freeB > bytes + 3u * 1024u * 1024u;
}
static void resc_drop(u32 i) {
    free(sResC[i].data);
    sResCBytes -= sResC[i].out;
    sResC[i] = ResCacheEnt{};
    sResCEvict++;
}
static const u8* resc_find(u32 rom, u32 size, u32 type, u32* out) {
    for (u32 i = 0; i < RESC_N; i++)
        if (sResC[i].data && sResC[i].rom == rom && sResC[i].size == size && sResC[i].type == type) {
            sResC[i].used = ++sResCTick;
            sResCHits++;
            *out = sResC[i].out;
            return sResC[i].data;
        }
    return nullptr;
}
static void resc_add(u32 rom, u32 size, u32 type, const u8* data, u32 out) {
    if (!sResCBudget || !out || out > sResCBudget / 4) return;
    for (;;) { // desalojar lo menos usado hasta que quepa y haya lugar
        u32 freeSlot = RESC_N, lru = RESC_N, oldest = ~0u;
        for (u32 i = 0; i < RESC_N; i++) {
            if (!sResC[i].data) { if (freeSlot == RESC_N) freeSlot = i; continue; }
            if (sResC[i].used < oldest) { oldest = sResC[i].used; lru = i; }
        }
        if (freeSlot != RESC_N && sResCBytes + out <= sResCBudget) {
            if (!heap_has_room(out)) return;
            u8* p = (u8*)malloc(out);
            if (!p) return;
            memcpy(p, data, out);
            sResC[freeSlot] = ResCacheEnt{ rom, size, type, out, ++sResCTick, p };
            sResCBytes += out;
            sResCAdds++;
            return;
        }
        if (lru == RESC_N) return;
        resc_drop(lru);
    }
}
// De donde es una direccion (caja negra): J = memoria del juego, A = copia de un asset (con su ROM), b = bufer del
// ultimo asset, L = lineal, H = heap, E = el ejecutable, ? = nada
extern "C" char PsAddrTag3DS(u32 a, u32* rom) {
    *rom = 0;
    if (a >= BLOB_BASE && a < BLOB_BASE + BLOB_SIZE) return 'J';
    for (u32 i = 0; i < RESC_N; i++)
        if (sResC[i].data && a >= (u32)sResC[i].data && a < (u32)sResC[i].data + sResC[i].out) { *rom = sResC[i].rom; return 'A'; }
    if (!sResBuf.empty() && a >= (u32)sResBuf.data() && a < (u32)sResBuf.data() + sResBuf.size()) return 'b';
    if (a >= __ctru_linear_heap && a < __ctru_linear_heap + __ctru_linear_heap_size) return 'L';
    if (a >= __ctru_heap && a < __ctru_heap + __ctru_heap_size) return 'H';
    if (a >= 0x00100000u && a < 0x00700000u) return 'E';
    return '?';
}
static void res_note_miss(u32 rom, u32 out) {
    sPkResMiss++;
    sPkResKB += out / 1024;
    const u32 kb = out / 1024;
    for (int i = 0; i < 3; i++)
        if (kb > sPkResTopKB[i]) {
            for (int j = 2; j > i; j--) { sPkResTop[j] = sPkResTop[j - 1]; sPkResTopKB[j] = sPkResTopKB[j - 1]; }
            sPkResTop[i] = rom; sPkResTopKB[i] = kb;
            break;
        }
}
static const void* res_get_impl(u32 rom, u32 size, u32 type, u32* outSize) {
    // El juego pide el mismo asset por pedazos (DMA): no volver a descomprimir
    static u32 lastRom = ~0u, lastSize, lastType, lastOut;
    if (rom == lastRom && size == lastSize && type == lastType) { *outSize = lastOut; return sResBuf.data(); }
    if (const u8* c = resc_find(rom, size, type, outSize)) return c;
    lastRom = ~0u;
    std::vector<u8> raw(size);
    if (host_rom_read(rom, raw.data(), size) != size) { *outSize = 0; return nullptr; }
    if (type == 1 && size >= 0x18 && memcmp(raw.data(), "PERS-SZP", 8) == 0) {
        u32 hsize = be32(&raw[8]), size2 = be32(&raw[16]), patches = be32(&raw[20]);
        u32 size1 = be32(&raw[hsize + 4]);
        sResBuf.assign((size1 > size2 ? size1 : size2) + 16, 0);
        yay0(&raw[hsize], sResBuf.data(), size - hsize);
        for (u32 i = 0; i < patches; i++) {
            u32 at = be32(&raw[28 + i * 8]);
            memcpy(&sResBuf[at], &raw[24 + i * 8], 4); // el valor ya esta en big-endian
        }
        *outSize = size2;
    } else {
        sResBuf.swap(raw);
        *outSize = size;
    }
    lastRom = rom; lastSize = size; lastType = type; lastOut = *outSize;
    res_note_miss(rom, *outSize);
    resc_add(rom, size, type, sResBuf.data(), *outSize);
    return sResBuf.data();
}

// trampolines (tramp.s): el juego los llama en modo BE
void tr_print(void); void tr_ticks_lo(void); void tr_ticks_hi(void); void tr_sleep_us(void); void tr_pump(void);
void tr_read_pad(void); void tr_gfx_task(void); void tr_exit_game(void); void tr_res_get(void); void tr_rom_read(void);
void tr_audio_push(void); void tr_save_load(void); void tr_save_store(void);
void tr_audio_task(void); void tr_audio_task_state(void); void tr_audio_pending(void); void tr_flag(void); void tr_mem_event(void);
void tr_gb_present(void); void tr_gb_rw(void); void tr_gb_power(void);
void ps_call_blob(u32 entry, u32 arg0);

// ---------------- excepciones: llamada a codigo MIPS de un asset -> interprete del bloque ----------------
void ps_exc_entry(void);
void ps_exc_resume(CpuRegisters* r) __attribute__((noreturn));
void ps_exc_resume_be(CpuRegisters* r) __attribute__((noreturn));
// El camino de salida (registro, red de 3dslink, cierre de pantalla) corre sobre esta pila cuando el fallo no tiene
// arreglo: con 16 KB se desbordaba y el fallo terminaba en la pantalla de error del sistema, sin registro.
static u8 sExcStack[0x20000] __attribute__((aligned(8)));
#ifdef PS_BLACKBOX
static void ps_box_stk_lines(void) {
    char b[64];
    snprintf(b, sizeof b, "primera dir mala, pila de listas (%lu):", (unsigned long)gPsBadStkN3DS); ps_box_fatal_line(b);
    for (u32 i = 0; i < 6 && i < gPsBadStkN3DS; i++) { u32 rom; const char t = PsAddrTag3DS(gPsBadStk3DS[i], &rom);
        snprintf(b, sizeof b, " %08lX %c%s rom %06lX", (unsigned long)gPsBadStk3DS[i], t, (gPsBadStkRes3DS >> i) & 1 ? " recurso" : "", (unsigned long)rom); ps_box_fatal_line(b); }
}
#endif
void ps_exc_handler(ERRF_ExceptionInfo* e, CpuRegisters* r) {
    u32 pc = r->pc;
    bool notNative = (pc >= BLOB_TEXT_END && pc < BLOB_BASE + BLOB_SIZE) || pc >= 0x80000000u;
    // En el 3DS la memoria de datos no es ejecutable (prefetch abort); en emuladores puede llegar como
    // instruccion indefinida al ejecutar el codigo MIPS: mismo tratamiento.
    if ((e->type == ERRF_EXCEPTION_PREFETCH_ABORT || e->type == ERRF_EXCEPTION_UNDEFINED) && notNative) {
        sExcTraps++;
        if (kLogs && sExcTraps <= 6) logf("[trampa] codigo MIPS en %08lX, llamado desde %08lX (cuadro %lu)\n", (unsigned long)pc, (unsigned long)r->lr, (unsigned long)sFrames);
        r->r[12] = pc;
        r->pc = BLOB_MIPS_TRAP;
        ps_exc_resume(r);
    }
    // Lecturas/escrituras a la RAM baja del N64 (0x80000000.. o apuntadores casi nulos): como en el N64,
    // las lecturas regresan 0 y las escrituras se ignoran. Solo instrucciones ARM del juego.
    if (e->type == ERRF_EXCEPTION_DATA_ABORT && pc >= BLOB_BASE && pc < BLOB_TEXT_END &&
        (e->far < 0x10000u || e->far >= 0x80000000u)) {
        u32 ins = *(const u32*)pc; // las instrucciones BE8 se guardan en little-endian
        bool load = (ins >> 20) & 1;
        u32 rd = (ins >> 12) & 0xF;
        u32 cls = (ins >> 25) & 7;
        bool handled = false;
        if (cls == 2 || cls == 3) handled = true;                                  // LDR/STR/LDRB/STRB
        else if (cls == 0 && (ins & 0x90) == 0x90 && (ins & 0x60)) {               // LDRH/STRH/LDRSB/LDRSH/LDRD/STRD
            handled = true;
            u32 sh = (ins >> 5) & 3;
            if (!load && sh == 2) { load = true; if (rd < 12) r->r[rd + 1] = 0; } // LDRD
        } else if (cls == 6) { handled = true; load = false; }                     // VLDR/VSTR: se deja el registro VFP
        if (handled) {
            sExcLowRam++;
            static u32 sLowRamLogs;
            if (sLowRamLogs < 8) { sLowRamLogs++; logf("[lowram] %s %08lX (pc %08lX)\n", load ? "lectura" : "escritura", e->far, pc); }
            if (load && rd < 13) r->r[rd] = 0;
            r->pc = pc + 4;
            ps_exc_resume_be(r);
        }
    }
    sExitReason = "excepcion";
#ifdef PS_BLACKBOX
    {   // la caja negra: el fallo se queda en la pantalla de abajo (sin red ni archivos) hasta que apaguen la consola
        char b[64];
        ps_box_fatal_begin();
        snprintf(b, sizeof b, "FALLO tipo %d cuadro %lu", e->type, (unsigned long)sFrames); ps_box_fatal_line(b);
        snprintf(b, sizeof b, "PC %08lX LR %08lX", (unsigned long)r->pc, (unsigned long)r->lr); ps_box_fatal_line(b);
        snprintf(b, sizeof b, "SP %08lX FAR %08lX FSR %08lX", (unsigned long)r->sp, (unsigned long)e->far, (unsigned long)e->fsr); ps_box_fatal_line(b);
        for (int i = 0; i < 12; i += 3) { snprintf(b, sizeof b, "r%d %08lX r%d %08lX r%d %08lX", i, (unsigned long)r->r[i], i + 1, (unsigned long)r->r[i + 1], i + 2, (unsigned long)r->r[i + 2]); ps_box_fatal_line(b); }
        snprintf(b, sizeof b, "r12 %08lX  juego %08lX-%08lX", (unsigned long)r->r[12], (unsigned long)BLOB_BASE, (unsigned long)BLOB_TEXT_END); ps_box_fatal_line(b);
        for (u32 k = 0; k < 4; k++) { const u32 q = (gPsBoxCmdN - 1 - k) & 3;
            snprintf(b, sizeof b, "dl %08lX %08lX %08lX", (unsigned long)gPsBoxCmd[q][0], (unsigned long)gPsBoxCmd[q][1], (unsigned long)gPsBoxCmd[q][2]); ps_box_fatal_line(b); }
        snprintf(b, sizeof b, "dir mala %lu: %08lX cmd %08lX", (unsigned long)gPsBadPtrN3DS, (unsigned long)gPsBadPtr3DS, (unsigned long)gPsBadPtrCmd3DS); ps_box_fatal_line(b);
        { u32 rom; const char t = PsAddrTag3DS(gPsBoxCmd[(gPsBoxCmdN - 1) & 3][0], &rom); snprintf(b, sizeof b, "lista actual: %c rom %06lX", t, (unsigned long)rom); ps_box_fatal_line(b); }
        ps_box_stk_lines();
        const u32 spv = r->sp;
        int shown = 0, n = snprintf(b, sizeof b, "pila:");
        if (spv >= 0x00100000u && spv < 0x40000000u && !(spv & 3)) {
            const u32* sp = (const u32*)spv;
            for (int i = 0; i < 1024 && shown < 18; i++) {
                const u32 v = __builtin_bswap32(sp[i]);
                if (v >= BLOB_BASE && v < BLOB_TEXT_END) {
                    n += snprintf(b + n, sizeof b - n, " %08lX", (unsigned long)v); shown++;
                    if (shown % 3 == 0) { ps_box_fatal_line(b); n = snprintf(b, sizeof b, "     "); }
                }
            }
        }
        if (shown % 3) ps_box_fatal_line(b);
        ps_box_fatal_line("Toma foto y apaga la consola");
        for (;;) svcSleepThread(1000000000LL);
    }
#endif
    logf("[host] EXCEPCION tipo %d cuadro %lu: PC %08lX LR %08lX SP %08lX FAR %08lX FSR %08lX\n", e->type, (unsigned long)sFrames, r->pc, r->lr, r->sp, e->far, e->fsr);
    logf("[host] r0 %08lX r1 %08lX r2 %08lX r3 %08lX r4 %08lX r5 %08lX r6 %08lX r7 %08lX\n", r->r[0], r->r[1], r->r[2], r->r[3], r->r[4], r->r[5], r->r[6], r->r[7]);
    logf("[host] r8 %08lX r9 %08lX r10 %08lX r11 %08lX r12 %08lX | bloque %08lX a %08lX\n", r->r[8], r->r[9], r->r[10], r->r[11], r->r[12], (unsigned long)BLOB_BASE, (unsigned long)BLOB_TEXT_END);
    {
        // direcciones de regreso que hay en la pila del juego (solo si la pila es memoria del programa)
        char line[256]; int n = snprintf(line, sizeof line, "[host] pila:");
        const u32 spv = r->sp;
        if (spv >= 0x00100000u && spv < 0x40000000u && !(spv & 3)) {
            const u32* sp = (const u32*)spv;
            for (int i = 0, shown = 0; i < 512 && shown < 16 && n < 230; i++) {
                u32 v = __builtin_bswap32(sp[i]);
                if (v >= BLOB_BASE && v < BLOB_TEXT_END) { n += snprintf(line + n, sizeof line - n, " %08lX", (unsigned long)v); shown++; }
            }
        }
        logf("%s\n", line);
    }
    if (sLog) fflush(sLog);
    host_exit_game();
}
} // extern "C"

// ---------------- vigilante: si el juego deja de dibujar, decir donde se quedo ----------------
extern "C" { extern u32 ps_last_game_sp, ps_last_game_lr; }
static void watchdog(void*) {
    u32 last = ~0u, still = 0;
    while (!sWatchQuit) {
        svcSleepThread(1000000000LL);
        if (sFrames != last) { last = sFrames; still = 0; continue; }
        if (sAptPaused || sAptExiting) { still = 0; continue; } // en HOME: no es bloqueo
        if (++still < 25) continue; // las primeras cargas pueden tardar
        logf("\n*** el juego no avanza (frame %lu)\nultima llamada al host desde %08lX\npila:", (unsigned long)sFrames,
             (unsigned long)ps_last_game_lr);
        const u32* sp = (const u32*)ps_last_game_sp;
        for (int i = 0, shown = 0; sp && i < 1024 && shown < 20; i++) {
            u32 v = __builtin_bswap32(sp[i]);
            if (v >= BLOB_BASE && v < BLOB_TEXT_END) { logf(" %08lX", (unsigned long)v); shown++; }
        }
        logf("\n");
        sExitReason = "atorado";
        host_exit_game();
    }
}

static bool blobPtr(u32 a) { return a >= BLOB_BASE && a < BLOB_BASE + BLOB_SIZE; }
// Diagnostico: vigilante en el nucleo 1 con mas prioridad que el audio. No usa stdout ni depende del hilo
// principal: escribe el registro en la SD cada 5 s, un pulso cada 10 s, y si el juego deja de dibujar 5 s,
// el estado de cada hilo (a los 5, 15 y 30 s, y luego cada 30 s mientras siga trabado).
static u32 be_load(u32 addr) { return __builtin_bswap32(*(const volatile u32*)addr); }
static void beat_hi(void*) { while (!sWatchQuit) { sBeatHi++; svcSleepThread(100000000LL); } }
static void beat_lo(void*) { while (!sWatchQuit) { sBeatLo++; svcSleepThread(100000000LL); } }
static void vigil_state(char* b, size_t n, u32 stillMs, u32 seq0, u32 hi0, u32 lo0) {
    const u32 now = (u32)(svcGetSystemTick() >> 18);
    const u32 st = sStMain < ST_COUNT ? sStMain : 0;
    const u32 cur = blobPtr(BLOB_CUR_THREAD) ? be_load(BLOB_CUR_THREAD) : 0;
    const int fid = blobPtr(cur) ? (int)be_load(cur + 0x14) : -1;
    const u32 wait = be_load(BLOB_DIAG_WAIT);
    snprintf(b, n,
             "[bloqueo] %lu s sin dibujar (cuadro %lu)\n"
             "[bloqueo] principal: %s desde hace %lu ms, %s (%lu cambios de etapa en este lapso)\n"
             "[bloqueo] juego: hilo N64 %d, espera de audio %lu, ultima llamada al host desde %08lX, pila %08lX\n"
             "[bloqueo] nucleo 1 (audio): %s desde hace %lu ms, %lu tareas, estado %lu\n"
             "[bloqueo] nucleo 0: latido alto %s, latido bajo %s | host desde otros hilos %lu | NDSP en cola %d\n",
             (unsigned long)(stillMs / 1000), (unsigned long)sFrames, kStageName[st], (unsigned long)(now - sStMainTick),
             sStMainSeq != seq0 ? "se mueve" : "quieto", (unsigned long)(sStMainSeq - seq0), fid, (unsigned long)wait,
             (unsigned long)ps_last_game_lr, (unsigned long)ps_last_game_sp, sStAud ? "sintetizando" : "esperando tarea",
             (unsigned long)(now - sStAudTick), (unsigned long)sStAudTasks, (unsigned long)sAudState,
             sBeatHi != hi0 ? "vivo" : "PARADO", sBeatLo != lo0 ? "vivo" : "PARADO", (unsigned long)sHostOtherThread, audio_queued());
}
static void vigil_stack(char* b, size_t n) {
    int k = snprintf(b, n, "[bloqueo] pila del juego:");
    const u32 sp = ps_last_game_sp;
    for (int i = 0, shown = 0; blobPtr(sp) && i < 1024 && shown < 24 && k < (int)n - 12; i++) {
        const u32 v = be_load(sp + i * 4);
        if (v >= BLOB_BASE && v < BLOB_TEXT_END) { k += snprintf(b + k, n - k, " %08lX", (unsigned long)v); shown++; }
    }
    snprintf(b + k, n - k, "\n");
}
static void vigil_screen(const char* report) {
    static PrintConsole con;
    consoleInit(GFX_BOTTOM, &con);
    printf("\x1b[2J\x1b[0;0H*** BLOQUEO (diagnostico) ***\n");
    for (const char* p = report; *p;) { // lineas cortadas a 40 columnas
        const char* e = strchr(p, '\n');
        const int len = e ? (int)(e - p) : (int)strlen(p);
        printf("%.40s\n", p);
        if (len > 40) printf("%.40s\n", p + 40);
        p += len + (e ? 1 : 0);
    }
    printf("log.txt ya se guardo. Apaga la consola.");
    gfxFlushBuffers();
    gfxScreenSwapBuffers(GFX_BOTTOM, false);
}
#ifdef PS_BLACKBOX
// ---------------- caja negra en la pantalla de abajo (PS_BLACKBOX=1) ----------------
// El vigilante (nucleo 1) la redibuja cada 250 ms con escrituras directas del CPU al framebuffer de abajo, sin
// GPU ni servicios: si la consola se congela por completo, la pantalla de abajo se queda con lo ultimo que vio.
#include "blackbox_font.h"
static u8* sBoxFb;
static u32 sBoxBeat, sBoxBadSeen, sBoxNanSeen;
static void box_init() {
    u16 w = 0, h = 0;
    u8* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
    if (!fb || w != 240 || h != 320) { logf("[caja] pantalla de abajo no disponible (%u x %u)\n", w, h); return; }
    memset(fb, 0, 240 * 320 * 3);
    svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)fb, 240 * 320 * 3);
    gfxScreenSwapBuffers(GFX_BOTTOM, false); // ese buffer queda a la vista; nadie mas cambia la pantalla de abajo
    sBoxFb = fb;
    logf("[caja] caja negra en la pantalla de abajo; GPU validado en cada lista\n");
}
static void box_text(int col, int row, const char* t, u8 r, u8 g, u8 b) {
    for (; *t && col < 53; t++, col++) {
        const unsigned ch = (unsigned char)*t;
        const unsigned char* gl = kBoxFont[(ch >= 32 && ch < 127 ? ch : '?') - 32];
        for (int y = 0; y < 10; y++) {
            const int py = row * 10 + y;
            if (py >= 240) return;
            for (int x = 0; x < 6; x++) {
                const int px = col * 6 + x;
                u8* p = sBoxFb + ((px * 240) + (239 - py)) * 3;
                const bool on = (gl[y] >> x) & 1;
                p[0] = on ? b : 0; p[1] = on ? g : 0; p[2] = on ? r : 0; // BGR8
            }
        }
    }
    for (; col < 53; col++) // resto del renglon en negro
        for (int y = 0; y < 10; y++) for (int x = 0; x < 6; x++) {
            u8* p = sBoxFb + (((col * 6 + x) * 240) + (239 - (row * 10 + y))) * 3;
            p[0] = p[1] = p[2] = 0;
        }
}
static int sBoxFatalRow;
static volatile bool sBoxFatal;
static void ps_box_fatal_begin(void) {
    sBoxFatal = true; // el hilo de la caja deja de dibujar
    svcSleepThread(50000000LL);
    if (sBoxFb) memset(sBoxFb, 0, 240 * 320 * 3);
    sBoxFatalRow = 0;
}
static void ps_box_fatal_line(const char* t) {
    if (!sBoxFb) return;
    box_text(0, sBoxFatalRow++, t, 255, sBoxFatalRow == 1 ? 80 : 255, sBoxFatalRow == 1 ? 80 : 255);
    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)sBoxFb, 240 * 320 * 3);
}
static const char* box_kind(u8 k) { return k == BOX_LIST ? "LISTA" : k == BOX_XFER ? "TRANSF" : k == BOX_COPY ? "COPIA" : k == BOX_FILL ? "RELLENO" : "?"; }
extern "C" { extern volatile uint32_t gPsBoxCmd[4][3]; extern volatile uint32_t gPsBoxCmdN; }
extern "C" { extern gxCmdQueue_s* volatile gBoxQueue; }
extern "C" { extern volatile unsigned gPsBadVtx3DS; }
extern "C" { extern volatile u32 gPsBadUnif3DS, gPsBadUnifReg3DS, gPsBadPtrN3DS, gPsBadPtr3DS, gPsBadPtrCmd3DS, gPsBadPtrW0_3DS; extern volatile uint32_t gPsBoxCmd[4][3]; extern volatile uint32_t gPsBoxCmdN; }
extern "C" { extern volatile unsigned gPsCrumb[8]; extern volatile unsigned gPsCrumbN; const char* PsCrumbName3DS(unsigned id); }
static void box_draw() {
    if (!sBoxFb || sBoxFatal) return;
    char b[64];
    const u32 now = (u32)(svcGetSystemTick() >> 18);
    int row = 0;
    snprintf(b, sizeof b, "CAJA NEGRA %s ruta %s  latido %lu", PS_BUILD_REV, gGfxConvertF2 ? "F2" : "F1", (unsigned long)++sBoxBeat);
    box_text(0, row++, b, 255, 255, 0);
    const u32 st = sStMain < ST_COUNT ? sStMain : 0;
    snprintf(b, sizeof b, "cuadro %lu %s %lums", (unsigned long)sFrames, kStageName[st], (unsigned long)(now - sStMainTick));
    box_text(0, row++, b, 255, 255, 255);
    snprintf(b, sizeof b, "nucleo1 %s %lums tareas %lu lat %lu/%lu", sStAud ? "SINTETIZA" : "espera", (unsigned long)(now - sStAudTick),
             (unsigned long)sStAudTasks, (unsigned long)sBeatHi, (unsigned long)sBeatLo);
    box_text(0, row++, b, 255, 255, 255);
    const u32 cur = blobPtr(BLOB_CUR_THREAD) ? be_load(BLOB_CUR_THREAD) : 0;
    u32 mv[2] = { 0, 0 };
    for (int i = 0; i < 2; i++) {
        const u32 bp = blobPtr(BLOB_BATTLE_MON + i * 4) ? be_load(BLOB_BATTLE_MON + i * 4) : 0;
        if (blobPtr(bp + 0x6E6)) mv[i] = *(const volatile u8*)(bp + 0x6E6);
    }
    snprintf(b, sizeof b, "juego hilo %d espera %lu lr %08lX mov %lu/%lu", blobPtr(cur) ? (int)be_load(cur + 0x14) : -1,
             (unsigned long)be_load(BLOB_DIAG_WAIT), (unsigned long)ps_last_game_lr, (unsigned long)mv[0], (unsigned long)mv[1]);
    box_text(0, row++, b, 255, 255, 255);
    snprintf(b, sizeof b, "GPU listas %lu dib %lu tr %lu cp %lu rell %lu", (unsigned long)gBox.lists, (unsigned long)gBox.draws,
             (unsigned long)gBox.xfers, (unsigned long)gBox.copies, (unsigned long)gBox.fills);
    box_text(0, row++, b, 255, 255, 255);
    snprintf(b, sizeof b, "MAL dir %lu NaN %lu c%lu | ult. lista hace %lums", (unsigned long)gBox.bad, (unsigned long)gBox.badNan,
             (unsigned long)gBox.nanReg, (unsigned long)(now - gBox.lastListTick));
    box_text(0, row++, b, gBox.bad || gBox.badNan ? 255 : 120, gBox.bad || gBox.badNan ? 80 : 255, 120);
    {   // el comando de la lista de dibujo que esta ejecutando el interprete (y los tres anteriores)
        const u32 n = gPsBoxCmdN;
        const u32 k = (n + 3) & 3;
        snprintf(b, sizeof b, "DL #%lu en %08lX: %08lX %08lX", (unsigned long)n, (unsigned long)gPsBoxCmd[k][0], (unsigned long)gPsBoxCmd[k][1], (unsigned long)gPsBoxCmd[k][2]);
        box_text(0, row++, b, 255, 255, 0);
        snprintf(b, sizeof b, "antes: %08lX %08lX %08lX", (unsigned long)gPsBoxCmd[(k + 3) & 3][1], (unsigned long)gPsBoxCmd[(k + 2) & 3][1], (unsigned long)gPsBoxCmd[(k + 1) & 3][1]);
        box_text(0, row++, b, 255, 255, 0);
    }
    {   // se mueve o esta quieto: comandos de la lista y funciones entre un pulso y el siguiente; si el hilo de
        // prioridad mas baja del nucleo 0 no avanza, el principal no suelta el procesador (ciclo), si avanza, espera algo
        static u32 sPrevDl, sPrevCr, sPrevLo;
        const u32 dl = gPsBoxCmdN, cr = gPsCrumbN, lo = sBeatLo;
        snprintf(b, sizeof b, "DL +%lu  funciones +%lu  principal %s", (unsigned long)(dl - sPrevDl), (unsigned long)(cr - sPrevCr),
                 lo != sPrevLo ? "ESPERA (cede)" : "CICLO (no cede)");
        box_text(0, row++, b, 255, 160, 0);
        sPrevDl = dl; sPrevCr = cr; sPrevLo = lo;
        struct mallinfo mi = mallinfo();
        const u32 freeB = (__ctru_heap_size > (u32)mi.arena ? __ctru_heap_size - (u32)mi.arena : 0) + (u32)mi.fordblks;
        snprintf(b, sizeof b, "libre %lu KB lineal %lu KB | tex %lu gpu %lu", (unsigned long)(freeB / 1024), (unsigned long)(linearSpaceFree() / 1024),
                 (unsigned long)sTexFaults, (unsigned long)sGpuFaults);
        box_text(0, row++, b, 255, 255, 255);
    }
    {   // cola GX: cuantos comandos hay, cual sigue, y el que esta en curso (01 lista, 02 relleno, 03 transferencia, 04 copia)
        const gxCmdQueue_s* q = gBoxQueue;
        if (q && q->entries && q->maxEntries) {
            const u32 cur = q->curEntry, n = q->numEntries;
            const u32 k = q->lastEntry; // el primero que no ha terminado
            const u32 c0 = k < q->maxEntries ? q->entries[k].data[0] : 0, c1 = k < q->maxEntries ? q->entries[k].data[1] : 0;
            snprintf(b, sizeof b, "cola %lu de %lu ult %lu | en curso %02lX %08lX", (unsigned long)cur, (unsigned long)n, (unsigned long)q->lastEntry,
                     (unsigned long)(c0 & 0xFF), (unsigned long)c1);
        } else snprintf(b, sizeof b, "cola: sin datos");
        box_text(0, row++, b, 0, 255, 255);
    }
    static const char* const kCap[] = { "-", "vacia", "EN LA SD", "ENVIADA A LA PC", "fallo el envio", "sin memoria" };
    snprintf(b, sizeof b, "atoron %lums | captura %s (%lu KB)", (unsigned long)gPsStallNow, kCap[gPsCapState < 6 ? gPsCapState : 0], (unsigned long)(gPsCapBytes / 1024));
    box_text(0, row++, b, gPsStallCount ? 255 : 120, gPsStallCount ? 80 : 255, 120);
    snprintf(b, sizeof b, "raro %lu | vtx invalidos %lu | unif corregidos %lu (c%lu)", (unsigned long)gBox.sus, (unsigned long)gPsBadVtx3DS,
             (unsigned long)gPsBadUnif3DS, (unsigned long)gPsBadUnifReg3DS);
    box_text(0, row++, b, 255, (gBox.sus || gPsBadVtx3DS || gPsBadUnif3DS) ? 80 : 255, (gBox.sus || gPsBadVtx3DS || gPsBadUnif3DS) ? 80 : 255);
    {   // la lista de dibujo del juego: lo minimo que le quedo libre y su tamano (si llega a 0 se encima con sus datos)
        const s32 fr = BLOB_DYNFREE ? (s32)intro_be(BLOB_DYNFREE) : 0; const u32 sz = BLOB_DYNSIZE ? intro_be(BLOB_DYNSIZE) : 0;
        snprintf(b, sizeof b, "lista del juego: libre min %ld de %lu KB", fr == 0x7FFFFFFF ? -1L : (long)fr / 1024, (unsigned long)sz / 1024);
        box_text(0, row++, b, 255, fr < 4096 ? 80 : 255, fr < 4096 ? 80 : 255);
    }
    {   // cache de modelos y el ultimo cuadro incompleto por texturas (lo que hace que falten modelos e imagenes)
        const mc::Stats m = GfxConvertModelCachePeek();
        snprintf(b, sizeof b, "tri GPU %lu | inv %lu h0 %lu t0 %lu why %lu/%lu/%lu", (unsigned long)gPsTriLast3DS, (unsigned long)gPsBadRetain3DS, (unsigned long)gPsSlot0Fallback3DS, (unsigned long)gPsTmplFallback3DS, (unsigned long)gPsRetainWhy3DS[3], (unsigned long)gPsRetainWhy3DS[6], (unsigned long)gPsRetainWhy3DS[7]); box_text(0, row++, b, 255, gPsBadRetain3DS ? 80 : 255, gPsBadRetain3DS ? 80 : 255);
        snprintf(b, sizeof b, "modelos %lu (%lu KB gpu %lu KB) sinmem %lu/%lu/%lu", (unsigned long)m.entries, (unsigned long)(m.bytes / 1024),
                 (unsigned long)(m.gpuBytes / 1024), (unsigned long)m.allocFails, (unsigned long)m.budgetFails, (unsigned long)m.gpuAllocFails);
        box_text(0, row++, b, 255, (m.allocFails || m.gpuAllocFails) ? 80 : 255, (m.allocFails || m.gpuAllocFails) ? 80 : 255);
        snprintf(b, sizeof b, "limpiezas al cambiar de modo %lu", (unsigned long)sFlushes); box_text(0, row++, b, 200, 255, 200);
        if (sTexFaults) { snprintf(b, sizeof b, "tex fallo %lu: %s", (unsigned long)sTexFaults, sTexFaultWhy); box_text(0, row++, b, 255, 80, 80); }
    }
    snprintf(b, sizeof b, "dir mala %lu: %08lX cmd %08lX w0 %08lX", (unsigned long)gPsBadPtrN3DS, (unsigned long)gPsBadPtr3DS,
             (unsigned long)gPsBadPtrCmd3DS, (unsigned long)gPsBadPtrW0_3DS);
    box_text(0, row++, b, 255, gPsBadPtrN3DS ? 80 : 255, gPsBadPtrN3DS ? 80 : 255);
    if (gPsBadStkSet3DS) {
        int n = snprintf(b, sizeof b, "origen:");
        for (u32 i = 0; i < 3 && i < gPsBadStkN3DS; i++) { u32 rom; const char t = PsAddrTag3DS(gPsBadStk3DS[i], &rom);
            n += snprintf(b + n, sizeof b - n, rom ? " %08lX%c%06lX" : " %08lX%c", (unsigned long)gPsBadStk3DS[i], t, (unsigned long)rom); }
        box_text(0, row++, b, 255, 80, 80);
    }
    // ultimas funciones a las que entro el hilo principal (la ultima arriba)
    {
        const u32 n = gPsCrumbN;
        for (u32 i = 0; i < 6; i++) {
            if (n < i + 1) { box_text(0, row++, "", 0, 0, 0); continue; }
            const u32 id = gPsCrumb[(n - 1 - i) & 7];
            snprintf(b, sizeof b, "%s%lu %s", i == 0 ? "> " : "  ", (unsigned long)id, PsCrumbName3DS(id));
            box_text(0, row++, b, i == 0 ? 255 : 200, 255, i == 0 ? 0 : 200);
        }
    }
    // ultimos eventos del GPU (el ultimo arriba)
    const u32 seq = gBox.seq;
    for (int i = 0, shown = 0; i < kBoxEvents - 1 && shown < 5; i++) { // solo listas: son las que pueden colgar al GPU
        if (seq < (u32)i + 1) { box_text(0, row++, "", 0, 0, 0); shown++; continue; }
        const BoxEvent& e = gBox.ev[(seq - i) % kBoxEvents];
        if (e.kind != BOX_LIST) continue;
        shown++;
        if (e.kind == BOX_LIST)
            snprintf(b, sizeof b, "f%lu %lup %lud x2^%lu w0:%lu nan:%lu s%lX %04lX%s", (unsigned long)e.frame,
                     (unsigned long)e.b, (unsigned long)e.c, (unsigned long)(e.a & 0xFF), (unsigned long)((e.a >> 8) & 0xFFF), (unsigned long)(e.a >> 20),
                     (unsigned long)e.d, (unsigned long)e.badReg, e.bad ? " MAL" : "");
        else
            snprintf(b, sizeof b, "%lu f%lu %s %08lX>%08lX %lX%s", (unsigned long)e.seq, (unsigned long)e.frame, box_kind(e.kind),
                     (unsigned long)e.a, (unsigned long)e.b, (unsigned long)e.c, e.bad ? " MAL" : "");
        box_text(0, row++, b, e.bad || (e.kind == BOX_LIST && e.d) ? 255 : 160, e.bad ? 80 : 200, e.bad ? 80 : (e.kind == BOX_LIST && e.d) ? 80 : 255);
    }
    svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)sBoxFb, 240 * 320 * 3);
    // problemas nuevos: al registro de inmediato (el vigilante lo escribe en la SD en esta misma vuelta)
    // NaN: solo la primera vez (los cuenta el pulso); direcciones invalidas: cada vez que aparecen
    if (gBox.bad != sBoxBadSeen || (gBox.badNan && !sBoxNanSeen)) {
        const BoxEvent& e = gBox.lastBad;
        char h[200];
        snprintf(h, sizeof h, "[caja] GPU: %lu direcciones invalidas, %lu uniformes NaN (c%lu %08lX %s)\n", (unsigned long)gBox.bad,
                 (unsigned long)gBox.badNan, (unsigned long)gBox.nanReg, (unsigned long)gBox.nanVal, gBox.nanMode ? "f32" : "f24");
        log_store(h);
        if (e.seq) {
            char l[200];
            snprintf(l, sizeof l, "[caja]   ultimo: #%lu cuadro %lu %s reg %03lX valor %08lX (a %08lX b %08lX c %08lX d %08lX)\n", (unsigned long)e.seq,
                     (unsigned long)e.frame, box_kind(e.kind), (unsigned long)e.badReg, (unsigned long)e.badVal, (unsigned long)e.a, (unsigned long)e.b,
                     (unsigned long)e.c, (unsigned long)e.d);
            log_store(l);
        }
        sBoxBadSeen = gBox.bad;
        sBoxNanSeen = gBox.badNan;
        log_write_out();
    }
}
#endif

static void vigil(void*) {
    u32 lastFrame = sFrames, stillMs = 0, nextReport = 5000, tick = 0;
    u32 seq0 = sStMainSeq, hi0 = sBeatHi, lo0 = sBeatLo;
    char a[1024], b[400];
    while (!sWatchQuit) {
        svcSleepThread(250000000LL);
        tick++;
        if (sAptPaused || sAptExiting) { lastFrame = sFrames; stillMs = 0; nextReport = 5000; continue; } // en HOME: no es bloqueo
        if (sFrames != lastFrame) {
            if (stillMs >= 5000) {
                snprintf(b, sizeof b, "[bloqueo] el juego volvio a dibujar despues de %lu s (cuadro %lu)\n", (unsigned long)(stillMs / 1000),
                         (unsigned long)sFrames);
                log_store(b);
            }
            lastFrame = sFrames; stillMs = 0; nextReport = 5000;
        } else {
            stillMs += 250;
        }
        if (kLogs && stillMs >= nextReport) {
            sLogNoStdout = true; // desde aqui nadie escribe a la red: si stdout es lo trabado, no arrastra al registro
            vigil_state(a, sizeof a, stillMs, seq0, hi0, lo0);
            log_store(a);
            vigil_stack(b, sizeof b);
            log_store(b);
            if (nextReport == 5000 && BLOB_FIBERS) { // los demas hilos del juego: donde quedo cada uno (el que espera algo que no llega)
                for (int f = 0; f < 32; f++) {
                    const u32 sp = be_load(BLOB_FIBERS + (u32)f * 128u + 32u);
                    if (!blobPtr(sp) || sp == 0) continue;
                    int k = snprintf(b, sizeof b, "[bloqueo] hilo %d:", f);
                    for (int i = 0, shown = 0; blobPtr(sp + i * 4) && i < 512 && shown < 12 && k < (int)sizeof b - 12; i++) {
                        const u32 v = be_load(sp + i * 4);
                        if (v >= BLOB_BASE && v < BLOB_TEXT_END) { k += snprintf(b + k, sizeof b - k, " %08lX", (unsigned long)v); shown++; }
                    }
                    snprintf(b + k, sizeof b - k, "\n");
                    log_store(b);
                }
            }
            if (nextReport == 30000) {
                diag_summary("bloqueo (el juego dejo de dibujar)");
                log_write_out();
#ifndef PS_BLACKBOX
                vigil_screen(a); // con caja negra la pantalla de abajo ya muestra todo (y sigue latiendo)
#endif
            }
            log_write_out();
            nextReport = nextReport < 15000 ? 15000 : nextReport < 30000 ? 30000 : nextReport + 30000;
            seq0 = sStMainSeq; hi0 = sBeatHi; lo0 = sBeatLo;
        }
        if ((tick % 40) == 0 && stillMs < 5000) { // pulso cada 10 s
            const u32 now = (u32)svcGetSystemTick();
            snprintf(b, sizeof b, "[pulso] cuadro %lu | principal: %s (%lu cambios) | nucleo 1: %s, %lu tareas | latidos %lu/%lu | host desde otros hilos %lu\n",
                     (unsigned long)sFrames, kStageName[sStMain < ST_COUNT ? sStMain : 0], (unsigned long)(sStMainSeq - seq0),
                     sStAud ? "sintetizando" : "esperando", (unsigned long)sStAudTasks, (unsigned long)(sBeatHi - hi0), (unsigned long)(sBeatLo - lo0),
                     (unsigned long)sHostOtherThread);
            (void)now;
            log_store(b);
#ifdef PS_BLACKBOX
            snprintf(b, sizeof b, "[caja] listas %lu dibujos %lu transf %lu copias %lu rellenos %lu | invalidas %lu NaN %lu\n", (unsigned long)gBox.lists,
                     (unsigned long)gBox.draws, (unsigned long)gBox.xfers, (unsigned long)gBox.copies, (unsigned long)gBox.fills, (unsigned long)gBox.bad,
                     (unsigned long)gBox.badNan);
            log_store(b);
#endif
            seq0 = sStMainSeq; hi0 = sBeatHi; lo0 = sBeatLo;
        }
#if defined(PS_BLACKBOX) && !defined(PS_BOXLITE)
        box_draw();
#endif
        if ((tick % 20) == 0) log_write_out(); // cada 5 s
    }
}

static u32 n64ToBlob(u32) { return 0; }

// (sGfxUp se declara arriba, junto a host_exit_game) la pantalla ya se inicio (si no, show_report no tiene donde escribir)
static void fatal(const char* msg) {
    logf("\n%s\n", msg);
    if (!sGfxUp) { gfxInitDefault(); sGfxUp = true; }
    diag_summary(msg);
    if (sLog) fflush(sLog);
    show_report();
    exit(1);
}

// Excepcion de C++ que nadie atrapa: se anota de que tipo es antes de caer en abort()
#include <cxxabi.h>
#include <exception>
#include <typeinfo>
static char sTermMsg[160];
static void host_terminate() {
    const std::type_info* t = abi::__cxa_current_exception_type();
    const char* what = "";
    try { if (std::current_exception()) std::rethrow_exception(std::current_exception()); }
    catch (const std::exception& e) { what = e.what(); }
    catch (...) {}
    snprintf(sTermMsg, sizeof(sTermMsg), "excepcion de C++ sin atrapar: %s %s", t ? t->name() : "?", what);
    logf("\n*** %s (lr %08lX)\n", sTermMsg, (unsigned long)__builtin_return_address(0));
    abort();
}

// abort() (p. ej. sin memoria en C++) regresaba al Homebrew Launcher sin decir nada
extern "C" void abort(void) {
    sExitReason = sTermMsg[0] ? sTermMsg : "abort (sin memoria?)";
    logf("\n*** abort() lr %08lX\n", (unsigned long)__builtin_return_address(0));
    diag_flush(true);
    host_exit_game();
    for (;;) {}
}

int main(int argc, char** argv) {
    sGbSelfPath = argc > 0 ? argv[0] : nullptr;
    std::set_terminate(host_terminate);
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/pokestadium", 0777);
    RecursiveLock_Init(&sLogLock);
    RecursiveLock_Init(&sFileLock);
    sLog = kLogs ? fopen(LOG_PATH, "w") : nullptr;
    if (sLog) {
        static char sLogBuf[64 * 1024]; // se vacia cada kDiagFlushMs: la SD no se toca en cada linea
        setvbuf(sLog, sLogBuf, _IOFBF, sizeof sLogBuf);
        sDiagLastFlush = svcGetSystemTick();
    }
    // (la conexion con 3dslink para ver el cierre en la PC se abre hasta salir: su memoria, 512 KB, le faltaba al juego)

    bool isNew = false;
    APT_CheckNew3DS(&isNew);
    if (isNew) osSetSpeedupEnable(true);
    logf("Pokemon Stadium 3DS - %s - %s rev %s\n", isNew ? "New 3DS" : "Old 3DS", kDiag ? "DIAGNOSTICO (PS_DIAG=1)" : "normal (sin PS_DIAG)", PS_BUILD_REV);
    if (kDiag) { logf("[diag] verificacion de ModelCache y de F2 encendida; cache 2560 KB; el registro se guarda cada %lu s\n", (unsigned long)(kDiagFlushMs / 1000)); diag_flush(true); }

    // Primero la ROM que el usuario metio en su .cia; si no viene, la de la SD
    const char* romPath = ROM_PATH_ROMFS;
    sRom = ps_romfs() ? fopen(ROM_PATH_ROMFS, "rb") : nullptr;
    if (!sRom) { romPath = ROM_PATH; sRom = fopen(ROM_PATH, "rb"); }
    if (!sRom) fatal("No encontre " ROM_PATH);
    logf("ROM: %s\n", romPath);
    fseek(sRom, 0, SEEK_END);
    if (ftell(sRom) != ROM_SIZE) fatal("La ROM no mide 32 MB (debe ser .z64 US 1.0)");
    setvbuf(sRom, nullptr, _IOFBF, 64 * 1024);

    // Graficos: ventana 3DS + backend citro3d + interprete Fast3D (F3DEX2)
    sInterp = std::make_shared<Fast::Interpreter>();
    Fast::GfxSetInstance(sInterp);
    sInterp->SetGfxDebugger(std::make_shared<Fast::GfxDebugger>());
    sInterp->Init(&sWindow, &sRenderer, "Pokemon Stadium 3DS", true, 400, 240, 0, 0);
    sGfxUp = true;
    dump_cleanup();
    gb_boot_menu(); // pantalla de abajo: cartuchos de los Transfer Pak y formato de imagen
    if (sGbTopLogo) { gb_top_logo(false); sGbTopLogo = false; } // la de arriba, en negro otra vez: ahi va el juego
#if defined(PS_BLACKBOX) && !defined(PS_BOXLITE)
    box_init();
#endif
    Fast::gfx_set_target_ucode(ucode_f3dex2);
    hook_backend_timing();
    GfxConvertInit(GfxConvertConfig{ blobPtr, n64ToBlob, nullptr }, 2 * 1024 * 1024);
    gGfxConvertSkin = true;

    // Bloque del juego en direccion fija
    u32 out = 0;
    Result res = svcControlMemory(&out, BLOB_BASE, 0, BLOB_SIZE, MEMOP_ALLOC, (MemPerm)(MEMPERM_READ | MEMPERM_WRITE));
    if (R_FAILED(res)) { logf("alloc %08lX\n", res); fatal("No pude reservar memoria para el juego"); }
    memset((void*)BLOB_BASE, 0, BLOB_SIZE);
    memcpy((void*)BLOB_BASE, blob_bin, blob_bin_size);
    sBlobReady = true;
    Handle self;
    svcDuplicateHandle(&self, CUR_PROCESS_HANDLE);
    svcFlushProcessDataCache(self, BLOB_BASE, BLOB_SIZE);
    res = svcControlProcessMemory(self, BLOB_BASE, BLOB_BASE, BLOB_TEXT_END - BLOB_BASE, MEMOP_PROT,
                                  (MemPerm)(MEMPERM_READ | MEMPERM_EXECUTE));
    if (R_FAILED(res)) { logf("prot %08lX\n", res); fatal("No pude marcar el codigo como ejecutable"); }

    // Banderas que valen siempre (no solo en la repeticion)
    if (host_flag("perdonar") != 0xFFFFFFFFu) gPsDebtDecay3DS = true;
    { const u32 kb = host_flag("assetcache"); if (kb != 0xFFFFFFFFu && kb != 1) sResCBudget = kb * 1024u; }
    { if (host_flag("luzcpu") != 0xFFFFFFFFu) PsSkinSetGpuLit3DS(false); }
    if (host_flag("sinretener") != 0xFFFFFFFFu) PsSkinSetRetain3DS(false);
    if (host_flag("nieblacpu") != 0xFFFFFFFFu) PsSkinSetRetainFog3DS(false);
    if (host_flag("sinfbgpu") != 0xFFFFFFFFu) gGfxConvertGpuFb = false; // regresar al RDP por software (sin modelos)
    {
        // cache de modelos horneados: modelcache.txt = KB de presupuesto (0 la apaga), modelverify.txt compara cada acierto
        // diagnostico: siempre cache de 1536 KB con verificacion; los .txt se ignoran para que la prueba sea la misma
        const u32 kb = kDiag ? 0xFFFFFFFFu : host_flag("modelcache"), ver = kDiag ? 1u : host_flag("modelverify");
        if (kDiag && (host_flag("modelcache") != 0xFFFFFFFFu || host_flag("modelverify") != 0xFFFFFFFFu))
            logf("[diag] modelcache.txt/modelverify.txt en la SD: se ignoran en esta compilacion\n");
        // F2: los cuerpos de F1 y sus copias para el GPU comparten el presupuesto
        GfxConvertModelCacheConfig((kb == 0xFFFFFFFFu ? 2560u : kb) * 1024u, ver != 0xFFFFFFFFu && ver != 0);
        gPsResVerify3DS = kDiag;
        { const u32 q = host_flag("quitar"); if (q != 0xFFFFFFFFu) gGfxConvertDbgDrop = q; }
        // L presionado al iniciar: ruta F1 (sin buffers residentes) para comparar en la misma compilacion
        hidScanInput();
        // (y sin copias retenidas de los modelos con huesos: las dos rutas especiales apagadas, para comparar)
        if (((hidKeysHeld() | sGbBootHeld) & KEY_L) || host_flag("rutalenta") != 0xFFFFFFFFu) { gGfxConvertF2 = false; PsSkinSetRetain3DS(false); }
#ifdef PS_F2_OFF
        gGfxConvertF2 = false;
#endif
        logf("[f2] ruta: %s\n", gGfxConvertF2 ? "F2 (modelos residentes en el GPU)" : "F1 (L al iniciar)");
        // R presionado al iniciar: sin capa del HUD (todo el HUD se dibuja en cada cuadro, como antes)
        sBootKeys = hidKeysHeld() | (sGbBootHeld & (KEY_L | KEY_R | KEY_X));
        if ((sBootKeys & KEY_R) || host_flag("sinhud") == 1) gGfxConvertHud = false;
        logf("[audio] fila de tareas de audio: %s\n", (sBootKeys & KEY_X) ? "apagada (X al iniciar)" : "activa");
        logf("[hud] capa del HUD: %s\n", gGfxConvertHud ? "activa" : "apagada (R al iniciar)");
        gGfxConvertHudDbg = gPsHudDbg3DS = host_flag("huddbg") == 1;
    }
    { const u32 o = host_flag("fborient"); if (o != 0xFFFFFFFFu && o < 8) gPsFbOrient3DS = (int)o; }
    { if (host_flag("limpiarfb") != 0xFFFFFFFFu) gPsClearFb3DS = true; }
    { const u32 mn = host_flag("retenermin"); if (mn != 0xFFFFFFFFu) PsSkinSetRetainMin3DS((int)mn); }
    // Modo repeticion (depuracion): sdmc:/3ds/pokestadium/replay.psfd = volcado de un frame (memoria + DL).
    // Se dibuja esa DL en bucle sin correr el juego, para ver exactamente lo que hace el render del 3DS.
    if (FILE* rf = fopen("sdmc:/3ds/pokestadium/replay.psfd", "rb")) {
        u32 hdr[4] = {};
        fread(hdr, 4, 4, rf);
        u32 base = __builtin_bswap32(hdr[1]), size = __builtin_bswap32(hdr[2]), dl = __builtin_bswap32(hdr[3]);
        if (base == BLOB_BASE && size <= BLOB_SIZE) fread((void*)BLOB_BASE, 1, size, rf);
        fclose(rf);
        logf("[replay] dl %08lX\n", (unsigned long)dl);
        sReplayMode = true;
        sLogRects = 60;
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/scissor_raw.txt", "rb")) { sScissorRaw = true; fclose(t); }
        extern bool gGfxConvertTexel1Test;
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/t1test.txt", "rb")) { gGfxConvertTexel1Test = true; fclose(t); }
        extern uint32_t gGfxConvertCcW0, gGfxConvertCcW1;
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/cc.txt", "rb")) { unsigned long a = 0, b = 0; fscanf(t, "%lx %lx", &a, &b); gGfxConvertCcW0 = a; gGfxConvertCcW1 = b; fclose(t); }
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/noskin.txt", "rb")) { gGfxConvertSkin = false; fclose(t); }
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/convlog.txt", "rb")) { extern int gGfxConvertLog; gGfxConvertLog = 900; fclose(t); }
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/nomerge.txt", "rb")) { PsSkinSetMerge3DS(false); fclose(t); }
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/noatlas.txt", "rb")) { gPsAtlasEnabled3DS = false; fclose(t); }
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/atlasonly.txt", "rb")) { unsigned w = 0, h = 0; if (fscanf(t, "%u %u", &w, &h) == 2) { gPsAtlasOnlyW3DS = w; gPsAtlasOnlyH3DS = h; } fclose(t); }
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/atlaslinear.txt", "rb")) { gPsAtlasPointOk3DS = false; fclose(t); }
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/uvlog.txt", "rb")) { gPsUvLog3DS = 3000; gPsAtlasDbg3DS = true; fclose(t); }
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/foglog.txt", "rb")) { gPsSkinFogLog3DS = 200; fclose(t); }
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/sklog.txt", "rb")) { extern int gPsSkinKindLog3DS; gPsSkinKindLog3DS = 60; gPsTevLog3DS = 600; fclose(t); }
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/atlasflush.txt", "rb")) { gPsAtlasMerge3DS = false; fclose(t); }
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/noblocks.txt", "rb")) { extern bool gGfxConvertMeshBlocks; gGfxConvertMeshBlocks = false; fclose(t); }
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/gpurange.txt", "rb")) { int lo = 0, hi = 999; if (fscanf(t, "%d %d", &lo, &hi) == 2) PsSkinSetRange3DS(lo, hi); fclose(t); }
        extern int gGfxConvertOnlyGroup;
        if (FILE* t = fopen("sdmc:/3ds/pokestadium/group.txt", "rb")) { int g = -1; fscanf(t, "%d", &g); gGfxConvertOnlyGroup = g; fclose(t); }
        // fbtest.txt: prueba de orientacion de las imagenes aparte en el GPU, sin contenido del juego: dibuja un
        // bloque rojo arriba a la izquierda y uno azul abajo a la derecha en una imagen de 32x24 y revisa cual
        // orientacion de la copia los deja en su lugar
        const u32 fbTestMode = host_flag("fbtest");
        if (fbTestMode != 0xFFFFFFFFu) {
            // mismo tamano que las imagenes del juego (el transfer del GPU no acepta imagenes muy chicas)
            static u16 testBuf[128 * 96];
            static u32 req[4];
            req[0] = 0x50534642u; req[1] = (u32)(uintptr_t)testBuf; req[2] = 128; req[3] = 96;
            auto rect = [](u32* w, u32 ulx, u32 uly, u32 lrx, u32 lry) {
                w[0] = (0xF6u << 24) | ((lrx << 2) << 12) | (lry << 2);
                w[1] = ((ulx << 2) << 12) | (uly << 2);
            };
            static u32 tdl[2 * 10];
            u32* d = tdl;
            // 1: completa; 2: solo cambiar a la imagen y regresar; 3: sin imagen (rellenos en la pantalla)
            if (fbTestMode != 3) { d[0] = 0x4Du << 24; d[1] = (u32)(uintptr_t)req; d += 2; }
            if (fbTestMode != 2 && fbTestMode != 6 && fbTestMode != 7) {
            d[0] = 0xE3000A01u; d[1] = 0x00300000u; d += 2;                 // ciclo de relleno
            d[0] = 0xF7000000u; d[1] = 0x00010001u; d += 2;                 // negro
            rect(d, 0, 0, 127, 95); d += 2;
            d[0] = 0xF7000000u; d[1] = 0xF801F801u; d += 2;                 // rojo
            rect(d, 0, 0, 15, 7); d += 2;
            d[0] = 0xF7000000u; d[1] = 0x003F003Fu; d += 2;                 // azul
            rect(d, 112, 88, 127, 95); d += 2;
            }
            d[0] = 0xFF10013Fu; d[1] = (u32)(uintptr_t)testBuf; d += 2;     // regresar
            d[0] = 0xDF000000u; d[1] = 0; d += 2;
            gPsFbStage3DS = (int)fbTestMode;
            int found = -1;
            for (int mode = 0; mode < 8 && found < 0; mode++) {
                gPsFbOrient3DS = mode;
                logf("[fbtest] probando %d (prueba %lu)\n", mode, (unsigned long)fbTestMode);
                memset(testBuf, 0, sizeof testBuf);
                fflush(sLog);
                sInterp->StartFrame();
                logf("[fbtest] a: cuadro abierto\n"); fflush(sLog);
                sInterp->Run(reinterpret_cast<::Gfx*>(tdl), sNoReplacements);
                logf("[fbtest] b: dibujado (entradas %lu salidas %lu llamadas %lu rechazos %lu)\n", (unsigned long)gPsFbStats3DS[0], (unsigned long)gPsFbStats3DS[1], (unsigned long)gPsFbStats3DS[2], (unsigned long)gPsFbStats3DS[3]); fflush(sLog);
                sInterp->EndFrame();
                logf("[fbtest] c: cuadro cerrado\n"); fflush(sLog);
                static u32 emptyDl[2] = { 0xDF000000u, 0 };
                sInterp->StartFrame(); // el siguiente cuadro copia la imagen (al empezar, ya con el GPU libre)
                logf("[fbtest] d: copia hecha (copias %lu)\n", (unsigned long)gPsFbCopies3DS); fflush(sLog);
                sInterp->Run(reinterpret_cast<::Gfx*>(emptyDl), sNoReplacements);
                sInterp->EndFrame();
                logf("[fbtest] e: listo\n"); fflush(sLog);
                auto at = [&](int x, int y) { const u8* b = (const u8*)&testBuf[y * 128 + x]; return (u16)((b[0] << 8) | b[1]); };
                const u16 a = at(4, 2), b = at(122, 92), c = at(64, 48);
                const bool red = (a >> 11) >= 28 && ((a >> 6) & 31) < 4 && ((a >> 1) & 31) < 4;
                const bool blue = (b >> 11) < 4 && ((b >> 6) & 31) < 4 && ((b >> 1) & 31) >= 28;
                const bool black = (c >> 1) == 0;
                logf("[fbtest] modo %d: esquina1 %04x esquina2 %04x centro %04x%s\n", mode, a, b, c, red && blue && black ? " BIEN" : "");
                if (red && blue && black) found = mode;
            }
            logf("[fbtest] orientacion: %d (copias %lu, entradas %lu, salidas %lu, llamadas %lu, rechazos %lu)\n", found, (unsigned long)gPsFbCopies3DS, (unsigned long)gPsFbStats3DS[0], (unsigned long)gPsFbStats3DS[1], (unsigned long)gPsFbStats3DS[2], (unsigned long)gPsFbStats3DS[3]);
            if (found >= 0) gPsFbOrient3DS = found;
        }
        u8 task[64] = {};
        u32 dlBe = __builtin_bswap32(dl);
        memcpy(task + 48, &dlBe, 4);
        u64 repT = 0; u32 repN = 0;
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysHeld() & KEY_START) break;
            const u64 t0 = svcGetSystemTick();
            host_gfx_task(task);
            repT += svcGetSystemTick() - t0;
            if (++repN == 60) {
                logf("[rep] 60 cuadros: %lu ms dibujando | f2 lotes %lu dibujos %lu anterior %lu (cerca %lu texgen %lu luzcpu %lu)\n", (unsigned long)TICKS_MS(repT),
                     (unsigned long)gPsResStats3DS[0], (unsigned long)gPsResBackend3DS[0], (unsigned long)gPsResStats3DS[13], (unsigned long)gPsResStats3DS[3],
                     (unsigned long)gPsResStats3DS[5], (unsigned long)gPsResStats3DS[6]);
                logf("[rep] hud: tramos %lu no aptos %lu sin tramo %lu | copiados %lu capturas %lu dibujados %lu con z %lu sin destino %lu llaves %lu\n",
                     (unsigned long)gGfxConvertHudStats[0], (unsigned long)gGfxConvertHudStats[2], (unsigned long)gGfxConvertHudStats[1], (unsigned long)gPsHudStats3DS[0],
                     (unsigned long)gPsHudStats3DS[1], (unsigned long)gPsHudStats3DS[2], (unsigned long)gPsHudStats3DS[4], (unsigned long)gPsHudStats3DS[3], (unsigned long)gPsHudStats3DS[5]);
                memset(gPsHudStats3DS, 0, 8 * sizeof(uint32_t)); memset(gGfxConvertHudStats, 0, 4 * sizeof(uint32_t));
                for (auto& x : gPsResStats3DS) x = 0;
                gPsResBackend3DS[0] = 0;
#ifdef PS_MEMPROF
                memprof_dump();
#endif
                repT = 0; repN = 0; log_write_out();
            }
            gspWaitForVBlank();
        }
        if (sLog) fclose(sLog);
        return 0;
    }

    // Tabla de servicios (el juego la lee en big-endian); orden = PsBlobApi en blob_host.c
    static u32 api[21];
    void* fns[21] = { (void*)tr_print, (void*)tr_ticks_lo, (void*)tr_ticks_hi, (void*)tr_sleep_us, (void*)tr_pump,
                      (void*)tr_read_pad, (void*)tr_gfx_task, (void*)tr_exit_game, (void*)tr_res_get, (void*)tr_rom_read,
                      (void*)tr_audio_push, (void*)tr_save_load, (void*)tr_save_store, (void*)tr_audio_task,
                      (void*)tr_audio_task_state, (void*)tr_audio_pending, (void*)tr_flag, (void*)tr_mem_event,
                      (void*)tr_gb_present, (void*)tr_gb_rw, (void*)tr_gb_power };
    for (int i = 0; i < 21; i++) api[i] = __builtin_bswap32((u32)fns[i]);
    audio_init();
    audio_worker_start();
    aptHook(&sAptCookie, apt_hook, nullptr);
    {
        s32 prio = 0x30;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        if (false) {
            if (!threadCreate(watchdog, nullptr, 16 * 1024, prio - 1, -2, true)) logf("[host] no pude crear el vigilante\n");
        } else {
            // vigilante en el nucleo 1, arriba del audio (prio - 2): sigue vivo aunque el nucleo 0 este ocupado o trabado
            sVigil = threadCreate(vigil, nullptr, 32 * 1024, prio - 3 > 0x18 ? prio - 3 : 0x18, 1, false);
            if (!sVigil) sVigil = threadCreate(vigil, nullptr, 32 * 1024, prio - 1, -2, false);
            const bool beats = threadCreate(beat_hi, nullptr, 4096, 0x19, 0, true) && threadCreate(beat_lo, nullptr, 4096, prio + 1, 0, true);
            logf("[diag] vigilante: %s; latidos del nucleo 0: %s\n", sVigil ? "nucleo 1" : "NO SE PUDO CREAR", beats ? "si" : "no");
        }
    }

    logf("Juego cargado (%lu KB).%s\n", (unsigned long)(blob_bin_size / 1024), kDiag ? " START+SELECT para salir" : "");
    threadOnException((ExceptionHandler)ps_exc_entry, sExcStack + sizeof(sExcStack), WRITE_DATA_TO_HANDLER_STACK);
    ps_call_blob(BLOB_ENTRY, (u32)api);
    fatal("El juego regreso al host (inesperado)");
    return 0;
}

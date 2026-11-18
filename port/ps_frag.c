/* Fragments (overlays del N64) en el port.
 *
 * Los fragments de CODIGO ya estan compilados dentro del ejecutable: "cargar" uno es devolver su
 * funcion de entrada nativa. Los de DATOS (modelos, animaciones) se cargan y reubican como en el N64.
 * Las direcciones originales que siguen guardadas en datos (0x8XXXXXXX) se traducen a nativo con
 * una tabla generada a partir del mapa del decomp. */
#include "global.h"
#include "src/memmap.h"
#include "src/3FB0.h"
#include "os/ps_os.h"
#include "generated/ps_frag_table.h" /* id -> entrada nativa */

typedef struct { u32 vram; void* native; } PsFragSym;
extern const PsFragSym gPsFragSyms[];
extern const u32 gPsFragSymsCount;

extern ret_func_80004454 ps_orig_func_80004454(s32 id, u8* romStart, u8* romEnd);
extern void* ps_orig_func_800044A8(s32 id, s32 a1, s32 a2);
extern uintptr_t ps_orig_Memmap_GetSegmentVaddr(u32 mask);
extern uintptr_t ps_orig_Memmap_GetFragmentVaddr(void* addr);

static void* code_entry(s32 id) {
    for (u32 i = 0; i < sizeof(gPsFragTable) / sizeof(gPsFragTable[0]); i++) {
        if (gPsFragTable[i].id == id) return gPsFragTable[i].entry;
    }
    return NULL;
}

/* Fragments de codigo sin funcion de entrada (se cargan como biblioteca para otros) */
int ps_frag_no_entry(void) { return 0; }

/* GB Tower (fragment 1, emulador de Game Boy en ASM): fuera del port */
static s32 gbtower_unavailable(void) {
    ps_printf("[ps] GB Tower no disponible en el port\n");
    return 0;
}

/* ---- Datos iniciales de los fragments de codigo ----
 * En el N64 cada carga de un fragment lo trae de nuevo de la ROM: sus variables con valor inicial vuelven a ese valor.
 * Aqui el fragment vive compilado dentro del ejecutable y sus datos se quedaban como los dejo la visita anterior
 * (al volver al mapa de Pokemon Stadium el desvanecido seguia al final: pantalla en negro). Al arrancar, ya con las
 * direcciones traducidas, se guarda una copia de esos datos; cada carga la devuelve. Las variables sin valor inicial
 * (.bss) se ponen en cero: en el N64 esa zona cae donde estaba la tabla de reubicacion, que el cargador borra, y el
 * juego cuenta con eso (el oscurecido del mapa arranca de 0 sin que nadie lo ponga). */
#ifdef PS_FRAG_DATA
#include "ps_frag_data.h"
extern u8 __ps_fdata_start[], __ps_fdata_end[], __ps_fsnap[];
static int sFragSnap;
void ps_frag_data_snapshot(void) {
    memcpy(__ps_fsnap, __ps_fdata_start, (u32)(__ps_fdata_end - __ps_fdata_start));
    sFragSnap = 1;
    ps_printf("[frag] datos iniciales de los fragments guardados: %u KB\n", (u32)(__ps_fdata_end - __ps_fdata_start) / 1024);
}
static void frag_data_reset(s32 id) {
    int dir = -1;
    if (!sFragSnap) return;
    for (u32 i = 0; i < sizeof(gPsFragTable) / sizeof(gPsFragTable[0]); i++) {
        if (gPsFragTable[i].id == id) { dir = gPsFragTable[i].dir; break; }
    }
    { static u32 sN; if (sN++ < 400) ps_printf("[frag] carga %d (fragment%d)\n", (int)id, dir); }
    for (u32 i = 0; i < sizeof(gPsFragData) / sizeof(gPsFragData[0]); i++) {
        if (gPsFragData[i].dir == dir) {
            u32 n = (u32)(gPsFragData[i].b - gPsFragData[i].a);
            if (n) memcpy(gPsFragData[i].a, __ps_fsnap + (gPsFragData[i].a - __ps_fdata_start), n);
            n = (u32)(gPsFragData[i].zb - gPsFragData[i].za);
            if (n) memset(gPsFragData[i].za, 0, n);
            return;
        }
    }
}
#else
void ps_frag_data_snapshot(void) {}
static void frag_data_reset(s32 id) { (void)id; }
#endif

ret_func_80004454 func_80004454(s32 id, u8* romStart, u8* romEnd) {
    void* e = code_entry(id);
    if (id == 1) return (ret_func_80004454)gbtower_unavailable;
    if (e) { frag_data_reset(id); return (ret_func_80004454)e; }
    return ps_orig_func_80004454(id, romStart, romEnd);
}

void* func_800044A8(s32 id, s32 a1, s32 a2) {
    void* e = code_entry(id);
    if (e) { frag_data_reset(id); return e; }
    return ps_orig_func_800044A8(id, a1, a2);
}

/* Un puntero nativo nunca tiene la forma de una direccion del N64: el ejecutable y el pool viven
 * fuera de 0x00000000-0x0FFFFFFF (segmentos) y de 0x80000000-0x9FFFFFFF (KSEG0 y fragments). */
/* Plataformas cuyo codigo/datos viven en 0x00000000-0x0FFFFFFF (3DS: bloque en 0x0A000000) declaran su rango */
__attribute__((weak)) int ps_platform_is_native(u32 a) { (void)a; return 0; }
static int is_n64_segmented(u32 a) { return (a >> 28) == 0 && a != 0 && !ps_platform_is_native(a); }

uintptr_t Memmap_GetSegmentVaddr(u32 mask) {
    if (!is_n64_segmented(mask)) return mask;
    return ps_orig_Memmap_GetSegmentVaddr(mask);
}

static int is_code_fragment(u32 addr) {
    s32 id = ((addr & 0x0FF00000) >> 20) - 0x10;
    return code_entry(id) != NULL;
}

typedef struct { u32 vram; u32 native; } PsStaticSym;
extern const PsStaticSym gPsStaticSyms[];
extern const u32 gPsStaticSymsCount;

static void* static_symbol(u32 addr, u32* dist) {
    s32 lo = 0, hi = (s32)gPsStaticSymsCount - 1, best = -1;
    while (lo <= hi) {
        s32 mid = (lo + hi) >> 1;
        if (gPsStaticSyms[mid].vram <= addr) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    if (best < 0 || (gPsStaticSyms[best].vram & 0xFFF00000u) != (addr & 0xFFF00000u) || gPsStaticSyms[best].native == 0) return NULL;
    *dist = addr - gPsStaticSyms[best].vram;
    return (u8*)(uintptr_t)gPsStaticSyms[best].native + *dist;
}

static void* native_symbol(u32 addr) {
    u32 sdist = 0xFFFFFFFFu;
    void* st = static_symbol(addr, &sdist);
    s32 lo = 0, hi = (s32)gPsFragSymsCount - 1, best = -1;
    while (lo <= hi) {
        s32 mid = (lo + hi) >> 1;
        if (gPsFragSyms[mid].vram <= addr) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    if (best >= 0 && (gPsFragSyms[best].vram & 0xFFF00000u) == (addr & 0xFFF00000u)) {
        u32 gdist = addr - gPsFragSyms[best].vram;
        if (st && sdist < gdist) return st; /* el static mas cercano gana */
        return (u8*)gPsFragSyms[best].native + gdist;
    }
    return st;
}

typedef struct { u32 slot; u32 target; } PsJumpSlot;
extern const PsJumpSlot gPsJumpSlots[];
extern const u32 gPsJumpSlotsCount;

uintptr_t Memmap_GetFragmentVaddr(void* p) {
    u32 addr = (u32)(uintptr_t)p;
    for (u32 i = 0; i < gPsJumpSlotsCount; i++) {
        if (gPsJumpSlots[i].slot == addr) { addr = gPsJumpSlots[i].target; break; }
    }
    if ((addr >= 0x80000000u && addr < 0x80400000u) || (addr >= 0x81000000u && addr < 0x90000000u && is_code_fragment(addr))) {
        void* n = native_symbol(addr);
        if (n) return (uintptr_t)n;
        ps_printf("[frag] sin simbolo nativo para %08x\n", addr);
    }
    return ps_orig_Memmap_GetFragmentVaddr(p);
}

/* ---- Direcciones del N64 escritas como numeros en los datos del juego ----
 * El decomp tiene tablas (geo layouts, listas de nodos) con direcciones como 0x86B0CF38 en lugar del
 * simbolo. En el N64 la reubicacion de fragments las arreglaba; aqui se traducen una vez al arrancar:
 * solo palabras alineadas que coinciden EXACTO con el inicio de un simbolo conocido. */
__attribute__((weak)) void ps_platform_data_range(u32** start, u32** end) { *start = NULL; *end = NULL; }

void ps_fixup_log(u32 n64) __attribute__((weak));
void ps_fixup_data_pointers(void) {
    u32 *p, *end, n = 0;
    ps_platform_data_range(&p, &end);
    /* las tablas del propio port contienen direcciones del N64 a proposito */
    const u32* skipA = (const u32*)gPsFragSyms;
    const u32* skipAEnd = (const u32*)(gPsFragSyms + gPsFragSymsCount);
    const u32* skipC = (const u32*)gPsStaticSyms;
    const u32* skipCEnd = (const u32*)(gPsStaticSyms + gPsStaticSymsCount + 1);
    const u32* skipB = (const u32*)gPsJumpSlots;
    const u32* skipBEnd = (const u32*)(gPsJumpSlots + gPsJumpSlotsCount + 1);
    for (; p && p < end; p++) {
        if ((p >= skipA && p < skipAEnd) || (p >= skipB && p < skipBEnd) || (p >= skipC && p < skipCEnd)) continue;
        u32 w = *p, w0 = w;
        if (!((w >= 0x80000000u && w < 0x80400000u) || (w >= 0x81000000u && w < 0x90000000u && is_code_fragment(w)))) continue;
        for (u32 i = 0; i < gPsJumpSlotsCount; i++) {
            if (gPsJumpSlots[i].slot == w) { w = gPsJumpSlots[i].target; break; }
        }
        u32 d = 1;
        void* st = static_symbol(w, &d);
        if (st && d == 0) { *p = (u32)(uintptr_t)st; n++; if (ps_fixup_log) ps_fixup_log(w0); continue; }
        s32 lo = 0, hi = (s32)gPsFragSymsCount - 1;
        while (lo <= hi) {
            s32 mid = (lo + hi) >> 1;
            if (gPsFragSyms[mid].vram == w) { *p = (u32)(uintptr_t)gPsFragSyms[mid].native; n++; if (ps_fixup_log) ps_fixup_log(w0); break; }
            if (gPsFragSyms[mid].vram < w) lo = mid + 1; else hi = mid - 1;
        }
    }
    ps_printf("[frag] %u direcciones del N64 en los datos traducidas a nativo\n", n);
}

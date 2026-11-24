/* Entrada de batalla del Pokemon Stadium japones (1998), opcional.
 *
 * Los datos (modelos, animaciones, fondos) se leen de la ROM japonesa que pone el usuario en la SD
 * (sdmc:/3ds/pokestadium/baserom_jp.z64). Aqui no hay ningun dato de esa ROM: solo se carga y se
 * reubica con el mismo cargador de fragments del juego. Si la ROM no esta, nada de esto se usa. */
#include "global.h"
#include "src/memmap.h"
#include "src/memory.h"
#include "src/3FB0.h"
#include "src/29BA0.h"
#include "os/ps_os.h"

const void* ps_jp_res_get(u32 rom, u32 size, u32* outSize);
u32 ps_jp_rom_read(u32 rom, void* dst, u32 size);

/* 1 si la ROM japonesa esta y es la esperada (cabecera del N64 + codigo de juego CPSJ) */
int ps_jp_available(void) {
    static int state;
    if (!state) {
        u8 h[0x40];
        state = (ps_jp_rom_read(0, h, sizeof h) == sizeof h && h[0] == 0x80 && h[0x3B] == 'C' && h[0x3C] == 'P' && h[0x3D] == 'S' && h[0x3E] == 'J') ? 1 : -1;
        ps_printf("[jp] ROM japonesa: %s\n", state > 0 ? "disponible" : "no disponible");
    }
    return state > 0;
}

/* Un rango de la ROM japonesa al pool, descomprimido (PERS-SZP) */
void* ps_jp_load(u32 romStart, u32 romEnd, s32 side) {
    u32 n = 0;
    const void* data = ps_jp_res_get(romStart, romEnd - romStart, &n);
    u8* dst;
    if (data == NULL || n == 0) { ps_printf("[jp] no pude leer %06x-%06x\n", romStart, romEnd); return NULL; }
    dst = main_pool_alloc(ALIGN16(n), side);
    if (dst != NULL) memcpy(dst, data, n);
    return dst;
}

/* Un fragment de datos de la ROM japonesa: cargar y reubicar como los del juego */
Fragment* ps_jp_fragment(s32 id, u32 romStart, u32 romEnd) {
    Fragment* f = (Fragment*)ps_jp_load(romStart, romEnd, 0);
    if (f == NULL) return NULL;
    if (memcmp(f->magic, "FRAGMENT", 8) != 0) { ps_printf("[jp] %06x no es un fragment\n", romStart); return NULL; }
    func_800043BC(id, f);
    return f;
}

/* ---------------- la escena ---------------- */
#include "ps_jp.h"
void func_80004200(u32 block_addr, u32 addr);
/* Las tablas solo existen si el bloque se compilo con la escena (PS_JP_DECOMP); si no, todo queda en cero */
extern const PsJpImage gPsJpImages[] __attribute__((weak));
extern const unsigned int gPsJpImagesCount __attribute__((weak));
extern const PsJpSym gPsJpSyms[] __attribute__((weak));
extern const unsigned int gPsJpSymsCount __attribute__((weak));
#define gPsJpImagesCount (&gPsJpImagesCount ? gPsJpImagesCount : 0u)
#define gPsJpSymsCount (&gPsJpSymsCount ? gPsJpSymsCount : 0u)

static u8 sJpLoaded[32]; /* fragments japoneses cargados (por indice de gPsJpImages) */

/* 1 si esa direccion del N64 cae en un fragment japones cargado: su traduccion es la del mapa, no la del juego de EE. UU. */
int ps_jp_owns(u32 addr) {
    u32 i;
    for (i = 0; i < gPsJpImagesCount; i++)
        if (sJpLoaded[i] && (addr & 0xFFF00000u) == PS_JP_VRAM(gPsJpImages[i])) return 1;
    return 0;
}

static char* jp_sym(u32 vram) {
    s32 lo = 0, hi = (s32)gPsJpSymsCount - 1;
    while (lo <= hi) {
        s32 mid = (lo + hi) >> 1;
        u32 v = ~gPsJpSyms[mid].nvram;
        if (v == vram) return gPsJpSyms[mid].native;
        if (v < vram) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* Apuntadores escritos en los datos de un fragment japones:
 *  - a funciones o variables del motor japones (0x800xxxxx): al simbolo equivalente del juego
 *  - a funciones de la escena dentro de un fragment (ya reubicadas a su zona): a la funcion compilada */
static void jp_fix_pointers(const PsJpImage* img, Fragment* f) {
    u32* p = (u32*)((u8*)f + f->headerSize);
    u32* end = (u32*)((u8*)f + f->relocOffset);
    u32 n = 0, i;
    for (; p < end; p++) {
        u32 w = *p;
        char* t = NULL;
        if (w >= 0x80000400u && w < 0x80100000u) t = jp_sym(w);
        if (!t && w >= 0x80000400u && w < 0x80100000u && (w & 3) == 0) ps_printf("[jp] %08x+%x: direccion del motor japones sin equivalente: %08x\n", PS_JP_VRAM(*img), (u32)((u8*)p - (u8*)f), w);
        else if (w >= 0x81000000u && w < 0x90000000u) t = jp_sym(w); /* funcion de otro fragment japones, sin reubicar */
        else {
            for (i = 0; i < gPsJpImagesCount; i++) {
                u32 base = (u32)(uintptr_t)gPsJpImages[i].mem;
                if (sJpLoaded[i] && w >= base && w < base + gPsJpImages[i].size) { t = jp_sym(PS_JP_VRAM(gPsJpImages[i]) + (w - base)); if (!t && PS_JP_VRAM(*img) == 0x82200000u && PS_JP_VRAM(gPsJpImages[i]) == 0x82200000u && (w - base) < 0x2C00) ps_printf("[jp] sin funcion para %08x (en +%x)\n", PS_JP_VRAM(gPsJpImages[i]) + (w - base), (u32)((u8*)p - (u8*)f)); break; }
            }
        }
        if (t) { *p = (u32)(uintptr_t)t; n++; }
    }
    (void)n;
}

/* Fragment_Load de la escena: el fragment va a su zona fija (el codigo compilado lo ubica por direccion) */
void* ps_jp_Fragment_Load(s32 id, u32 romStart, u32 romEnd) {
    u32 i, n = 0;
    for (i = 0; i < gPsJpImagesCount; i++) {
        const PsJpImage* img = &gPsJpImages[i];
        const void* data;
        Fragment* f;
        if (img->romStart != romStart) continue;
        /* el codigo de modelos (0x81000000) es el del juego: los iconos de los Pokemon usan sus modelos, no los japoneses */
        if (PS_JP_VRAM(*img) == 0x81000000u) return NULL;
        data = ps_jp_res_get(romStart, romEnd - romStart, &n);
        if (data == NULL || n == 0 || n > img->size) { ps_printf("[jp] fragment %08x: no cabe o no se pudo leer (%x de %x)\n", PS_JP_VRAM(*img), n, img->size); return NULL; }
        memset(img->mem, 0, img->size);
        memcpy(img->mem, data, n);
        f = (Fragment*)img->mem;
        if (memcmp(f->magic, "FRAGMENT", 8) != 0) { ps_printf("[jp] %08x no es un fragment\n", PS_JP_VRAM(*img)); return NULL; }
        sJpLoaded[i] = 1;
        {
            u32 reloc = f->relocOffset;
            Memmap_RelocateFragment(id, f);
            jp_fix_pointers(img, f);
            if (reloc < img->size) memset(img->mem + reloc, 0, img->size - reloc); /* ahi empieza el .bss del fragment */
        }
        return f;
    }
    ps_printf("[jp] fragment en %06x: sin zona reservada\n", romStart);
    return NULL;
}
/* El fragment que arma los datos del entrenador (codigo del juego japones): aqui los llena ps_jp_scene.c */
s32 ps_jp_Fragment_LoadAndCall(s32 id, u32 romStart, u32 romEnd, s32 a, s32 b) { (void)id; (void)romStart; (void)romEnd; (void)a; (void)b; return 0; }

void* ps_jp_Asset_LoadToSegment(s32 id, u32 romStart, u32 romEnd, s32 side) {
    u8* vaddr = ps_jp_load(romStart, romEnd, side);
    if ((vaddr != NULL) && (id > 0)) {
        Memmap_SetSegmentMap(id, (uintptr_t)vaddr, main_pool_get_block_dist(vaddr));
        main_pool_set_func(vaddr, id + 0x82000000, (AllocateFunc)func_80004200);
    }
    return vaddr;
}
/* El archivo completo en memoria (el juego lo trataria por partes desde su propia ROM) */
static BinArchive* sJpArchive;
static u32 sJpArchiveRom;
BinArchive* ps_jp_BinArchive_Open(u32 romStart, u32 romEnd, s32 mode, s32 side) {
    (void)mode; (void)side;
    { u8 h[16]; u32 n; /* solo la tabla: los archivos se traen de uno en uno */
      if (ps_jp_rom_read(romStart, h, 16) != 16) return NULL;
      n = ((u32)h[12] << 24) | ((u32)h[13] << 16) | ((u32)h[14] << 8) | h[15];
      if (n > 256) return NULL;
      romEnd = romStart + 16 + n * 16; }
    sJpArchive = ps_jp_load(romStart, romEnd, 0);
    sJpArchiveRom = romStart;
    return sJpArchive;
}
/* Perfil del jugador (lo pone la pantalla de abajo): retrato (-1 el del juego, 0-53 de la US, 0x100+n japones) y nombre */
s32 gPsProfPortrait = -1;
char gPsProfName[12];
/* Retratos de la US en las tarjetas japonesas: mismo formato (64x64 RGBA16). Indices 0x40+n. */
#define PS_US_PORTRAITS 0x533B20u
extern const void* ps_us_res_get(u32 rom, u32 size, u32* outSize);
extern u32 ps_us_rom_read(u32 rom, void* dst, u32 size);
static void* jp_us_portrait(s32 idx) {
    u8 e[16]; u32 off, size, n = 0; const void* d; u8* dst;
    if (ps_us_rom_read(PS_US_PORTRAITS + 16 + (u32)idx * 16, e, 16) != 16) return NULL;
    off = ((u32)e[0] << 24) | ((u32)e[1] << 16) | ((u32)e[2] << 8) | e[3];
    size = ((u32)e[4] << 24) | ((u32)e[5] << 16) | ((u32)e[6] << 8) | e[7];
    if (size == 0 || size > 0x4000) return NULL;
    d = ps_us_res_get(PS_US_PORTRAITS + off, size, &n);
    if (d == NULL || n != 0x2000) return NULL;
    dst = main_pool_alloc(0x2000, 0);
    if (dst != NULL) memcpy(dst, d, 0x2000);
    return dst;
}
/* El estadio de la US donde va a ser la pelea, en lugar de la arena japonesa (la pantalla gigante se queda). Cada estadio es un archivo
   de stadium_models con una interfaz de consulta: 0, 1 y 3 devuelven sus tres capas (geo layouts), 2 el cielo y 4 la
   niebla. Es lo mismo que hace la batalla (fragment62) para armar su escenario. */
#include "src/geo_layout.h"
#include "src/11BA0.h"
extern u8 stadium_models_ROM_START[];
extern u8 stadium_models_ROM_END[];
typedef void* (*PsStadiumFn)(s32, s32);
void ps_jp_us_stadium_load(void);
extern u8 gPool[];
/* Una torre de luces mas, al centro y en linea con las dos del fondo; la pantalla gigante va empotrada sobre sus
   focos (la escena la sube hasta ahi, ver PS_GRID_DY/DZ en port/jp). A cada pieza de las torres (estructura, focos y
   resplandor) se le agrega una copia de su lista con solo la torre del fondo a la derecha, recorrida al centro. Las
   otras tres torres de la copia quedan en un punto (triangulos sin area). Solo el estadio 1. */
#define PS_TORRE_X 2500
#define PS_TORRE_Y 0
#define PS_TORRE_Z 0
static s32 ps_torre_es(u32* dl, s32* cmds, s32* verts) {
    s32 maxY = -30000, maxX = 0, n = 0, nv = 0, i;
    for (; n < 96; n++) {
        u32 op = dl[n * 2] >> 24;
        if (op == 0xDF) break;
        if (op == 0xDE) return 0;
        if (op == 0x01) {
            s32 c = (dl[n * 2] >> 12) & 0xFF;
            s16* v = (s16*)dl[n * 2 + 1];
            for (i = 0; i < c; i++, v += 8) { s32 x = v[0] < 0 ? -v[0] : v[0]; if (x > maxX) maxX = x; if (v[1] > maxY) maxY = v[1]; }
            nv += c;
        }
    }
    if (n >= 96) return 0;
    *cmds = n + 1; *verts = nv;
    return maxY >= 2700 && maxX < 3500;
}
static void ps_torre_nodos(u8* first, s32 depth) {
    u8* n = first;
    s32 guard = 0;
    if (n == NULL || depth > 5) return;
    do {
        u32* dl = *(u32**)(n + 0x18);
        s32 cmds, verts, i, k;
        if (n[0] == 0x19 && dl != NULL && ps_torre_es(dl, &cmds, &verts)) {
            u32* out = main_pool_alloc((cmds + 1) * 8 + verts * 16, 0);
            if (out != NULL) {
                s16* vo = (s16*)(out + (cmds + 1) * 2);
                out[0] = 0xDE000000; out[1] = (u32)dl;
                for (i = 0; i < cmds; i++) {
                    out[2 + i * 2] = dl[i * 2]; out[3 + i * 2] = dl[i * 2 + 1];
                    if ((dl[i * 2] >> 24) == 0x01) {
                        s32 c = (dl[i * 2] >> 12) & 0xFF;
                        s16* v = (s16*)dl[i * 2 + 1];
                        out[3 + i * 2] = (u32)vo;
                        for (k = 0; k < c; k++, v += 8, vo += 8) {
                            memcpy(vo, v, 16);
                            if (v[0] > 0 && v[2] < 0) { vo[0] = v[0] - PS_TORRE_X; vo[1] = v[1] - PS_TORRE_Y; vo[2] = v[2] + PS_TORRE_Z; } else { vo[0] = 0; vo[1] = 0; vo[2] = 0; }
                        }
                    }
                }
                *(u32**)(n + 0x18) = out;
            }
        }
        ps_torre_nodos(*(u8**)(n + 0xC), depth + 1);
        n = *(u8**)(n + 8);
    } while (n != NULL && n != first && ++guard < 64);
}

/* Donde va el edificio de la pantalla gigante en cada estadio: altura del centro de la pantalla, fondo (z, negativo
   hacia atras) y escala en centesimas (45 = la pantalla mide 576x432). Unidades del mundo de la escena. */
typedef struct { s16 y, z, scale; } PsJpBoard;
/* el marco dorado: centro en y, fondo z, tamano del hueco (igual en la batalla) */
#define PS_GOLD_CY 330
#define PS_GOLD_Z (-1450)
#define PS_GOLD_W 640
#define PS_GOLD_H 480
static const PsJpBoard kJpBoard[18] = {
    /* 1: el estadio abierto con torres de luz: arriba de las gradas del fondo, entre las torres. Los demas: parado en
       la cancha, al fondo (donde la arena japonesa tenia su pantalla). */
    { 347, -1127, 45 }, { 1321, -2765, 45 }, { 347, -1127, 45 }, { 347, -1127, 45 }, { 347, -1127, 45 }, { 347, -1127, 45 },
    { 347, -1127, 45 }, { 347, -1127, 45 }, { 347, -1127, 45 }, { 347, -1127, 45 }, { 347, -1127, 45 }, { 347, -1127, 45 },
    { 347, -1127, 45 }, { 347, -1127, 45 }, { 347, -1127, 45 }, { 347, -1127, 45 }, { 347, -1127, 45 }, { 347, -1127, 45 },
};
static s32 sJpStadium = 1;
static s32 sJpBoardPos[3];
/* out: y, z, escala. (Para probar: estadioprueba.txt = estadio + 1; tablay, tablaz (positivo = hacia atras), tablae.) */
void ps_jp_board_pos(s32* out) { out[0] = sJpBoardPos[0]; out[1] = sJpBoardPos[1]; out[2] = sJpBoardPos[2]; }
static GraphNode* sJpArenaNode;
/* cuelga un nodo mas junto al estadio (el edificio de la pantalla, que arma port/jp) */
void ps_jp_arena_add(GraphNode* node) { if (sJpArenaNode != NULL && node != NULL) func_80012094(sJpArenaNode, node); }
/* Los nodos del estadio y del edificio viven en memoria fija, no en el pool del juego: la escena japonesa guarda y
   suelta estados del pool por su cuenta, y lo que se pide a media escena puede quedar liberado y pisado mientras sigue
   colgado del arbol (en la consola terminaba en un salto a datos). El archivo del estadio se carga al empezar la
   escena, debajo de todo lo que ella pide, y dura hasta que termina. */
static u8 sJpStadMem[0x18000] __attribute__((aligned(16)));
static u8 sJpBoardMem[0x4000] __attribute__((aligned(16)));
static GraphNode* sJpCapa[3];
static u32 ps_mem_used(const u8* m, u32 size) { while (size > 0 && m[size - 1] == 0) size--; return size; }
void* ps_jp_board_block(void) { memset(sJpBoardMem, 0, sizeof(sJpBoardMem)); return func_80002DA4((void*)sJpBoardMem, sizeof(sJpBoardMem)); }
void ps_jp_board_done(void) { ps_printf("[jp] edificio: %x de %x bytes de nodos\n", (unsigned)ps_mem_used(sJpBoardMem, sizeof(sJpBoardMem)), (unsigned)sizeof(sJpBoardMem)); }
static void ps_jp_board_setup(s32 idx) {
    extern s32 ps_flag(const char* name, s32 dflt);
    s32 t = ps_flag("estadioprueba", 0);
    if (t > 0 && t <= 18) idx = t - 1;
    if ((u32)idx >= 18) idx = 1;
    sJpStadium = idx;
    /* la presentacion va en el marco dorado (el mismo de la batalla, frente a las gradas del fondo), no en el edificio */
    sJpBoardPos[0] = PS_GOLD_CY; sJpBoardPos[1] = PS_GOLD_Z; sJpBoardPos[2] = kJpBoard[idx].scale;
}
/* El marco dorado en la escena de entrada: con la camara en perspectiva de la arena que se acaba de dibujar (desde start).
   El hueco oscuro lo tapa la presentacion (ps_jp_grid_rect). Igual al de la batalla (fragment62_2EA8E0.c). */
static Vtx sGoldVtx[8];
static void ps_gold_vtx(Vtx* v, s32 x, s32 y, s32 z, u8 r, u8 g, u8 b) {
    v->v.ob[0] = x; v->v.ob[1] = y; v->v.ob[2] = z; v->v.flag = 0; v->v.tc[0] = 0; v->v.tc[1] = 0;
    v->v.cn[0] = r; v->v.cn[1] = g; v->v.cn[2] = b; v->v.cn[3] = 255;
}
void ps_jp_gold_frame(Gfx* start) {
    Gfx* g;
    u32 pl = 0, pm = 0, cur = 0;
    const s32 x0 = -PS_GOLD_W / 2, y1 = PS_GOLD_CY + PS_GOLD_H / 2, w = PS_GOLD_W, h = PS_GOLD_H, z = PS_GOLD_Z;
    for (g = start; g < gDisplayListHead; g++) {
        if ((g->words.w0 >> 24) == 0xDA) {
            const u32 p = (g->words.w0 & 0xFF) ^ G_MTX_PUSH;
            if (p & G_MTX_PROJECTION) {
                if (p & G_MTX_LOAD) cur = g->words.w1;
                else if (cur) { pl = cur; pm = g->words.w1; cur = 0; }
            }
        }
    }
    if (!pl || !pm) return;
    ps_gold_vtx(&sGoldVtx[0], x0 - 30, y1 + 30, z - 4, 230, 190, 40);
    ps_gold_vtx(&sGoldVtx[1], x0 + w + 30, y1 + 30, z - 4, 230, 190, 40);
    ps_gold_vtx(&sGoldVtx[2], x0 + w + 30, y1 - h - 30, z - 4, 160, 120, 20);
    ps_gold_vtx(&sGoldVtx[3], x0 - 30, y1 - h - 30, z - 4, 160, 120, 20);
    ps_gold_vtx(&sGoldVtx[4], x0 - 12, y1 + 12, z - 2, 14, 22, 80);
    ps_gold_vtx(&sGoldVtx[5], x0 + w + 12, y1 + 12, z - 2, 14, 22, 80);
    ps_gold_vtx(&sGoldVtx[6], x0 + w + 12, y1 - h - 12, z - 2, 8, 14, 50);
    ps_gold_vtx(&sGoldVtx[7], x0 - 12, y1 - h - 12, z - 2, 8, 14, 50);
    gDPPipeSync(gDisplayListHead++);
    gSPMatrix(gDisplayListHead++, pl, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gDisplayListHead++, pm, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    gSPMatrix(gDisplayListHead++, (u32)&D_8006F010 & 0x1FFFFFFF, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gSPClearGeometryMode(gDisplayListHead++, G_LIGHTING | G_FOG | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_CULL_BOTH);
    gSPSetGeometryMode(gDisplayListHead++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(gDisplayListHead++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
    gSPVertex(gDisplayListHead++, sGoldVtx, 8, 0);
    gSP2Triangles(gDisplayListHead++, 0, 3, 2, 0, 0, 2, 1, 0);
    gSP2Triangles(gDisplayListHead++, 4, 7, 6, 0, 4, 6, 5, 0);
    gDPPipeSync(gDisplayListHead++);
}
static s32 ps_jp_mode_stadium(void) {
    static const u8 kIdx[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01 };
    const s32 mode = (D_800AE540.unk_0000 == 0x10 || D_800AE540.unk_0000 == 0xA) ? D_800AE540.unk_0001 : D_800AE540.unk_0000;
    s32 idx = (u32)mode < sizeof(kIdx) ? kIdx[mode] : 0;
    if (mode == 7) idx += D_800AE540.unk_0002;
    return idx;
}
void ps_jp_us_stadium_load(void) {
    static const u8 kIdx[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01 };
    static const s32 kCapa[3] = { 0, 1, 3 };
    BinArchive* a;
    PsStadiumFn fn;
    MemoryBlock* pool;
    s32 mode, idx, k;
    sJpCapa[0] = sJpCapa[1] = sJpCapa[2] = NULL;
    sJpArenaNode = NULL;
    mode = (D_800AE540.unk_0000 == 0x10 || D_800AE540.unk_0000 == 0xA) ? D_800AE540.unk_0001 : D_800AE540.unk_0000;
    idx = (u32)mode < sizeof(kIdx) ? kIdx[mode] : 0;
    if (mode == 7) idx += D_800AE540.unk_0002;
    ps_jp_board_setup(idx);
    idx = sJpStadium;
    a = func_800044F4(stadium_models_ROM_START, stadium_models_ROM_END, 1, 1);
    if (a == NULL) return;
    fn = (PsStadiumFn)func_8000484C(a, idx);
    if (fn == NULL) return;
    memset(sJpStadMem, 0, sizeof(sJpStadMem));
    pool = func_80002DA4((void*)sJpStadMem, sizeof(sJpStadMem));
    for (k = 0; k < 3; k++) {
        void* layout = PS_FRAG_CALL(fn, kCapa[k], 0);
        sJpCapa[k] = layout != NULL ? process_geo_layout(pool, layout) : NULL;
    }
    ps_printf("[jp] estadio de la US: modo %d, archivo %d, %x de %x bytes de nodos\n", (int)mode, (int)idx, (unsigned)ps_mem_used(sJpStadMem, sizeof(sJpStadMem)), (unsigned)sizeof(sJpStadMem));
}
void ps_jp_us_stadium_attach(GraphNode* parent) {
    s32 k;
    GraphNode* capa[3];
    s32 idx = sJpStadium;
    if (parent == NULL) return;
    { /* la arena japonesa: camara -> raiz del modelo (04) -> perspectiva (06) -> [pantalla (16), modelo (0e)] */
        u8* n04 = *(u8**)((u8*)parent + 0xC);
        u8* n06 = n04 != NULL ? *(u8**)(n04 + 0xC) : NULL;
        u8* n = n06 != NULL ? *(u8**)(n06 + 0xC) : NULL;
        u8* first = n;
        s32 guard = 0;
        if (n06 == NULL || n06[0] != 0x06) return;
        while (n != NULL && guard++ < 16) { if (n[0] == 0x0E) n[1] &= ~1; n = *(u8**)(n + 8); if (n == first) break; }
        parent = (GraphNode*)n06;
        sJpArenaNode = parent;
    }
    for (k = 0; k < 3; k++) { capa[k] = sJpCapa[k]; if (capa[k] != NULL) func_80012094(parent, capa[k]); }
    (void)idx;
    /* la torre central ya no se usa: en su lugar va el edificio de la pantalla (port/jp/ps_jp_scene.c) */
    if (0 && idx == 1) for (k = 0; k < 3; k++) if (capa[k] != NULL) { u8* h = *(u8**)((u8*)capa[k] + 0xC); ps_torre_nodos(h != NULL ? h : (u8*)capa[k], 0); }
}

/* El perfil en las pantallas de la US (VS, resultados, batalla): el nombre se escribe en la sesion y el retrato se
   cambia donde esas pantallas lo piden. Lado 0 = el jugador. */
void ps_prof_apply(void) {
    unk_D_800AE540_0004* t = D_800AE540.unk_1194[0].unk_08[0];
    s32 k;
    if (t == NULL || !gPsProfName[0]) return;
    for (k = 0; k < 11 && gPsProfName[k]; k++) t->unk_008[k] = gPsProfName[k];
    for (; k < 16; k++) t->unk_008[k] = 0;
    /* el juego tambien lo copia al equipo (unk_214->unk_014, lo que muestran el VS y los resultados); entre peleas de
       una copa o gimnasio se vuelve a armar desde ahi, asi que se escribe en los dos */
    if (t->unk_214 != NULL) {
        char* n = t->unk_214->unk_014;
        for (k = 0; k < 11 && gPsProfName[k]; k++) n[k] = gPsProfName[k];
        for (; k < 16; k++) n[k] = 0;
    }
}
u8* ps_prof_portrait(BinArchive* archive, s32 idx, s32 side) {
    if (side == 0) {
        if (gPsProfPortrait >= 0x100 && gPsProfPortrait < 0x120 && ps_jp_available()) { /* uno de la ROM japonesa */
            u8 e[16]; u32 off, size; void* p;
            if (ps_jp_rom_read(0x2F5940 + 16 + (u32)(gPsProfPortrait - 0x100) * 16, e, 16) == 16) {
                off = ((u32)e[0] << 24) | ((u32)e[1] << 16) | ((u32)e[2] << 8) | e[3];
                size = ((u32)e[4] << 24) | ((u32)e[5] << 16) | ((u32)e[6] << 8) | e[7];
                p = size != 0 && size <= 0x4000 ? ps_jp_load(0x2F5940 + off, 0x2F5940 + off + size, 0) : NULL;
                if (p != NULL) return p;
            }
        } else if (gPsProfPortrait >= 0 && gPsProfPortrait < 54) idx = gPsProfPortrait;
    }
    return func_8000484C(archive, idx);
}
/* Cada archivo esta comprimido aparte: se descomprime al pedirlo (el juego lo leeria de su propia ROM) */
void* ps_jp_BinArchive_GetFile(BinArchive* archive, s32 n) {
    BinArchiveFile* e;
    if (archive == NULL || archive != sJpArchive) return func_8000484C(archive, n);
    if (n >= 0x40) { void* p = jp_us_portrait(n - 0x40); if (p != NULL) return p; n = 23; }
    if (n < 0 || (u32)n >= archive->num_files) return NULL;
    e = (BinArchiveFile*)((u8*)archive + sizeof(BinArchive) + n * sizeof(BinArchiveFile));
    if (e->unk_08 == 0) e->unk_08 = (u32)(uintptr_t)ps_jp_load(sJpArchiveRom + e->offset, sJpArchiveRom + e->offset + e->size, 0);
    return (void*)(uintptr_t)e->unk_08;
}
/* Al terminar la escena: olvidar los fragments japoneses */
void ps_jp_scene_end(void) {
    u32 i;
    for (i = 0; i < gPsJpImagesCount; i++)
        if (sJpLoaded[i]) { Memmap_ClearFragmentMemmap(((PS_JP_VRAM(gPsJpImages[i]) & 0x0FF00000) >> 20) - 0x10); sJpLoaded[i] = 0; }
}

/* ---------------- del juego a la escena ---------------- */
#include "src/29BA0.h"
#include "jp/ps_jp_in.h"
void func_8001A46C(void* renderer, s32 slot, void* mon, u16 fill);
s32 jp_ps_jp_scene_run(const PsJpIn* in) __attribute__((weak));

int gPsGeoTrace; /* prueba: anotar los siguientes N comandos de geo layout */
#include <stdarg.h>
int vsnprintf(char*, unsigned int, const char*, va_list);
void ps_jp_tracef(const char* fmt, ...) { (void)fmt; }
void ps_jp_trace(const char* s) { (void)s; }

/* Sonido de la entrada, con lo de la US: la musica de su pantalla de VS y tres efectos de esa misma pantalla en los
   golpes de la revelacion (160 pasos: entran las tarjetas, cae el "VS", giran a los paneles). */
extern void func_8000D1F0(s32);
extern void func_8000D278(s32);
extern s32 func_8004DF94(s32, s32, s32);
extern s32 func_80048B90(u32);
static s32 sJpReveal = -1;
/* Sonidos de la entrada, por lista (el host la puede cambiar con vs_sfx.txt). Cada uno: cuando y que.
   Cuando: 0 en adelante = paso desde que las flamas llenan la pantalla (justo antes de que salgan las tarjetas);
   -1000 + n = cuadro n desde que empieza la escena; -2 = sin usar.
   Que: tipo << 16 | valor: 1 musica de la US, 2 efecto de la pantalla de VS de la US, 3 callar todo en "valor" cuadros,
   4 efecto de menu de la US.
   En la consola la musica tarda unos segundos en arrancar desde que se pide, y el primer efecto carga antes su banco:
   por eso la musica se pide al empezar la escena (para que se oiga al llegar a las flamas) y el banco de efectos se
   carga antes (el efecto 07 solo lo carga, no suena). */
#define PS_JP_SND_MAX 8
s32 gPsJpSnd[PS_JP_SND_MAX][2] = {
    { -1000 + 70, (1 << 16) | 0x31 },  /* se pide la musica de la pantalla de VS (tarda en arrancar: entra ya con las flamas) */
    { -1000 + 110, (2 << 16) | 0x07 }, /* despues de la musica, se carga el banco de efectos (no suena) */
    { 106, (2 << 16) | 0x0F },         /* el giro de las cajas con los Pokemon, 2 pasos antes */
    { 150, (3 << 16) | 30 },           /* ya sono el giro: se calla todo */
    { -2, 0 }, { -2, 0 }, { -2, 0 }, { -2, 0 },
};
static s32 sJpFrame;
s32 gPsJpSceneOn; /* 1 mientras corre la escena: el host guarda y repone el estado de niebla del dibujo */
static void jp_snd_run(s32 when) {
    s32 i;
    for (i = 0; i < PS_JP_SND_MAX; i++) {
        s32 v = gPsJpSnd[i][1] & 0xFFFF;
        if (gPsJpSnd[i][0] != when) continue;
        ps_printf("[jp] sonido: cuando %d, tipo %d, valor %x\n", (int)when, (int)(gPsJpSnd[i][1] >> 16), (unsigned)(gPsJpSnd[i][1] & 0xFFFF));
        switch (gPsJpSnd[i][1] >> 16) {
            case 1: func_8000D1F0(v); break;
            case 2: func_8004DF94(0x01100000 | v, 0, 0); break;
            case 3: func_8000D278(v); break;
            case 4: func_80048B90(v); break; /* efecto de menu */
        }
    }
}
s32 ps_jp_music(s32 jpTrack) { (void)jpTrack; return 0; }                        /* (lo decide la lista) */
s32 ps_jp_music_stop(s32 frames) { (void)frames; return 0; }                    /* (lo decide la lista) */
s32 ps_jp_audio_cmd(u32 cmd, s32 a, s32 b) { (void)a; (void)b; ps_printf("[jp] comando de audio %x (cuadro %d)\n", (unsigned)cmd, (int)sJpFrame); if (cmd == 0x3C) sJpReveal = 0; return 0; }
void ps_jp_frame(void) { /* cada cuadro de la escena; el paso sigue contando hasta que termina */
    if (sJpFrame < 900) jp_snd_run(-1000 + sJpFrame++);
    if (sJpReveal < 0) return;
    jp_snd_run(sJpReveal);
    if (sJpReveal < 30000) sJpReveal++;
}
/* La imagen de 320x240 donde la escena dibuja el fuego y las tarjetas: no es la pantalla (ver gGfxConvertNotScreen) */
u32 gPsNotScreenFb;
#include "src/6BC0.h"
unk_D_80068BB0* ps_jp_GfxImage_Allocate(s32 fmt, s32 size, s32 width, s32 height, s32 side) {
    unk_D_80068BB0* img = func_80006314(fmt, size, width, height, side);
    if (img != NULL && width == 320) gPsNotScreenFb = (u32)(uintptr_t)img->img_p;
    return img;
}
/* La pantalla gigante solo se dibuja cuando la escena ya dibujo su imagen al menos una vez: antes, esa memoria trae
 * lo que hubiera y el interprete la guardaria como textura. */
static int sJpImageDrawn;
int ps_jp_image_drawn(void) { return sJpImageDrawn; }
void ps_jp_GfxImage_SetRenderTarget(Gfx** gfx_p, unk_D_80068BB0* img) {
    if (img != NULL && (u32)(uintptr_t)img->img_p == gPsNotScreenFb) sJpImageDrawn = 1;
    func_80006498(gfx_p, img);
}
/* icono de un Pokemon de la escena: lo dibuja el juego con su propio Pokemon */
void ps_jp_icon_slot(void* renderer, s32 slot, const void* usMon, u16 fill) {
    static unk_func_80026268_arg0 none;
    func_8001A46C(renderer, slot, usMon ? (void*)usMon : (void*)&none, fill);
}

void* jp_ps_jp_board_build(void) __attribute__((weak));
s32 jp_ps_jp_board_mtx(f32 (*out)[4]) __attribute__((weak));
/* carga los fragments de la escena (battle: los de la batalla, sin el de 0x82200000, que usa el mismo numero que un
   fragment de la US) */
static s32 sJpBattleLoaded; /* la batalla los cargo y los tiene que soltar al terminar */
static void ps_jp_images_load(s32 battle) {
    u32 i, pass;
    /* dos vueltas, para que las referencias entre fragments encuentren al otro ya ubicado */
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < gPsJpImagesCount; i++) {
            const u32 v = PS_JP_VRAM(gPsJpImages[i]);
            if (v == 0x82400000u || (battle && v == 0x82200000u)) continue;
            ps_jp_Fragment_Load(((v & 0x0FF00000) >> 20) - 0x10, gPsJpImages[i].romStart, gPsJpImages[i].romEnd);
        }
}
void ps_jp_scene_end(void);
int ps_jp_scene_ready(void);
/* Batalla: el edificio de la pantalla gigante de la entrada japonesa, junto al estadio, donde lo pone la entrada. Su
   hueco lo llena la pantalla de abajo (fragment62). 1 si se puso. */
s32 ps_jp_battle_board(GraphNode* parent) {
    GraphNode* node;
    if (parent == NULL || jp_ps_jp_board_build == NULL || !ps_jp_scene_ready()) return 0;
    /* el edificio y su dibujo estan en los fragments de la escena; al terminar la entrada se sueltan (ps_jp_scene_end),
       asi que la batalla los vuelve a cargar y los suelta al acabar (ps_jp_battle_board_end) */
    ps_jp_images_load(1);
    sJpBattleLoaded = 1;
    ps_jp_board_setup(ps_jp_mode_stadium());
    /* en la batalla va en el mismo lugar que en la entrada japonesa (kJpBoard, segun el estadio) */
    node = (GraphNode*)jp_ps_jp_board_build();
    if (node == NULL) return 0;
    func_80012094(parent, node);
    return 1;
}
void ps_jp_battle_board_end(void) { if (sJpBattleLoaded) { sJpBattleLoaded = 0; ps_jp_scene_end(); } }
s32 ps_jp_battle_board_mtx(f32 (*out)[4]) { return jp_ps_jp_board_mtx != NULL ? jp_ps_jp_board_mtx(out) : 0; }

/* 1 si la escena esta compilada en este bloque y la ROM japonesa esta en la SD */
int ps_jp_scene_ready(void) { return jp_ps_jp_scene_run != NULL && gPsJpImagesCount != 0 && ps_jp_available(); }

/* La entrada japonesa para la batalla que esta armada en D_800AE540. Regresa 1 si se mostro. */
int ps_jp_scene_play(void) {
    static PsJpIn in;
    s32 s, k;
    /* La entrada japonesa esta apagada por ahora: siempre va la pantalla de VS de la US */
    if (1) return 0;
    if (!ps_jp_scene_ready()) return 0;
    {   /* el rival (copas, castillo, Mewtwo, eventos): lo arma la pantalla de VS de la US al empezar (fragment63); como
           esa no se muestra, se arma aqui antes de leer los equipos (si no, quedaban sustitutos y la seleccion vacia) */
        extern void func_8002C128(void);
        func_8002C128();
    }
    ps_prof_apply();
    memset(&in, 0, sizeof in);
    in.mode = 0; in.ruleCategory = 0; in.modeSelection = 0; in.round = 0; /* por ahora: como Free Battle (arena techada, sin titulo) */
    for (s = 0; s < 2; s++) {
        unk_D_800AE540_0004* t = D_800AE540.unk_1194[s].unk_08[0];
        PsJpSideIn* si = &in.side[s];
        if (t == NULL || t->unk_214 == NULL) return 0;
        for (k = 0; k < 15 && t->unk_008[k]; k++) si->name[k] = t->unk_008[k];
        memcpy(si->title, "Trainer", 8);
        si->trainerIndex = t->unk_214->unk_003 < 54 ? 0x40 + t->unk_214->unk_003 : 23; /* su retrato de la US */
        if (s == 0) { /* el jugador: lo que eligio en su perfil */
            if (gPsProfPortrait >= 0x100 && gPsProfPortrait < 0x120) si->trainerIndex = (u8)(gPsProfPortrait - 0x100);
            else if (gPsProfPortrait >= 0 && gPsProfPortrait < 54) si->trainerIndex = (u8)(0x40 + gPsProfPortrait);
            if (gPsProfName[0]) { memset(si->name, 0, sizeof si->name); for (k = 0; k < 11 && gPsProfName[k]; k++) si->name[k] = gPsProfName[k]; }
        }
        si->count = 0; /* los que trae el equipo (no cuantos se escogen): la escena muestra los 6 */
        for (k = 0; k < 6; k++) if (*(const u8*)&t->unk_214->unk_028[k] != 0) si->count = k + 1;
        if (si->count == 0) si->count = 6;
        for (k = 0; k < 6; k++) {
            unk_func_80026268_arg0* m = &t->unk_214->unk_028[k];
            si->mon[k] = m;
            memcpy(si->head[k], m, 0x30);
            memcpy(si->nick[k], m->unk_30, 11);
        }
    }
    main_pool_push_state('JPVS');
    ps_jp_battle_board_end();
    ps_jp_images_load(0); /* el fragment de la escena y sus datos */
    ps_jp_us_stadium_load();
    { extern int gPsGeoText; gPsGeoText = 1; }
    ps_printf("[jp] escena: inicio\n");
    sJpReveal = -1;
    sJpFrame = 0;
    gPsJpSceneOn = 1;
    jp_ps_jp_scene_run(&in);
    gPsJpSceneOn = 0;
    ps_printf("[jp] escena: fin\n");
    gPsNotScreenFb = 0;
    { extern int gPsGeoText; gPsGeoText = 0; }
    sJpImageDrawn = 0;
    sJpArchive = NULL;
    ps_jp_scene_end();
    main_pool_pop_state('JPVS');
    {   /* La memoria que uso la escena se deja en ceros: la pantalla que sigue lee campos que no inicializa, y con lo
           que dejaba la escena salia a veces blanquecina. */
        u32 n = main_pool_get_available();
        if (n > 0x4000) {
            void* p;
            main_pool_push_state('JPCL');
            p = main_pool_alloc(n - 0x2000, 0);
            if (p != NULL) memset(p, 0, n - 0x2000);
            main_pool_pop_state('JPCL');
        }
    }
    return 1;
}

#define JP_FRAG_ID(vram) ((((u32)(vram) & 0x0FF00000) >> 0x14) - 0x10)
/* Prueba (jptest.txt en la SD): carga los bloques de la escena y anota lo que encontro */
void ps_jp_test(void) {
    static const struct { u32 vram, a, b; } k[] = {
        { 0x8F200000, 0x3553C0, 0x35C390 }, { 0x8F500000, 0x35C390, 0x35E790 }, { 0x8F600000, 0x35E790, 0x369F50 },
        { 0x8EB00000, 0x369F50, 0x37B090 }, { 0x8F300000, 0x320820, 0x326700 }, { 0x8F900000, 0x3139C0, 0x317EF0 },
        { 0x8EC00000, 0x37B090, 0x380D60 },
    };
    u32 i;
    if (!ps_jp_available()) return;
    main_pool_push_state('JPTS');
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        Fragment* f = ps_jp_fragment(JP_FRAG_ID(k[i].vram), k[i].a, k[i].b);
        if (f) ps_printf("[jp] %08x: cargado en %08x, rom %x ram %x, reloc en %x, mapa -> %08x\n", k[i].vram, (u32)(uintptr_t)f, f->sizeInRom, f->sizeInRam,
                         f->relocOffset, (u32)Memmap_GetFragmentVaddr((void*)(uintptr_t)(k[i].vram + 0x20)));
    }
    main_pool_pop_state('JPTS');
}

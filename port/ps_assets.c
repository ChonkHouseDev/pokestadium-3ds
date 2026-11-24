/* Carga de assets: el juego pide rangos de ROM; aqui se sirven desde pokestadium.o2r.
 *
 *  - PERS-SZP: Torch ya los guarda descomprimidos, se copian al pool tal cual.
 *  - Encabezados de archivos (BinArchive): se convierten a little-endian al cargarlos.
 *  - Lo demas (Yay0, JPEG, audio, tablas) se lee crudo y el juego lo procesa como en el N64.
 *
 * Pendiente (formato por formato): convertir el contenido de cada tipo de asset a little-endian. */
#include "global.h"
#include "src/memory.h"
#include "ps_assets.h"
#include "os/ps_os.h"
#include <string.h>

PsResGetFn gPsResGet;

/* Trazador (tools/tracer): aviso de que un asset quedo en memoria. En el juego normal no hace nada. */
__attribute__((weak)) void ps_trace_asset(void* dst, unsigned int size, unsigned int assetIndex, unsigned int base) {
    (void)dst; (void)size; (void)assetIndex; (void)base;
}

__attribute__((weak)) void ps_trace_clear(void* addr, u32 size) { (void)addr; (void)size; }

/* Aviso de que un rango de RAM cambio de dueno o de contenido (ver PS_MEM_* en ps_assets.h). El host del 3DS
 * lo usa para invalidar los modelos horneados; en las demas plataformas no hace nada. */
__attribute__((weak)) void ps_mem_event(u32 kind, void* addr, u32 size) { (void)kind; (void)addr; (void)size; }

#define IS_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)

extern void* ps_orig_func_80003DC4(u8* romStart, u8* romEnd, s32 arg2, s32 arg3);

const PsAssetEntry* ps_asset_at(unsigned int rom) {
    int lo = 0, hi = (int)gPsAssetIndexCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (gPsAssetIndex[mid].rom == rom) return &gPsAssetIndex[mid];
        if (gPsAssetIndex[mid].rom < rom) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

const PsAssetEntry* ps_asset_containing(unsigned int rom) {
    int lo = 0, hi = (int)gPsAssetIndexCount - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (gPsAssetIndex[mid].rom <= rom) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    /* los encabezados de archivo se solapan con su primer archivo: preferir el rango que contiene */
    for (int i = best; i >= 0 && i > best - 4; i--) {
        const PsAssetEntry* e = &gPsAssetIndex[i];
        if (rom >= e->rom && rom < e->rom + e->size) return e;
    }
    return NULL;
}

static int is_archive_header(const PsAssetEntry* e) {
    size_t n = strlen(e->path);
    return n > 7 && strcmp(e->path + n - 7, "_header") == 0;
}

/* BinArchive: u16 flags, u16 segmento, u32 rom, u32 total, u32 n; luego n x {u32 off, u32 size, u32 cache, u8[4]} */
static void archive_header_to_native(u8* p, unsigned int size) {
    for (unsigned int i = 0; i + 4 <= size; i += 4) {
        if (i == 0) {
            u8 a = p[0], b = p[1], c = p[2], d = p[3];
            p[0] = b; p[1] = a; p[2] = d; p[3] = c;
        } else if (i >= 0x10 && ((i - 0x10) & 0xF) == 0xC) {
            /* bytes sueltos: se dejan igual */
        } else {
            u8 a = p[i], b = p[i + 1];
            p[i] = p[i + 3]; p[i + 1] = p[i + 2]; p[i + 2] = b; p[i + 3] = a;
        }
    }
}

unsigned int ps_rom_read(unsigned int rom, void* dst, unsigned int size) {
    unsigned int done = 0;
    while (done < size) {
        const PsAssetEntry* e = ps_asset_containing(rom + done);
        if (e == NULL || e->type != PS_ASSET_RAW) {
            ps_printf("[rom] sin datos crudos para %07x (%s)\n", rom + done, e ? e->path : "fuera de los assets");
            break;
        }
        unsigned int resSize = 0;
        const u8* data = gPsResGet(e->path, &resSize);
        if (data == NULL) { ps_printf("[rom] falta el recurso %s\n", e->path); break; }
        unsigned int off = rom + done - e->rom;
        unsigned int n = size - done;
        if (off + n > resSize) n = resSize - off;
        memcpy((u8*)dst + done, data + off, n);
        if (IS_LITTLE_ENDIAN && is_archive_header(e) && off == 0) archive_header_to_native((u8*)dst + done, n);
        ps_trace_asset((u8*)dst + done, n, (unsigned int)(e - gPsAssetIndex), off);
        ps_mem_event(PS_MEM_ASSET, (u8*)dst + done, n);
        done += n;
    }
    return done;
}

/* Reemplaza func_80003DC4 (src/3FB0.c): cargar un rango de ROM al pool, descomprimido. */
void* func_80003DC4(u8* romStart, u8* romEnd, s32 arg2, s32 arg3) {
    const PsAssetEntry* e = ps_asset_at((u32)(uintptr_t)romStart);
    if (e != NULL && e->type == PS_ASSET_PERS_SZP) {
        unsigned int size = 0;
        const u8* data = gPsResGet(e->path, &size);
        if (data == NULL) { ps_printf("[assets] falta %s\n", e->path); return NULL; }
        u8* dst = main_pool_alloc(ALIGN16(size), arg2);
        if (dst != NULL) {
            memcpy(dst, data, size);
            ps_trace_asset(dst, size, (unsigned int)(e - gPsAssetIndex), 0);
            ps_mem_event(PS_MEM_ASSET, dst, size);
        }
        return dst;
    }
    return ps_orig_func_80003DC4(romStart, romEnd, arg2, arg3);
}

/* Lectura cruda de la ROM por DMA (usada por func_80003B30 y el audio) */
s32 ps_pi_read(u32 rom, void* dst, u32 size) { return (s32)ps_rom_read(rom, dst, size); }

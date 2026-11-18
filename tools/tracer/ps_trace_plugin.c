/* Plugin de QEMU: registra como lee el juego cada asset (offset y tamano del acceso).
 *
 * El juego corre en big-endian (como en el N64). El host del trazador avisa con una syscall
 * "marcador" cada vez que copia un asset a memoria (direccion, tamano, id del asset, offset base);
 * el plugin mantiene el mapa de regiones y, en cada lectura/escritura hecha desde el codigo del juego,
 * anota (asset, offset, ancho). Al salir escribe el mapa: de ahi Torch sabe que voltear.
 *
 * Uso: qemu-mips -plugin ./libps_trace.so,out=mapa.txt,code=0x400000-0x900000,skip=rangos.txt programa ...
 */
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define MARK_SYSCALL 4999         /* syscall inexistente en MIPS o32: solo la ve el plugin */
#define MARK_ASSET   1            /* a1=dir, a2=tamano, a3=id del asset, a4=offset dentro del asset */
#define MARK_CLEAR   2            /* a1=dir, a2=tamano: la memoria deja de ser de un asset */

typedef struct { uint32_t start, end, asset, base; } Region;
static GArray* sRegions;          /* ordenadas por start, sin solapes */
static GHashTable* sAccess;       /* clave (asset<<32 | offset) -> mascara de anchos (1,2,4,8) */
static uint64_t sCodeLo = 0, sCodeHi = UINT64_MAX;
static GArray* sSkip;             /* rangos de PC a ignorar (memcpy, bzero, copias genericas) */
static char* sOut = "ps_trace_map.txt";
static uint64_t sHits;
static uint64_t sWatchLo = 0, sWatchHi = 0; /* watch=lo-hi: reportar escrituras (pc) en ese rango */

static void region_remove(uint32_t s, uint32_t e) {
    for (guint i = 0; i < sRegions->len;) {
        Region* r = &g_array_index(sRegions, Region, i);
        if (r->end <= s || r->start >= e) { i++; continue; }
        if (r->start < s && r->end > e) {           /* partir en dos */
            Region tail = *r;
            tail.base += e - r->start;
            tail.start = e;
            r->end = s;
            g_array_insert_val(sRegions, i + 1, tail);
            i += 2;
        } else if (r->start < s) { r->end = s; i++; }
        else if (r->end > e) { r->base += e - r->start; r->start = e; i++; }
        else g_array_remove_index(sRegions, i);
    }
}

static void region_add(uint32_t s, uint32_t size, uint32_t asset, uint32_t base) {
    region_remove(s, s + size);
    Region r = { s, s + size, asset, base };
    guint i = 0;
    while (i < sRegions->len && g_array_index(sRegions, Region, i).start < s) i++;
    g_array_insert_val(sRegions, i, r);
}

static Region* region_find(uint32_t a) {
    guint lo = 0, hi = sRegions->len;
    while (lo < hi) {
        guint mid = (lo + hi) / 2;
        Region* r = &g_array_index(sRegions, Region, mid);
        if (a < r->start) hi = mid;
        else if (a >= r->end) lo = mid + 1;
        else return r;
    }
    return NULL;
}

static void on_syscall(qemu_plugin_id_t id, unsigned int vcpu, int64_t num, uint64_t a1, uint64_t a2,
                       uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8) {
    if (num != MARK_SYSCALL) return;
    if (a1 == MARK_ASSET) region_add((uint32_t)a2, (uint32_t)a3, (uint32_t)a4, (uint32_t)a5);
    else if (a1 == MARK_CLEAR) region_remove((uint32_t)a2, (uint32_t)(a2 + a3));
}

static void on_mem(unsigned int vcpu, qemu_plugin_meminfo_t info, uint64_t vaddr, void* udata) {
    if (sWatchHi && vaddr >= sWatchLo && vaddr < sWatchHi && qemu_plugin_mem_is_store(info)) {
        fprintf(stderr, "[watch] escritura en %08" PRIx64 " desde pc %08" PRIx64 "\n", vaddr, (uint64_t)(uintptr_t)udata);
    }
    if (sRegions->len == 0) return;
    Region* r = region_find((uint32_t)vaddr);
    if (!r) return;
    unsigned size = 1u << qemu_plugin_mem_size_shift(info);
    uint64_t off = r->base + ((uint32_t)vaddr - r->start);
    gpointer key = GSIZE_TO_POINTER(((uint64_t)r->asset << 32) | off);
    guint mask = GPOINTER_TO_UINT(g_hash_table_lookup(sAccess, key));
    g_hash_table_insert(sAccess, key, GUINT_TO_POINTER(mask | size));
    sHits++;
}

static int skip_pc(uint64_t pc) {
    if (pc < sCodeLo || pc >= sCodeHi) return 1;
    for (guint i = 0; i < sSkip->len; i += 2) {
        if (pc >= g_array_index(sSkip, uint64_t, i) && pc < g_array_index(sSkip, uint64_t, i + 1)) return 1;
    }
    return 0;
}

static void on_tb(qemu_plugin_id_t id, struct qemu_plugin_tb* tb) {
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn* insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t pc = qemu_plugin_insn_vaddr(insn);
        if (skip_pc(pc) && !sWatchHi) continue;
        qemu_plugin_register_vcpu_mem_cb(insn, on_mem, QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_RW, (void*)(uintptr_t)pc);
    }
}

static gint cmp_key(gconstpointer a, gconstpointer b) {
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return x < y ? -1 : x > y;
}

static void on_plugin_exit(qemu_plugin_id_t id, void* p) {
    FILE* f = fopen(sOut, "w");
    GArray* keys = g_array_new(FALSE, FALSE, sizeof(uint64_t));
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, sAccess);
    while (g_hash_table_iter_next(&it, &k, &v)) { uint64_t key = GPOINTER_TO_SIZE(k); g_array_append_val(keys, key); }
    g_array_sort(keys, cmp_key);
    /* formato: <asset> <offset_hex> <mascara de anchos> */
    for (guint i = 0; i < keys->len; i++) {
        uint64_t key = g_array_index(keys, uint64_t, i);
        guint mask = GPOINTER_TO_UINT(g_hash_table_lookup(sAccess, GSIZE_TO_POINTER(key)));
        fprintf(f, "%u %x %u\n", (unsigned)(key >> 32), (unsigned)(key & 0xFFFFFFFF), mask);
    }
    fclose(f);
    fprintf(stderr, "[trace] %" PRIu64 " accesos, %u posiciones distintas -> %s\n", sHits, keys->len, sOut);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t* info, int argc, char** argv) {
    sRegions = g_array_new(FALSE, FALSE, sizeof(Region));
    sSkip = g_array_new(FALSE, FALSE, sizeof(uint64_t));
    sAccess = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (int i = 0; i < argc; i++) {
        if (g_str_has_prefix(argv[i], "out=")) sOut = g_strdup(argv[i] + 4);
        else if (g_str_has_prefix(argv[i], "watch=")) sscanf(argv[i] + 6, "%" SCNx64 "-%" SCNx64, &sWatchLo, &sWatchHi);
        else if (g_str_has_prefix(argv[i], "code=")) sscanf(argv[i] + 5, "%" SCNx64 "-%" SCNx64, &sCodeLo, &sCodeHi);
        else if (g_str_has_prefix(argv[i], "skip=")) {
            FILE* f = fopen(argv[i] + 5, "r");
            uint64_t a, b;
            while (f && fscanf(f, "%" SCNx64 " %" SCNx64, &a, &b) == 2) { g_array_append_val(sSkip, a); g_array_append_val(sSkip, b); }
            if (f) fclose(f);
        }
    }
    qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb);
    qemu_plugin_register_vcpu_syscall_cb(id, on_syscall);
    qemu_plugin_register_atexit_cb(id, on_plugin_exit, NULL);
    return 0;
}

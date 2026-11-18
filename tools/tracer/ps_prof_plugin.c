/* Plugin de QEMU: perfil plano por funcion (instrucciones ejecutadas, propias).
 * Uso: qemu-xxx -plugin ./libps_prof.so,syms=simbolos.txt,out=salida.txt programa ...
 * simbolos.txt: "direccion_hex nombre" (de nm), se ordena aqui. */
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct { uint64_t addr; char* name; uint64_t insns; } Fn;
static Fn* sFns;
static size_t sN;
static char* sOut = "ps_prof.txt";
typedef struct { Fn* f; uint64_t n; } Blk;

static int cmp_addr(const void* a, const void* b) {
    const Fn* x = a; const Fn* y = b;
    return x->addr < y->addr ? -1 : x->addr > y->addr;
}
static Fn* find(uint64_t pc) {
    size_t lo = 0, hi = sN;
    while (lo < hi) { size_t m = (lo + hi) / 2; if (sFns[m].addr <= pc) lo = m + 1; else hi = m; }
    return lo ? &sFns[lo - 1] : NULL;
}
static void on_exec(unsigned int vcpu, void* ud) { Blk* b = ud; b->f->insns += b->n; }
static void on_tb(qemu_plugin_id_t id, struct qemu_plugin_tb* tb) {
    Fn* f = find(qemu_plugin_tb_vaddr(tb));
    if (!f) return;
    Blk* b = g_new(Blk, 1); b->f = f; b->n = qemu_plugin_tb_n_insns(tb);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, on_exec, QEMU_PLUGIN_CB_NO_REGS, b);
}
static int cmp_insns(const void* a, const void* b) {
    const Fn* x = *(Fn* const*)a; const Fn* y = *(Fn* const*)b;
    return x->insns > y->insns ? -1 : x->insns < y->insns;
}
static void on_end(qemu_plugin_id_t id, void* p) {
    Fn** v = malloc(sN * sizeof(Fn*));
    uint64_t total = 0;
    for (size_t i = 0; i < sN; i++) { v[i] = &sFns[i]; total += sFns[i].insns; }
    qsort(v, sN, sizeof(Fn*), cmp_insns);
    FILE* o = fopen(sOut, "w");
    fprintf(o, "total %" PRIu64 "\n", total);
    for (size_t i = 0; i < sN && v[i]->insns; i++) fprintf(o, "%6.2f%% %12" PRIu64 " %s\n", 100.0 * v[i]->insns / total, v[i]->insns, v[i]->name);
    fclose(o);
}
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t* info, int argc, char** argv) {
    GArray* a = g_array_new(FALSE, FALSE, sizeof(Fn));
    for (int i = 0; i < argc; i++) {
        if (!strncmp(argv[i], "out=", 4)) sOut = argv[i] + 4;
        else if (!strncmp(argv[i], "syms=", 5)) {
            FILE* f = fopen(argv[i] + 5, "r");
            char line[512];
            while (f && fgets(line, sizeof line, f)) {
                char name[400]; unsigned long ad;
                if (sscanf(line, "%lx %399s", &ad, name) != 2) continue;
                Fn fn = { ad & ~1ul, g_strdup(name), 0 };
                g_array_append_val(a, fn);
            }
            if (f) fclose(f);
        }
    }
    sN = a->len; sFns = (Fn*)g_array_free(a, FALSE);
    qsort(sFns, sN, sizeof(Fn), cmp_addr);
    qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb);
    qemu_plugin_register_atexit_cb(id, on_end, NULL);
    return 0;
}

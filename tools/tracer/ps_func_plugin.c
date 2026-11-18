/* Plugin de QEMU: que funciones del juego se ejecutan y en que orden aparecen por primera vez.
 * Sirve para comparar dos builds (MIPS del trazador vs ARM del 3DS) y ver donde se separan.
 * Uso: qemu-xxx -plugin ./libps_func.so,syms=simbolos.txt,out=salida.txt programa ...
 * simbolos.txt: "direccion_hex nombre" por linea (de nm). */
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct { char* name; uint64_t count; uint64_t first; } Func;
static GHashTable* sByAddr;
static uint64_t sOrder;
static char* sOut = "ps_funcs.txt";
static GPtrArray* sAll;

static void on_exec(unsigned int vcpu, void* ud) {
    Func* f = ud;
    if (f->count++ == 0) f->first = ++sOrder;
}
static void on_tb(qemu_plugin_id_t id, struct qemu_plugin_tb* tb) {
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    Func* f = g_hash_table_lookup(sByAddr, GUINT_TO_POINTER((guint)pc));
    if (f) qemu_plugin_register_vcpu_tb_exec_cb(tb, on_exec, QEMU_PLUGIN_CB_NO_REGS, f);
}
static gint by_first(gconstpointer a, gconstpointer b) {
    const Func* x = *(Func* const*)a; const Func* y = *(Func* const*)b;
    return x->first < y->first ? -1 : x->first > y->first;
}
static void on_end(qemu_plugin_id_t id, void* p) {
    GPtrArray* used = g_ptr_array_new();
    for (guint i = 0; i < sAll->len; i++) { Func* f = g_ptr_array_index(sAll, i); if (f->count) g_ptr_array_add(used, f); }
    g_ptr_array_sort(used, by_first);
    FILE* o = fopen(sOut, "w");
    for (guint i = 0; i < used->len; i++) { Func* f = g_ptr_array_index(used, i); fprintf(o, "%s %" PRIu64 "\n", f->name, f->count); }
    fclose(o);
}
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t* info, int argc, char** argv) {
    sByAddr = g_hash_table_new(NULL, NULL);
    sAll = g_ptr_array_new();
    for (int i = 0; i < argc; i++) {
        if (!strncmp(argv[i], "out=", 4)) sOut = argv[i] + 4;
        else if (!strncmp(argv[i], "syms=", 5)) {
            FILE* f = fopen(argv[i] + 5, "r");
            char line[512];
            while (f && fgets(line, sizeof line, f)) {
                char name[400]; unsigned long a;
                if (sscanf(line, "%lx %399s", &a, name) != 2) continue;
                a &= ~1ul;
                if (g_hash_table_lookup(sByAddr, GUINT_TO_POINTER((guint)a))) continue;
                Func* fn = g_new0(Func, 1); fn->name = g_strdup(name);
                g_hash_table_insert(sByAddr, GUINT_TO_POINTER((guint)a), fn);
                g_ptr_array_add(sAll, fn);
            }
            if (f) fclose(f);
        }
    }
    qemu_plugin_register_vcpu_tb_trans_cb(id, on_tb);
    qemu_plugin_register_atexit_cb(id, on_end, NULL);
    return 0;
}

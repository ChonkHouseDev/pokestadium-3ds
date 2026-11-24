/* Reemplazos de funciones que en el decomp siguen en ensamblador MIPS, y arranque del juego. */
#include "global.h"
#include "os/ps_os.h"

const PsHost* gPsHost;

static u32 be32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

/* Yay0 (asm/us/yay0.s y asm/us/517A0.s): el flujo comprimido viene de la ROM en big-endian. */
static void yay0(const u8* src, u8* dst) {
    u32 size = be32(src + 4);
    const u8* link = src + be32(src + 8);
    const u8* chunk = src + be32(src + 12);
    const u8* maskp = src + 16;
    u8* end = dst + size;
    u32 mask = 0;
    int bits = 0;
    while (dst < end) {
        if (bits == 0) { mask = be32(maskp); maskp += 4; bits = 32; }
        if (mask & 0x80000000u) {
            *dst++ = *chunk++;
        } else {
            u32 l = ((u32)link[0] << 8) | link[1];
            link += 2;
            u32 dist = (l & 0xFFF) + 1;
            u32 n = l >> 12;
            n = n ? n + 2 : (u32)(*chunk++) + 0x12;
            u8* from = dst - dist;
            while (n-- && dst < end) *dst++ = *from++;
        }
        mask <<= 1;
        bits--;
    }
}
void Yay0_Decompress(void* src, void* dst) { yay0(src, dst); }
void func_80050BA0(s32 src, void* dst) { yay0((const u8*)(uintptr_t)src, dst); }

/* Constantes del .rodata de func_80042158 (asm/us/nonmatchings/3D140_2) */
const f32 D_8007CB80 = 1.100000024f;
const f32 D_8007CB84 = 1.100000024f;
const f32 D_8007CB88 = 1.200000048f;
const f32 D_8007CB8C = 1.100000024f;
const f32 D_8007CB90 = 0.009999999776f;

/* ---- Pendientes de traducir a C (siguen en ASM en el decomp) ---- */
#define STUB_ONCE(name) do { static int once; if (!once) { once = 1; ps_printf("[stub] %s\n", name); } } while (0)

/* Sonido del Game Boy (GB Tower, fuera del port). El decodificador de las voces (func_80045FF0 y 49190.s) ya no esta
   aqui: se traduce del original al compilar (tools/port/m2c.py). */
s16 func_80049DF0(void) { STUB_ONCE("func_80049DF0 (audio)"); return 0; }
s16 func_8004A474(void) { STUB_ONCE("func_8004A474 (audio)"); return 0; }
s16 func_8004A89C(void) { STUB_ONCE("func_8004A89C (audio)"); return 0; }

/* GB Tower (fragment 1): fuera del port, "No disponible" */
void func_81206D9C(void* a) { (void)a; }
void func_81206E64(void* a) { (void)a; }
void func_81206F38(void* a) { (void)a; }

int gPsAudioEnabled = 0;

/* ---- libc/libm de libultra ---- */
const float __libm_qnan_f = __builtin_nanf("");
void _bcopy(void* src, void* dst, s32 n) { __builtin_memmove(dst, src, (unsigned int)n); }

/* ---- Arranque ---- */
extern void Main(void);
void ps_game_start(const PsHost* host) {
    gPsHost = host;
    ps_printf("[ps] Pokemon Stadium (port nativo) iniciando\n");
    { extern void ps_fixup_data_pointers(void); ps_fixup_data_pointers(); }
    { extern void ps_frag_data_snapshot(void); ps_frag_data_snapshot(); }
    Main();                /* crea el hilo idle */
    ps_run_until_exit();   /* corre los hilos del juego hasta que el host pida salir */
}

/* ---- PS_BOUNDS=1 (tools/port/build_blob_3ds.sh): avisos del compilador de indices fuera de un arreglo ---- */
typedef struct { const char* file; u32 line; u32 col; } PsUbLoc;
typedef struct { u16 kind; u16 info; char name[1]; } PsUbType;
typedef struct { PsUbLoc loc; const PsUbType* arr; const PsUbType* idx; } PsUbOob;
void __ubsan_handle_out_of_bounds(PsUbOob* d, unsigned long index) {
    static const PsUbOob* seen[768];
    static u32 n;
    u32 i;
    for (i = 0; i < n; i++) if (seen[i] == d) return; /* cada lugar del codigo, una vez */
    if (n < 768) seen[n++] = d;
    ps_printf("[limites] %s:%u indice %ld en %s\n", d->loc.file, (unsigned)d->loc.line, (long)index, d->arr ? d->arr->name : "?");
}
void __ubsan_handle_out_of_bounds_abort(PsUbOob* d, unsigned long index) { __ubsan_handle_out_of_bounds(d, index); }

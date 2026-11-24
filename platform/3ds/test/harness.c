/* Banco de pruebas del bloque del 3DS bajo qemu-armeb: mismo codigo del juego (ARM BE8) que va en el
 * .3dsx, con un "host" minimo en big-endian que usa syscalls de Linux. Sirve para encontrar fallas del
 * bloque sin la consola. Lo que es propio del 3DS (citro3d, NDSP, el manejador de excepciones) no se prueba
 * aqui, pero la logica de RAM baja del manejador se reproduce igual.
 *
 * Uso: qemu-armeb ps_blob_test baserom.z64 segundos [dir_volcados frames,.. [audio.pcm]] */
#include "global.h"
#include "os/ps_os.h"
#include "ps_assets.h"

long hsys(long n, long a, long b, long c, long d, long e, long f);
void hsig_restorer(void);
void ps_mips_trap_shim(void);
void Yay0_Decompress(void* src, void* dst);
extern u32 __ps_text_end[];
void* memcpy(void* d, const void* s, unsigned n);
void* memset(void* d, int c, unsigned n);
int memcmp(const void* a, const void* b, unsigned n);
unsigned strlen(const char* s);

static void hwrite(int fd, const void* p, u32 n) { hsys(4, fd, (long)p, n, 0, 0, 0); }
static void hputs(const char* s) { hwrite(1, s, strlen(s)); }
static void hhex(u32 v) { char b[9]; for (int i = 0; i < 8; i++) b[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15]; b[8] = 0; hputs(b); }
static void hdec(u32 v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = '0' + v % 10; v /= 10; } while (v); hputs(b + i); }
static u32 atou(const char* s) { u32 v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }
static void* hmap(u32 size) { return (void*)hsys(192, 0, size, 3, 0x22, -1, 0); } /* mmap2 anonimo RW */

static u8* sRom;
static u32 sRomSize;
static u64 sStart;
static u64 sLimitUs;
static u32 sFrames;
static const char* sDumpDir;
static const char* sDumpList;
static int sPcm = -1;
static u32 sLowRam;

static u64 now_us(void) { struct { s32 s, ns; } t; hsys(263, 1, (long)&t, 0, 0, 0, 0); return (u64)t.s * 1000000u + t.ns / 1000; }
static void h_print(const char* s) { hputs(s); }
static u64 ticks(void) { u64 us = now_us(); return us * 268u + us * 111856u / 1000000u; } /* reloj del ARM11 */
static u32 h_ticks_lo(void) { return (u32)ticks(); }
static u32 h_ticks_hi(void) { return (u32)(ticks() >> 32); }
static void h_sleep_us(u32 us) { struct { s32 s, ns; } t = { us / 1000000, (us % 1000000) * 1000 }; hsys(162, (long)&t, 0, 0, 0, 0, 0); }
static u32 h_pump(void) {
    if (now_us() - sStart > sLimitUs) { extern void ps_os_dump_threads(void); ps_os_dump_threads(); hputs("[harness] fin: "); hdec(sFrames); hputs(" frames, lowram "); hdec(sLowRam); hputs("\n"); hsys(248, 0, 0, 0, 0, 0, 0); }
    return 1;
}
/* Guion de botones (argv[6...]: boton@frame, se mantiene 6 frames). Botones del N64:
 * S A B Z U D l r, C: 1 izq 2 arriba 3 der 4 abajo. Sin guion: START cada ~4 s. */
static struct { u16 b; u32 f; } sScript[128];
static int sScriptN;
static u16 script_button(char c) {
    switch (c) {
        case 'S': return START_BUTTON; case 'A': return A_BUTTON; case 'B': return B_BUTTON; case 'Z': return Z_TRIG;
        case 'U': return U_JPAD; case 'D': return D_JPAD; case 'l': return L_JPAD; case 'r': return R_JPAD;
        case 'L': return L_TRIG; case 'R': return R_TRIG;
        case '1': return L_CBUTTONS; case '2': return U_CBUTTONS; case '3': return R_CBUTTONS; case '4': return D_CBUTTONS;
    }
    return 0;
}
static void h_read_pad(OSContPad* pads) {
    memset(pads, 0, sizeof(OSContPad) * 4);
    if (sScriptN) {
        for (int i = 0; i < sScriptN; i++)
            if (sFrames >= sScript[i].f && sFrames < sScript[i].f + 6) pads[0].button |= sScript[i].b;
        return;
    }
    /* START cada ~4 s para avanzar del titulo */
    if (sFrames > 600 && (sFrames % 240) < 3) pads[0].button = START_BUTTON;
}
static int in_list(u32 f) {
    if (!sDumpList) return 0;
    const char* p = sDumpList;
    while (*p) { if (atou(p) == f) return 1; while (*p && *p != ',') p++; if (*p) p++; }
    return 0;
}
static void h_gfx_task(OSTask* t) {
    sFrames++;
    if (!in_list(sFrames)) return;
    char path[256]; u32 n = strlen(sDumpDir);
    memcpy(path, sDumpDir, n);
    const char* nm = "/blob_00000.psfd";
    memcpy(path + n, nm, strlen(nm) + 1);
    for (u32 v = sFrames, i = 0; i < 5; i++, v /= 10) path[n + 10 - i] = '0' + v % 10;
    int fd = hsys(5, (long)path, 0101 | 01000, 0644, 0, 0, 0);
    if (fd < 0) return;
    u32 hdr[4] = { 0x50534644u, 0x0A000000u, 0x00C30000u, (u32)t->t.data_ptr };
    hwrite(fd, hdr, 16);
    hwrite(fd, (void*)0x0A000000, 0x00C30000u);
    hsys(6, fd, 0, 0, 0, 0, 0);
    hputs("[dump] "); hputs(path); hputs("\n");
}
static void h_exit(void) { hputs("[harness] salir\n"); hsys(248, 0, 0, 0, 0, 0, 0); }
static u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u8* sResBuf;
static const void* h_res_get(u32 rom, u32 size, u32 type, u32* outSize) {
    static u32 lastRom = ~0u, lastOut;
    if (rom == lastRom) { *outSize = lastOut; return sResBuf; }
    const u8* raw = sRom + rom;
    if (type == 1 && size >= 0x18 && memcmp(raw, "PERS-SZP", 8) == 0) {
        u32 hsize = be32(raw + 8), size2 = be32(raw + 16), patches = be32(raw + 20);
        memset(sResBuf, 0, size2 + 0x10000);
        Yay0_Decompress((void*)(raw + hsize), sResBuf);
        for (u32 i = 0; i < patches; i++) memcpy(sResBuf + be32(raw + 28 + i * 8), raw + 24 + i * 8, 4);
        *outSize = size2;
    } else {
        memcpy(sResBuf, raw, size);
        *outSize = size;
    }
    lastRom = rom; lastOut = *outSize;
    return sResBuf;
}
static u32 h_rom_read(u32 rom, void* dst, u32 size) {
    if (rom >= sRomSize) return 0;
    if (rom + size > sRomSize) size = sRomSize - rom;
    memcpy(dst, sRom + rom, size);
    return size;
}
static void h_audio_push(const void* buf, u32 size, u32 freq) { (void)freq; if (sPcm >= 0) hwrite(sPcm, buf, size); }

static void h_save_load(void* b, u32 n) { (void)b; (void)n; }
static void h_save_store(const void* b, u32 n) { (void)b; (void)n; hputs("[harness] partida guardada\n"); }
/* Audio "asincrono": aqui se corre al momento, pero el fin se avisa por sondeo como en el 3DS */
static u32 sAudSt;
static u32 h_audio_task(OSTask* t, u32 fn) { ((int (*)(OSTask*))fn)(t); sAudSt = 2; return 1; }
static u32 h_audio_task_state(void) { u32 s = sAudSt; if (s == 2) sAudSt = 0; return s; }
static const void* sApi[15] = { h_print, h_ticks_lo, h_ticks_hi, h_sleep_us, h_pump, h_read_pad, h_gfx_task,
                                h_exit, h_res_get, h_rom_read, h_audio_push, h_save_load, h_save_store, h_audio_task, h_audio_task_state };

/* ---- SIGSEGV: misma logica que ps_exc_handler del host del 3DS ---- */
#define UC_PC(uc) (((u32*)(uc))[8 + 15])
#define UC_R(uc, i) (((u32*)(uc))[8 + (i)])
#define UC_FAR(uc) (((u32*)(uc))[8 + 17])
static void on_segv(int sig, void* info, void* uc) {
    (void)sig;
    u32 pc = UC_PC(uc);
    u32 far = ((u32*)info)[3];
    if (pc >= (u32)__ps_text_end && pc < 0x0AC30000u) { /* llamada a codigo MIPS de un asset */
        static u32 sTraps;
        if (sTraps++ < 40) { hputs("[trap] mips "); hhex(pc); hputs(" lr "); hhex(UC_R(uc, 14)); hputs("\n"); }
        UC_R(uc, 12) = pc;
        UC_PC(uc) = (u32)ps_mips_trap_shim;
        return;
    }
    if (pc >= 0x0A000000u && pc < (u32)__ps_text_end && (far < 0x10000u || far >= 0x80000000u)) {
        u32 ins = __builtin_bswap32(*(const u32*)pc); /* BE8: instrucciones en little-endian */
        int load = (ins >> 20) & 1;
        u32 rd = (ins >> 12) & 0xF, cls = (ins >> 25) & 7;
        int handled = 0;
        if (cls == 2 || cls == 3) handled = 1;
        else if (cls == 0 && (ins & 0x90) == 0x90 && (ins & 0x60)) {
            handled = 1;
            if (!load && ((ins >> 5) & 3) == 2) { load = 1; if (rd < 12) UC_R(uc, rd + 1) = 0; }
        } else if (cls == 6) { handled = 1; load = 0; }
        if (handled) {
            if (sLowRam++ < 8) { hputs("[lowram] "); hputs(load ? "lectura " : "escritura "); hhex(far); hputs(" pc "); hhex(pc); hputs("\n"); }
            if (load && rd < 13) UC_R(uc, rd) = 0;
            UC_PC(uc) = pc + 4;
            return;
        }
    }
    hputs("\n*** SIGSEGV pc "); hhex(pc); hputs(" lr "); hhex(UC_R(uc, 14)); hputs(" far "); hhex(far);
    hputs(" r0 "); hhex(UC_R(uc, 0)); hputs(" r1 "); hhex(UC_R(uc, 1)); hputs(" r2 "); hhex(UC_R(uc, 2)); hputs(" r3 "); hhex(UC_R(uc, 3));
    hputs("\npila:");
    u32* sp = (u32*)UC_R(uc, 13);
    for (int i = 0, shown = 0; i < 512 && shown < 16; i++)
        if (sp[i] >= 0x0A000000u && sp[i] < (u32)__ps_text_end) { hputs(" "); hhex(sp[i]); shown++; }
    hputs("\n");
    hsys(248, 1, 0, 0, 0, 0, 0);
}

/* Vigilante: si los frames no avanzan en 5 s, decir donde esta el CPU */
static void on_alarm(int sig, void* info, void* uc) {
    static u32 last = ~0u, strikes;
    (void)sig; (void)info;
    if (sFrames != last) { last = sFrames; strikes = 0; return; }
    hputs("[vigilante] atorado en frame "); hdec(sFrames); hputs(" pc "); hhex(UC_PC(uc)); hputs(" lr "); hhex(UC_R(uc, 14)); hputs(" pila:");
    u32* sp = (u32*)UC_R(uc, 13);
    for (int i = 0, shown = 0; i < 256 && shown < 12; i++)
        if (sp[i] >= 0x0A000000u && sp[i] < (u32)__ps_text_end) { hputs(" "); hhex(sp[i]); shown++; }
    hputs("\n");
    if (++strikes >= 3) hsys(248, 2, 0, 0, 0, 0, 0);
}

void ps_blob_entry(const void* api);
static u8 sAltStack[0x10000] __attribute__((aligned(16)));

void harness_main(u32* sp) {
    int argc = sp[0];
    char** argv = (char**)(sp + 1);
    if (argc < 3) { hputs("uso: ps_blob_test baserom.z64 segundos [dir frames [audio.pcm]]\n"); return; }
    int fd = hsys(5, (long)argv[1], 0, 0, 0, 0, 0);
    if (fd < 0) { hputs("no abre la ROM\n"); return; }
    sRomSize = 32 << 20;
    sRom = hmap(sRomSize);
    for (u32 got = 0; got < sRomSize;) { long n = hsys(3, fd, (long)(sRom + got), sRomSize - got, 0, 0, 0); if (n <= 0) break; got += n; }
    sResBuf = hmap(8 << 20);
    sLimitUs = (u64)atou(argv[2]) * 1000000u;
    if (argc > 4) { sDumpDir = argv[3]; sDumpList = argv[4]; }
    if (argc > 5 && argv[5][0] != '-') sPcm = hsys(5, (long)argv[5], 0101 | 01000, 0644, 0, 0, 0);
    for (int i = 6; i < argc && sScriptN < 128; i++) {
        const char* a = argv[i];
        const char* at = a; while (*at && *at != '@') at++;
        if (!*at) continue;
        sScript[sScriptN].b = script_button(a[0]);
        sScript[sScriptN].f = atou(at + 1);
        sScriptN++;
    }
    struct { void* sp; s32 flags; u32 size; } ss = { sAltStack, 0, sizeof(sAltStack) };
    hsys(186, (long)&ss, 0, 0, 0, 0, 0); /* sigaltstack: las fibras tienen pilas chicas */
    struct { void* h; u32 flags; void* restorer; u32 mask[2]; } sa = { (void*)on_segv, 0x04000000u | 4 | 0x08000000u, (void*)hsig_restorer, { 0, 0 } };
    hsys(174, 11, (long)&sa, 0, 8, 0, 0);
    hsys(174, 7, (long)&sa, 0, 8, 0, 0); /* SIGBUS tambien */
    struct { void* h; u32 flags; void* restorer; u32 mask[2]; } sa2 = { (void*)on_alarm, 0x04000000u | 4 | 0x08000000u | 0x10000000u, (void*)hsig_restorer, { 0, 0 } };
    hsys(174, 14, (long)&sa2, 0, 8, 0, 0);
    struct { s32 is, ius, vs, vus; } it = { 5, 0, 5, 0 };
    hsys(104, 0, (long)&it, 0, 0, 0, 0); /* setitimer ITIMER_REAL */
    sStart = now_us();
    hputs("[harness] arrancando el bloque\n");
    ps_blob_entry(sApi);
}

void ps_trace_asset(void* dst, unsigned int size, unsigned int idx, unsigned int base) {
    (void)dst;
    hputs("[asset] f"); hdec(sFrames); hputs(" "); hputs(gPsAssetIndex[idx].path); hputs(" +"); hhex(size); hputs(" @"); hhex(base); hputs("\n");
}

void ps_fixup_log(u32 w) { hputs("[fix] "); hhex(w); hputs("\n"); }

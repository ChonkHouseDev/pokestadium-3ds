/* Host del trazador: el juego corre en MIPS big-endian (como el N64) bajo QEMU, leyendo los assets
 * directamente de la ROM y sin convertir. Cada vez que un asset llega a memoria se avisa al plugin
 * de QEMU con una syscall marcador (ver tools/tracer/ps_trace_plugin.c).
 *
 * Uso: ps_trace <baserom.z64> [segundos] [boton@frame ...]   botones: A B S Z L R U D (cruceta) l r (cruceta izq/der) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "os/ps_os.h"
#include "ps_assets.h"

void ps_game_start(const PsHost* host);
void Yay0_Decompress(void* src, void* dst);

#define MARK_SYSCALL 4999
static int sLogAssets = -1;
static int log_assets(void) { if (sLogAssets < 0) sLogAssets = getenv("PS_LOG_ASSETS") != NULL; return sLogAssets; }
void ps_trace_asset(void* dst, unsigned int size, unsigned int idx, unsigned int base) {
    if (log_assets()) fprintf(stderr, "[asset] %p +%x %s @%x\n", dst, size, gPsAssetIndex[idx].path, base);
    syscall(MARK_SYSCALL, 1, (long)dst, (long)size, (long)idx, (long)base);
}
void ps_trace_clear(void* addr, u32 size) {
    if (log_assets() > 0 && getenv("PS_LOG_ALLOC")) fprintf(stderr, "[alloc] %p +%x\n", addr, size);
    syscall(MARK_SYSCALL, 2, (long)addr, (long)size);
}

/* ---------------- assets desde la ROM (sin convertir) ---------------- */
static u8* sRom;
static long sRomSize;
static u32 be32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

static const void* res_get(const char* path, unsigned int* size) {
    static struct { const char* path; u8* data; u32 size; } cache[1024];
    static int count;
    for (int i = 0; i < count; i++) {
        if (cache[i].path == path || strcmp(cache[i].path, path) == 0) { *size = cache[i].size; return cache[i].data; }
    }
    const PsAssetEntry* e = NULL;
    for (unsigned i = 0; i < gPsAssetIndexCount; i++) {
        if (strcmp(gPsAssetIndex[i].path, path) == 0) { e = &gPsAssetIndex[i]; break; }
    }
    if (!e) return NULL;
    const u8* src = sRom + e->rom;
    u8* data;
    u32 n;
    if (e->type == PS_ASSET_PERS_SZP) {
        /* igual que func_80003890: Yay0 + parches (valor, offset) + ceros hasta decompressed_size2 */
        u32 hsize = be32(src + 8), size2 = be32(src + 16), patches = be32(src + 20);
        u32 size1 = be32(src + hsize + 4);
        n = size2;
        data = calloc(1, (size1 > size2 ? size1 : size2) + 16);
        Yay0_Decompress((void*)(src + hsize), data);
        for (u32 i = 0; i < patches; i++) {
            u32 val = be32(src + 24 + i * 8), at = be32(src + 28 + i * 8);
            memcpy(data + at, &val, 4); /* el host es big-endian: el valor queda igual que en el N64 */
        }
        if (size2 > size1) memset(data + size1, 0, size2 - size1);
    } else {
        n = e->size;
        data = malloc(n);
        memcpy(data, src, n);
    }
    if (count < 1024) { cache[count].path = path; cache[count].data = data; cache[count].size = n; count++; }
    *size = n;
    return data;
}


/* Llamadas del juego a codigo MIPS dentro de assets (pool sin permiso de ejecucion): interpretar */
#include <signal.h>
#include <stdint.h>
#include <ucontext.h>
int ps_is_pool_code(u32 pc);
s32 ps_call_fragment_func(void* fn, s32 a0, s32 a1);
static void on_segv(int sig, siginfo_t* si, void* ctx) {
    ucontext_t* uc = ctx;
    u32 pc = (u32)uc->uc_mcontext.pc;
    if (ps_is_pool_code(pc)) {
        uc->uc_mcontext.gregs[2] = (s32)ps_call_fragment_func((void*)pc, (s32)uc->uc_mcontext.gregs[4], (s32)uc->uc_mcontext.gregs[5]);
        uc->uc_mcontext.pc = uc->uc_mcontext.gregs[31];
        return;
    }
    /* Lectura/escritura en la RAM baja del N64 (NULL, 0x800000XX): en el N64 no truena. Leer 0 e ignorar escrituras. */
    u32 fa = (u32)(uintptr_t)si->si_addr;
    if (fa < 0x10000u || (fa >= 0x80000000u && fa < 0x80800000u)) {
        u32 ins = *(u32*)(uintptr_t)pc;
        u32 op = ins >> 26, rt = (ins >> 16) & 31;
        int isLoad = op == 0x20 || op == 0x21 || op == 0x23 || op == 0x24 || op == 0x25 || op == 0x27 || op == 0x37;
        int isFpLoad = op == 0x31 || op == 0x35;
        int isStore = op == 0x28 || op == 0x29 || op == 0x2B || op == 0x3F || op == 0x39 || op == 0x3D;
        if (isLoad || isFpLoad || isStore) {
            static int warned;
            if (warned++ < 8) fprintf(stderr, "[lowram] %s en %08x (pc %08x)\n", isStore ? "escritura" : "lectura", fa, pc);
            if (isLoad && rt) uc->uc_mcontext.gregs[rt] = 0;
            if (isFpLoad) uc->uc_mcontext.fpregs.fp_r.fp_dregs[rt / 2] = 0;
            uc->uc_mcontext.pc += 4;
            return;
        }
    }
    fprintf(stderr, "[crash] pc=%08x ra=%08x addr=%p\n", pc, (u32)uc->uc_mcontext.gregs[31], si->si_addr);
    fprintf(stderr, "[regs]");
    for (int i = 2; i < 32; i++) fprintf(stderr, " r%d=%08x", i, (u32)uc->uc_mcontext.gregs[i]);
    fprintf(stderr, "\n");
    _exit(139);
}
static void install_trap(void) {
    struct sigaction sa = { 0 };
    sa.sa_sigaction = on_segv;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGSEGV, &sa, NULL);
}

/* Datos del juego (incluye .rodata): se hacen escribibles para traducir direcciones del N64 */
#include <sys/mman.h>
extern char etext[], _edata[];
void ps_platform_data_range(u32** start, u32** end) {
    uintptr_t a = (uintptr_t)etext & ~0xFFFu, b = ((uintptr_t)_edata + 0xFFF) & ~0xFFFu;
    mprotect((void*)a, b - a, PROT_READ | PROT_WRITE | PROT_EXEC);
    *start = (u32*)(((uintptr_t)etext + 3) & ~3u);
    *end = (u32*)_edata;
}

/* ---------------- servicios ---------------- */
static u64 sStartUs, sLimitUs;
static u32 sFrames;
static struct { u16 button; u32 frame; } sScript[256];
static int sScriptCount;

static u64 now_us(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (u64)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000; }
static void h_print(const char* s) { fputs(s, stdout); fflush(stdout); }
static u64 h_time_us(void) { return now_us() - sStartUs; }
static void h_sleep_us(u32 us) { usleep(us); }
/* Lectura cruda (DMA): directo de la ROM, como en el 3DS (el host MIPS es big-endian, igual que la ROM) */
static u32 h_rom_read(u32 rom, void* dst, u32 size) {
    if (sRom && rom < sRomSize) {
        if (rom + size > sRomSize) size = sRomSize - rom;
        memcpy(dst, sRom + rom, size);
        return size;
    }
    return ps_rom_read(rom, dst, size);
}
static int h_pump(void) { return h_time_us() < sLimitUs; }
static void h_read_pad(OSContPad* pads) {
    memset(pads, 0, sizeof(OSContPad) * MAXCONTROLLERS);
    for (int i = 0; i < sScriptCount; i++) {
        if (sFrames >= sScript[i].frame && sFrames < sScript[i].frame + 4) pads[0].button |= sScript[i].button;
    }
    for (int i = 1; i < MAXCONTROLLERS; i++) pads[i].errno = CONT_NO_RESPONSE_ERROR;
}
/* Volcado de frames para el visor (tools/viewer): PS_DUMP=frame1,frame2,... PS_DUMP_DIR=carpeta
 * Archivo: "PSFD", u32 base, u32 tamano, u32 display list, memoria del juego [base, base+tamano) */
extern char _end[];
static void dump_frame(OSTask* t) {
    static char list[512];
    static int init;
    if (!init) { init = 1; const char* e = getenv("PS_DUMP"); if (e) snprintf(list, sizeof(list), ",%s,", e); }
    if (!list[0]) return;
    char key[16];
    snprintf(key, sizeof(key), ",%u,", sFrames);
    if (!strstr(list, key)) return;
    const char* dir = getenv("PS_DUMP_DIR");
    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%05u.psfd", dir ? dir : ".", sFrames);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    u32 base = (u32)(uintptr_t)etext & ~0xFu, size = (u32)((uintptr_t)_end - base);
    u32 hdr[4] = { 0x50534644u, base, size, (u32)(uintptr_t)t->t.data_ptr };
    fwrite(hdr, 4, 4, f); /* el host es big-endian: el visor los lee como tal */
    fwrite((void*)(uintptr_t)base, 1, size, f);
    fclose(f);
    fprintf(stderr, "[dump] frame %u -> %s\n", sFrames, path);
}
static void h_gfx_task(OSTask* t) { sFrames++; dump_frame(t); }
static void h_exit(void) { exit(0); }

/* Audio: PS_PCM=archivo guarda las muestras tal cual (s16 estereo big-endian) */
static FILE* sPcm;
static u32 sPcmFreq;
void ps_audio_push(const void* buf, u32 size, u32 freq) {
    static int init;
    if (!init) { init = 1; const char* e = getenv("PS_PCM"); if (e) sPcm = fopen(e, "wb"); }
    if (freq != sPcmFreq) { sPcmFreq = freq; fprintf(stderr, "[ai] frecuencia %u\n", freq); }
    if (sPcm) { fwrite(buf, 1, size, sPcm); fflush(sPcm); }
}
static const PsHost sHost = { h_print, h_time_us, h_sleep_us, h_rom_read, h_pump, h_read_pad, h_gfx_task, h_exit };

static u16 parse_button(char c) {
    switch (c) {
        case 'A': return A_BUTTON; case 'B': return B_BUTTON; case 'S': return START_BUTTON;
        case 'Z': return Z_TRIG; case 'L': return L_TRIG; case 'R': return R_TRIG;
        case 'U': return U_JPAD; case 'D': return D_JPAD; case 'l': return L_JPAD; case 'r': return R_JPAD;
        case '1': return L_CBUTTONS; case '2': return U_CBUTTONS; case '3': return R_CBUTTONS; case '4': return D_CBUTTONS;
    }
    return 0;
}

/* Fragment al que entra el juego: el registro sirve para saber que pantallas se recorrieron */
int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "uso: %s baserom.z64 [segundos] [boton@frame ...]\n", argv[0]); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); sRomSize = ftell(f); fseek(f, 0, SEEK_SET);
    sRom = malloc(sRomSize);
    fread(sRom, 1, sRomSize, f);
    fclose(f);
    sLimitUs = (argc > 2 ? atoi(argv[2]) : 10) * 1000000ull;
    for (int i = 3; i < argc && sScriptCount < 256; i++) {
        char* at = strchr(argv[i], '@');
        if (!at) continue;
        sScript[sScriptCount].button = parse_button(argv[i][0]);
        sScript[sScriptCount].frame = (u32)atoi(at + 1);
        sScriptCount++;
    }
    gPsResGet = res_get;
    { extern int gPsAudioEnabled; gPsAudioEnabled = 1; } /* en big-endian las tablas de audio ya son validas */
    install_trap();
    sStartUs = now_us();
    ps_game_start(&sHost);
    printf("[trace] fin: %u frames\n", sFrames);
    return 0;
}

void ps_fixup_log(u32 w) { if (getenv("PS_LOG_FIX")) fprintf(stderr, "[fix] %08x\n", w); }

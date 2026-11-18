/* Host de prueba sin ventana (Linux): corre la logica del juego leyendo pokestadium.o2r.
 * Sirve para depurar el arranque sin libultraship ni GPU.
 *
 * Uso: ps_headless <pokestadium.o2r> [segundos] [boton@frame ...]
 *      botones: A B S(start) Z L R U D (cruceta) */
#define _GNU_SOURCE
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ucontext.h>
#include <zip.h>
#include "os/ps_os.h"
#include "ps_assets.h"

void ps_game_start(const PsHost* host);

/* ---------------- lector de .o2r (recursos Blob de Torch) ---------------- */
static zip_t* sZip;
typedef struct { char* path; u8* data; u32 size; } Res;
static Res sCache[1024];
static int sCacheCount;

static const void* res_get(const char* path, unsigned int* size) {
    for (int i = 0; i < sCacheCount; i++) {
        if (strcmp(sCache[i].path, path) == 0) { *size = sCache[i].size; return sCache[i].data; }
    }
    zip_stat_t st;
    if (zip_stat(sZip, path, 0, &st) != 0) return NULL;
    u8* raw = malloc(st.size);
    zip_file_t* f = zip_fopen(sZip, path, 0);
    zip_fread(f, raw, st.size);
    zip_fclose(f);
    /* Blob de libultraship: encabezado de 0x40 bytes, u32 tamano (LE) y los datos */
    u32 n = raw[0x40] | (raw[0x41] << 8) | (raw[0x42] << 16) | ((u32)raw[0x43] << 24);
    u8* data = malloc(n);
    memcpy(data, raw + 0x44, n);
    free(raw);
    if (sCacheCount < 1024) {
        sCache[sCacheCount].path = strdup(path);
        sCache[sCacheCount].data = data;
        sCache[sCacheCount].size = n;
        sCacheCount++;
    }
    *size = n;
    return data;
}

/* ---------------- servicios del host ---------------- */
static u64 sStartUs, sLimitUs;
static u32 sFrames;
static struct { u16 button; u32 frame; } sScript[64];
static int sScriptCount;

static u64 now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
}
static void h_print(const char* s) { fputs(s, stdout); fflush(stdout); }
static u64 h_time_us(void) { return now_us() - sStartUs; }
static void h_sleep_us(u32 us) { usleep(us); }
static u32 h_rom_read(u32 rom, void* dst, u32 size) { return ps_rom_read(rom, dst, size); }
static int h_pump(void) { return h_time_us() < sLimitUs; }
static void h_read_pad(OSContPad* pads) {
    memset(pads, 0, sizeof(OSContPad) * MAXCONTROLLERS);
    for (int i = 0; i < sScriptCount; i++) {
        if (sFrames >= sScript[i].frame && sFrames < sScript[i].frame + 6) pads[0].button |= sScript[i].button;
    }
    for (int i = 1; i < MAXCONTROLLERS; i++) pads[i].errno = CONT_NO_RESPONSE_ERROR;
}
static void h_gfx_task(OSTask* t) {
    (void)t;
    sFrames++;
    if (sFrames % 60 == 0) printf("[headless] %u frames dibujados\n", sFrames);
}
static void h_exit(void) { exit(0); }

static const PsHost sHost = { h_print, h_time_us, h_sleep_us, h_rom_read, h_pump, h_read_pad, h_gfx_task, h_exit };

int ps_is_pool_code(u32 pc);
s32 ps_call_fragment_func(void* fn, s32 a0, s32 a1);

/* Llamada del juego a codigo MIPS dentro de un asset (el pool no es ejecutable): se interpreta.
 * cdecl de x86: [esp] = regreso, [esp+4] = a0, [esp+8] = a1; resultado en eax. */
static int trap_mips_call(ucontext_t* uc) {
    u32 pc = (u32)uc->uc_mcontext.gregs[REG_EIP];
    if (!ps_is_pool_code(pc)) return 0;
    u32* sp = (u32*)uc->uc_mcontext.gregs[REG_ESP];
    uc->uc_mcontext.gregs[REG_EAX] = (u32)ps_call_fragment_func((void*)pc, (s32)sp[1], (s32)sp[2]);
    uc->uc_mcontext.gregs[REG_EIP] = sp[0];
    uc->uc_mcontext.gregs[REG_ESP] += 4;
    return 1;
}

static void on_segv(int sig, siginfo_t* si, void* ctx) {
    (void)si;
    if (sig == SIGSEGV && trap_mips_call((ucontext_t*)ctx)) return;
    void* bt[48];
    int n = backtrace(bt, 48);
    fprintf(stderr, "\n[headless] senal %d. Pila:\n", sig);
    backtrace_symbols_fd(bt, n, 2);
    _exit(128 + sig);
}

static void on_crash(int sig) {
    void* bt[48];
    int n = backtrace(bt, 48);
    fprintf(stderr, "\n[headless] senal %d. Pila:\n", sig);
    backtrace_symbols_fd(bt, n, 2);
    _exit(128 + sig);
}

static u16 parse_button(char c) {
    switch (c) {
        case 'A': return A_BUTTON; case 'B': return B_BUTTON; case 'S': return START_BUTTON;
        case 'Z': return Z_TRIG; case 'L': return L_TRIG; case 'R': return R_TRIG;
        case 'U': return U_JPAD; case 'D': return D_JPAD;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "uso: %s pokestadium.o2r [segundos] [boton@frame ...]\n", argv[0]); return 1; }
    int err = 0;
    sZip = zip_open(argv[1], ZIP_RDONLY, &err);
    if (!sZip) { fprintf(stderr, "no se pudo abrir %s\n", argv[1]); return 1; }
    sLimitUs = (argc > 2 ? atoi(argv[2]) : 10) * 1000000ull;
    for (int i = 3; i < argc && sScriptCount < 64; i++) {
        char* at = strchr(argv[i], '@');
        if (!at) continue;
        sScript[sScriptCount].button = parse_button(argv[i][0]);
        sScript[sScriptCount].frame = (u32)atoi(at + 1);
        sScriptCount++;
    }
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = on_segv;
        sa.sa_flags = SA_SIGINFO | SA_NODEFER;
        sigaction(SIGSEGV, &sa, NULL);
    }
    signal(SIGBUS, on_crash);
    signal(SIGFPE, on_crash);
    signal(SIGILL, on_crash);
    signal(SIGABRT, on_crash);
    gPsResGet = res_get;
    sStartUs = now_us();
    ps_game_start(&sHost);
    printf("[headless] fin: %u frames\n", sFrames);
    return 0;
}

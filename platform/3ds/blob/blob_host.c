/* Lado del juego (big-endian) de la frontera con el host del 3DS (little-endian).
 *
 * Reglas para cruzar la frontera:
 *  - Solo argumentos y retornos de 32 bits en registros (nada de u64 ni structs por valor).
 *  - Los datos en memoria compartida que escribe el host (controles, tamanos) van en big-endian.
 *  - La tabla de servicios la arma el host con los apuntadores ya en big-endian. */
#include "global.h"
#include "os/ps_os.h"
#include "ps_assets.h"

int strcmp(const char* a, const char* b);

typedef struct {
    void (*print)(const char* s);
    u32 (*ticks_lo)(void);
    u32 (*ticks_hi)(void);
    void (*sleep_us)(u32 us);
    u32 (*pump)(void);                                         /* 0 = salir */
    void (*read_pad)(OSContPad* pads);                          /* escribe 4 OSContPad en big-endian */
    void (*gfx_task)(OSTask* task);                             /* dibuja la tarea (el host lee en big-endian) */
    void (*exit_game)(void);
    const void* (*res_get)(u32 rom, u32 size, u32 type, u32* outSize); /* asset ya preparado (big-endian) */
    u32 (*rom_read)(u32 rom, void* dst, u32 size);             /* lectura cruda de la ROM (DMA) */
    void (*audio_push)(const void* buf, u32 size, u32 freq);   /* muestras s16 estereo big-endian */
    void (*save_load)(void* buf, u32 size);                     /* flash de la partida desde la SD */
    void (*save_store)(const void* buf, u32 size);
    u32 (*audio_task)(OSTask* t, u32 fn, const void* out, u32 outSize); /* 1 = el host corre fn(t) en el otro nucleo y entrega out al terminar */
    u32 (*audio_task_state)(void);                              /* total de tareas terminadas */
    u32 (*audio_pending)(void);                                 /* bytes que faltan por tocar en la salida */
    u32 (*flag)(const char* name);                              /* sdmc:/3ds/pokestadium/<name>.txt: numero, 1 si vacio, ~0 si no existe */
    void (*mem_event)(u32 kind, void* addr, u32 size);          /* ver PS_MEM_* en ps_os.h */
    u32 (*gb_present)(u32 ch);                                  /* 1 si el control ch tiene Transfer Pak con cartucho (un .sav de la SD) */
    u32 (*gb_rw)(u32 chw, u32 addr, void* buf, u32 size);       /* lee/escribe el cartucho de Game Boy; chw = ch | (escribir << 8) */
    void (*gb_power)(u32 ch, u32 on);                           /* al apagar se guarda el .sav si cambio */
} PsBlobApi;

static const PsBlobApi* sApi;
static u64 sTickBase;

#define SYSCLOCK_ARM11 268111856ull

static u64 ticks(void) {
    u32 hi, lo, hi2;
    do { hi = sApi->ticks_hi(); lo = sApi->ticks_lo(); hi2 = sApi->ticks_hi(); } while (hi != hi2);
    return ((u64)hi << 32) | lo;
}

static void b_print(const char* s) { sApi->print(s); }
void ps_mem_event(u32 kind, void* addr, u32 size) { sApi->mem_event(kind, addr, size); }
static u64 b_time_us(void) {
    /* ticks del ARM11 (268.111856 MHz) a microsegundos sin division de 64 bits lenta:
     * us = ticks * 2^32 / 268.111856e6 / 2^32 ~= ticks * 16019 >> 32 * 1000... se usa factor fijo */
    u64 t = ticks() - sTickBase;
    u32 hi = (u32)(t >> 32), lo = (u32)t;
    /* 1e6 / 268111856 * 2^32 = 16019340.6 */
    const u32 K = 16019341u;
    return (u64)hi * K + (((u64)lo * K) >> 32);
}
static void b_sleep_us(u32 us) { sApi->sleep_us(us); }
/* DMA crudo: directo de la ROM (en big-endian ya es el formato que espera el juego) */
/* Ultima vez que el juego (no el hilo de audio, que lee sus muestras) leyo de la ROM o pidio un asset: si no
 * presenta cuadros pero esta cargando, el sonido se detiene con el (3D140.c, ps_audio_due) */
extern OSThread* gPsCurThread;
static u64 sLastLoadUs;
static void load_mark(void) { if (!gPsCurThread || gPsCurThread->id != 4) sLastLoadUs = b_time_us(); }
int ps_loading_recent(void) { return sLastLoadUs && b_time_us() - sLastLoadUs < 150000; }
static u32 b_rom_read(u32 rom, void* dst, u32 size) {
    load_mark();
    const u32 r = sApi->rom_read(rom, dst, size);
    load_mark();
    return r;
}
/* Partida guardada: se escribe a la SD un rato despues del ultimo cambio (el juego programa muchas paginas seguidas) */
extern int sPsFlashDirty;
const u8* ps_flash_data(void);
void ps_save_load(void* buf, u32 size) { sApi->save_load(buf, size); }

/* Audio del RSP en el nucleo 1: el host llama ps_rsp_hle_task_impl(t) desde su hilo (en modo BE) */
int ps_rsp_hle_task_impl(OSTask* t);
/* El juego espera el aviso de fin del audio para mandar el siguiente cuadro: con el audio en el otro nucleo eso
 * dejaba el nucleo del juego sin hacer nada ~20 ms por cuadro. El aviso sale al arrancar la tarea y solo se
 * espera de verdad donde hace falta: cuando la fila esta llena.
 * Fila de tareas: hasta 3 formadas (el juego tiene 3 listas de comandos y 3 buffers de salida: armar una cuarta
 * reescribiria la que corre). Con X al iniciar (audioformar = 0): una a la vez. */
#define PS_AUD_Q 6 /* igual que PS_AUDQ en src/3D140.c y la fila del host */
static u32 sAudQOut[PS_AUD_Q]; /* bytes de sonido que deja cada tarea formada */
static OSTask sAudQ[PS_AUD_Q];  /* copia propia de cada tarea formada: la del planificador se reescribe con la siguiente */
static u32 sAudSub, sAudSeen;   /* tareas formadas y terminadas (lo que ya vio este nucleo): pendientes = la resta */
unsigned long long gPsAudioWaitUs, gPsAudioWaitSchedUs; /* tiempo que el nucleo del juego espero al audio del nucleo 1 */
extern OSThread* gPsCurThread;
static OSMesgQueue sAudMq;
static OSMesg sAudMsg[4];
static int sAudMqInit, sAudWaiters, sAudSleepMode = -1, sQueueMode = -1;
s32 ps_flag(const char* name, s32 dflt);
static int audio_count(void) { return (int)(sAudSub - sAudSeen); }
/* El host (nucleo 1) corre las tareas formadas una tras otra y entrega el sonido de cada una al terminarla. Aqui
 * solo se lleva la cuenta: cuantas termino desde la ultima vez, y despertar a quien esperaba lugar en la fila. */
static void audio_reap(void) {
    if (sAudSub == sAudSeen) return;
    const u32 done = sApi->audio_task_state();
    if (done != sAudSeen) {
        sAudSeen = done;
        while (sAudWaiters > 0) { sAudWaiters--; ps_send_from_event(&sAudMq, 0); }
    }
}
/* para el vigilante del host (se lee en big-endian): 0 nada, 1 esperando audio dormido, 2 esperando audio en cola */
volatile u32 gPsDiagWait;
/* espera a que termine una tarea mas */
static void audio_wait_done(void) {
    const u32 seen0 = sAudSeen;
    audio_reap();
    if (sAudSeen != seen0 || audio_count() == 0) return;
    {
        const unsigned long long t0 = b_time_us();
        if (sAudSleepMode < 0) sAudSleepMode = ps_flag("audiodormir", 0) != 0;
        if (!sAudMqInit) { osCreateMesgQueue(&sAudMq, sAudMsg, 4); sAudMqInit = 1; }
        /* el planificador (hilo 3) no se bloquea: mientras espera, sus colas se llenan de retrazos y se perdia el
         * aviso de fin de tarea (el juego se quedaba trabado) */
        const int sched = gPsCurThread && gPsCurThread->id == 3;
        if (sched) gPsAudioWaitSchedUs -= b_time_us();
        if (!sAudSleepMode && !sched && gPsCurThread && gPsCurThread->id != 0) {
            gPsDiagWait = 2;
            while (sAudSeen == seen0 && audio_count() > 0) {
                sAudWaiters++;
                osRecvMesg(&sAudMq, NULL, OS_MESG_BLOCK); /* lo despierta audio_reap desde ps_events_poll */
                audio_reap();
            }
        } else {
            gPsDiagWait = 1;
            while (sAudSeen == seen0 && audio_count() > 0) { sApi->sleep_us(200); audio_reap(); }
        }
        gPsDiagWait = 0;
        if (sched) gPsAudioWaitSchedUs += b_time_us();
        gPsAudioWaitUs += b_time_us() - t0;
    }
}
/* Salida de la siguiente tarea de audio (la avisa el juego al armar el cuadro, 3D140.c): el host la entrega al
 * terminar la tarea. Antes salia con osAiSetNextBuffer al armar el cuadro siguiente: un cuadro del juego despues. */
static const void* sNextOut;
static u32 sNextOutSize;
void ps_audio_next_out(const void* buf, u32 size, u32 freq) {
    sNextOut = buf; sNextOutSize = size;
    if (freq) sApi->audio_push(buf, 0, freq); /* tamano 0: solo avisa la frecuencia */
}
/* silencio en la salida (buf NULL): para rehacer la reserva despues de que se vacio */
void ps_audio_pad(u32 bytes, u32 freq) { if (bytes) sApi->audio_push(NULL, bytes, freq); }
int ps_audio_async_early = 1;
int ps_audio_async_start(OSTask* t) {
    if (sQueueMode < 0) sQueueMode = ps_flag("audioformar", 1) != 0; /* fila de tareas: el juego no espera al nucleo 1 */
    audio_reap();
    const int maxQ = sQueueMode ? PS_AUD_Q : 1;
    while (audio_count() >= maxQ) audio_wait_done();
    OSTask* sl = &sAudQ[sAudSub % PS_AUD_Q];
    *sl = *t;
    if (!sApi->audio_task(sl, (u32)ps_rsp_hle_task_impl, sNextOut, sNextOutSize)) return 0; /* sin nucleo 1: la corre el juego */
    sAudQOut[sAudSub % PS_AUD_Q] = sNextOutSize;
    sNextOut = NULL; sNextOutSize = 0;
    sAudSub++;
    return 1;
}
/* Al empezar a armar un cuadro de audio (hilo de audio del juego): con 3 tareas pendientes la siguiente lista
 * de comandos y el siguiente buffer de salida son los de la mas vieja; esperar aqui (solo se bloquea ese hilo) */
void ps_audio_frame_begin(void) {
    audio_reap();
    while (audio_count() >= PS_AUD_Q) audio_wait_done();
}
int ps_audio_async_busy(void) { return audio_count() > 0; }
/* 1 si cabe otra tarea sin esperar. El hilo de audio arma varios cuadros seguidos solo mientras quepan: si esperara
 * aqui al otro nucleo, el juego no avanzaria (y si cediera el turno a media rafaga, tampoco el sonido). */
int ps_audio_room(void) { audio_reap(); return audio_count() < PS_AUD_Q; }
/* bytes de sonido de las tareas que el nucleo 1 todavia no termina (cuentan en ps_audio_pending pero aun no suenan) */
u32 ps_audio_inflight(void) {
    u32 b = 0;
    audio_reap();
    for (u32 k = sAudSeen; k != sAudSub; k++) b += sAudQOut[k % PS_AUD_Q];
    return b;
}
/* JPEG y cualquier otra tarea HLE usan el mismo estado (DMEM) que el audio del otro nucleo: esperar a que acabe */
void ps_audio_async_wait(void) { while (audio_count() > 0) audio_wait_done(); }
/* osAiGetLength: lo que queda por tocar, incluido lo formado que el nucleo 1 todavia no entrega (lo suma el host) */
u32 ps_audio_pending(void) {
    audio_reap();
    return sApi->audio_pending();
}
/* Transfer Pak (port/os/os_misc.c): el cartucho de Game Boy lo emula el host con un .sav de la SD */
int ps_gb_present(int ch) { return (int)sApi->gb_present((u32)ch); }
int ps_gb_rw(int ch, int write, u32 addr, u8* buf, u32 size) { return (int)sApi->gb_rw((u32)ch | (write ? 0x100u : 0u), addr, buf, size); }
void ps_gb_power(int ch, int on) { sApi->gb_power((u32)ch, (u32)on); }
s32 gPsVoicesPeak, gPsVoicesStolen; /* medidor de voces de audio (n_synallocvoice.c) */
/* Banderas en la SD para prender/apagar cosas sin recompilar: valor, o dflt si el archivo no existe */
s32 ps_flag(const char* name, s32 dflt) {
    u32 v = sApi->flag(name);
    return v == 0xFFFFFFFFu ? dflt : (s32)v;
}
int ps_audio_async_poll(void) {
    if (audio_count() == 0) return 0;
    const u32 seen0 = sAudSeen;
    audio_reap();
    if (sAudSeen == seen0) return 0;
    return ps_audio_async_early ? 0 : 1; /* con aviso adelantado el SP ya se aviso al arrancar */
}
static int b_pump(void) {
    static int quiet;
    if (sPsFlashDirty) { sPsFlashDirty = 0; quiet = 30; }
    else if (quiet > 0 && --quiet == 0) sApi->save_store(ps_flash_data(), 0x20000);
    return (int)sApi->pump();
}
static void b_read_pad(OSContPad* pads) { sApi->read_pad(pads); }
static void b_gfx_task(OSTask* t) { sApi->gfx_task(t); }
static void b_exit(void) { sApi->exit_game(); }
/* osAiSetNextBuffer: con el nucleo 1 cada tarea ya lleva su salida (ps_audio_next_out) y el host la entrega; esta
 * llamada del juego (un cuadro despues) solo sirve cuando no hay nucleo 1 y la tarea corrio aqui mismo */
void ps_audio_push(const void* buf, u32 size, u32 freq) {
    if (sAudSub > 0) return;
    sApi->audio_push(buf, size, freq);
}

static const PsHost sHost = { b_print, b_time_us, b_sleep_us, b_rom_read, b_pump, b_read_pad, b_gfx_task, b_exit };

/* Assets: el host los lee de la ROM en la SD y descomprime los PERS-SZP (datos en big-endian, como el N64) */
static const void* res_get(const char* path, unsigned int* size) {
    for (u32 i = 0; i < gPsAssetIndexCount; i++) {
        const PsAssetEntry* e = &gPsAssetIndex[i];
        if (e->path == path || strcmp(e->path, path) == 0) {
            u32 n = 0;
            load_mark();
            const void* p = sApi->res_get(e->rom, e->size, e->type, &n);
            load_mark();
            *size = n;
            return p;
        }
    }
    return NULL;
}

int strcmp(const char* a, const char* b);

/* El bloque vive en 0x0A000000-0x0B400000: esas direcciones son nativas, no segmentos del N64 */
int ps_platform_is_native(u32 a) { return a >= 0x0A000000u && a < 0x0B400000u; }
extern u32 __ps_text_end[], __ps_bss_start[];
void ps_platform_data_range(u32** start, u32** end) { *start = __ps_text_end; *end = __ps_bss_start; }
void ps_game_start(const PsHost* host);
extern int gPsAudioEnabled;

void ps_arm_set_fpscr(u32 v);
u32 ps_arm_get_fpscr(void);

void ps_blob_entry(const PsBlobApi* api) {
    /* RunFast (FZ+DN): sin esto el VFP del ARM11 manda los denormales al kernel y todo va lentisimo */
    ps_arm_set_fpscr(ps_arm_get_fpscr() | 0x03000000u);
    sApi = api;
    sTickBase = 0;
    sTickBase = ticks();
    gPsResGet = res_get;
    gPsAudioEnabled = 1; /* en big-endian las tablas de audio son validas tal cual */
    ps_game_start(&sHost);
    sApi->exit_game();
}

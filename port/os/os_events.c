/* "Hardware" simulado: reloj, retrazado vertical (VI), timers y mensajes diferidos (DMA, RSP). */
#include <stdarg.h>
#include <stdio.h>
#include "ps_os.h"

#define N64_COUNT_HZ 46875000ull
#define VI_PERIOD    (N64_COUNT_HZ / 60ull) /* NTSC */

void ps_printf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    gPsHost->print(buf);
}

/* ---- reloj ---- */
u64 ps_now_count(void) { return gPsHost->time_us() * 375ull >> 3; } /* 46.875 MHz = us * 375 / 8 */
static u64 sTimeBase = 0;
u32 osGetCount(void) { ps_events_poll(); ps_preempt_check(); return (u32)ps_now_count(); }
OSTime osGetTime(void) { ps_events_poll(); return ps_now_count() - sTimeBase; }
void osSetTime(OSTime t) { sTimeBase = ps_now_count() - t; }

/* ---- tabla de eventos (osSetEventMesg) ---- */
typedef struct { OSMesgQueue* mq; OSMesg msg; } PsEvt;
static PsEvt sEvents[OS_NUM_EVENTS];
void osSetEventMesg(OSEvent e, OSMesgQueue* mq, OSMesg msg) {
    if (e < OS_NUM_EVENTS) { sEvents[e].mq = mq; sEvents[e].msg = msg; }
}
void ps_post_event(s32 e) {
    if (e < OS_NUM_EVENTS && sEvents[e].mq) ps_send_from_event(sEvents[e].mq, sEvents[e].msg);
}

/* ---- mensajes diferidos (fin de DMA, tareas del RSP, etc.) ---- */
#define PS_PENDING 128
static struct { OSMesgQueue* mq; OSMesg msg; } sPending[PS_PENDING];
static int sPendHead, sPendCount;
void ps_post_msg(OSMesgQueue* mq, OSMesg msg) {
    if (sPendCount >= PS_PENDING) { ps_printf("[os] cola de eventos llena\n"); return; }
    int i = sPendHead + sPendCount;
    if (i >= PS_PENDING) i -= PS_PENDING;
    sPending[i].mq = mq;
    sPending[i].msg = msg;
    sPendCount++;
}

/* ---- VI ---- */
static OSMesgQueue* sViMq;
static OSMesg sViMsg;
static u32 sViCount = 1, sViCounter;
static u64 sNextVi;
static u64 sLastViAt;
static int sViIdle = 1;
static void* sViCurFb;
static void* sViNextFb;
void osViSetEvent(OSMesgQueue* mq, OSMesg msg, u32 count) { sViMq = mq; sViMsg = msg; sViCount = count ? count : 1; }
void osViSwapBuffer(void* fb) { sViNextFb = fb; }
void* osViGetCurrentFramebuffer(void) { return sViCurFb; }
void* osViGetNextFramebuffer(void) { return sViNextFb; }

/* ---- timers (osSetTimer) ---- */
#define PS_TIMERS 16
static struct { OSTimer* t; u64 when; } sTimers[PS_TIMERS];
int osSetTimer(OSTimer* t, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg) {
    t->interval = interval;
    t->mq = mq;
    t->msg = msg;
    u64 when = ps_now_count() + (countdown ? countdown : interval);
    for (int i = 0; i < PS_TIMERS; i++) {
        if (sTimers[i].t == t || sTimers[i].t == NULL) { sTimers[i].t = t; sTimers[i].when = when; return 0; }
    }
    ps_printf("[os] sin timers libres\n");
    return -1;
}
int osStopTimer(OSTimer* t) {
    for (int i = 0; i < PS_TIMERS; i++) {
        if (sTimers[i].t == t) { sTimers[i].t = NULL; return 0; }
    }
    return -1;
}

/* ---- estadisticas ---- */
static u64 sStatStart, sGfxFrames, sViFrames, sIdle;
static u64 sIdleVi, sIdleTimer, sIdleAud; static u32 sIdleDumps, sIdleWarps;
extern int gPsIdleDump;
/* Reloj de cuadros del juego (30 por segundo): para saber si va atrasado contra el tiempo real */
static u64 sFrameClock;
u32 gPsLostUs; /* tiempo de juego perdonado (microsegundos, acumulado) */
#define FRAME_PERIOD (N64_COUNT_HZ / 30ull)
/* Cuadros de atraso que el juego puede recuperar corriendo de mas. Normal: 3 (0.1 s, lo que hace falta para saltar
 * cuadros a ritmo parejo en una escena pesada). Con la musica del intro sonando: 12 (0.4 s): ahi la imagen tiene que
 * volver a donde va la musica despues de cada carga de escena, y lo recupera sin dibujar esos cuadros (un brinco).
 * alcance.txt = N lo cambia para comparar (12 = como antes). */
extern u32 gPsIntro[8] __attribute__((weak));
s32 ps_flag(const char* name, s32 dflt) __attribute__((weak));
int ps_catchup_frames(void) {
    static int sN = -1;
    if (sN < 0) { sN = ps_flag ? (int)ps_flag("alcance", 3) : 3; if (sN < 1) sN = 1; if (sN > 30) sN = 30; }
    if (&gPsIntro && gPsIntro[0] == 1 && sN < 12) return 12;
    return sN;
}
/* Atraso del juego al mandar el ultimo cuadro (microsegundos; negativo = va adelantado). Lo lee el host para decidir
 * si ese cuadro se dibuja (durante el intro; fuera de ahi usa su propio reloj, que es el mismo sin correcciones). */
s32 gPsDebtUs;
extern u32 gPsGameVi __attribute__((weak));
static u32 sViSubmit; /* reloj del juego (retrazos) cuando se mando el ultimo cuadro */
void ps_stats_gfx_frame(void) {
    static u32 total;
    sGfxFrames++;
    if (++total == 300) gPsIdleDump = 1;
    u64 now = ps_now_count();
    {
        s64 d = sFrameClock ? ((s64)now - (s64)sFrameClock) * 8 / 375 : 0;
        if (d > 2000000) d = 2000000;
        if (d < -2000000) d = -2000000;
        gPsDebtUs = (s32)d;
    }
    if (&gPsGameVi) sViSubmit = gPsGameVi;
    /* El juego recupera un atraso saltandose cuadros de imagen, pero solo hasta ps_catchup_frames() cuadros: lo que
     * pase de ahi se perdona (se pierde). Con 12 (0.4 s), despues de cada atoron el juego corria un rato mas rapido
     * que el tiempo real para alcanzar al reloj y se veia acelerado. */
    const u64 cap = (u64)ps_catchup_frames() * FRAME_PERIOD;
    if (sFrameClock == 0) sFrameClock = now;
    else if (now > sFrameClock + cap) {
        gPsLostUs += (u32)((now - cap - sFrameClock) * 8 / 375);
        sFrameClock = now - cap;
    }
    sFrameClock += FRAME_PERIOD;
}
/* Intro con musica: la imagen sigue a la musica que se oye (3D140.c mide y llama a ps_frame_clock_shift).
 * ps_frame_target_vi256: en que retrazo del juego deberia ir la imagen ahora mismo segun este reloj, contado desde
 * baseVi y en 1/256 de retrazo. Es continuo: al mandar un cuadro el reloj avanza un cuadro y sViSubmit dos retrazos;
 * solo brinca cuando se perdona atraso o cuando lo corre ps_frame_clock_shift. */
s32 ps_frame_target_vi256(u32 baseVi) {
    s64 ph;
    if (!sFrameClock) return 0;
    ph = ((s64)ps_now_count() - (s64)sFrameClock) * 256 / (s64)VI_PERIOD;
    if (ph > 256 * 240) ph = 256 * 240;
    if (ph < -256 * 240) ph = -256 * 240;
    return (s32)(sViSubmit + 2 - baseVi) * 256 + (s32)ph;
}
void ps_frame_clock_resync(void) {
    const u64 now = ps_now_count();
    if (sFrameClock && now > sFrameClock) sFrameClock = now;
}
/* us > 0: la musica va adelante de la imagen; el reloj de cuadros se adelanta (el juego queda atrasado y se pone al
 * corriente sin dibujar esos cuadros). us < 0: la musica va atras (se quedo sin sonido); la imagen la espera: se
 * recorren el reloj de cuadros y el siguiente retrazo. */
static u64 sViHold; /* espera pendiente de la imagen: se reparte alargando los siguientes retrazos */
void ps_frame_clock_shift(s32 us) {
    u64 d = (u64)(us < 0 ? -us : us) * 375 / 8;
    if (!sFrameClock || us == 0) return;
    if (us > 0) {
        sFrameClock -= d;
        if (sViHold > d) sViHold -= d; else sViHold = 0;
    } else {
        /* El reloj de cuadros se recorre de una vez (la medicion ya lo ve corregido) pero los retrazos se alargan
         * poco a poco (medio periodo cada uno, ps_events_poll): la imagen va mas lenta un momento en vez de
         * congelarse, y el hilo de audio, que despierta con cada retrazo, sigue armando sonido (parar los
         * retrazos de golpe lo dejaba sin correr justo cuando la musica ya venia corta). */
        sFrameClock += d;
        sViHold += d;
        if (sViHold > N64_COUNT_HZ) sViHold = N64_COUNT_HZ;
    }
}
/* Si el juego va atrasado y todos esperan el siguiente retrazo, el retrazo se adelanta a "ahora": en el N64
 * cada cuadro espera un retrazo nuevo y con el 3DS lento eso dejaba el CPU sin hacer nada hasta 16 ms por cuadro */
int gPsViWarp = 1;
static int ps_game_behind(u64 now) { return sFrameClock && now > sFrameClock; }
static void stats_tick(u64 now) {
    if (sStatStart == 0) { sStatStart = now; return; }
    if (sViFrames < 300) return;
    u64 total = now - sStatStart;
    u32 ms = (u32)(total / (N64_COUNT_HZ / 1000));
    u32 busy = (u32)((total > sIdle ? total - sIdle : 0) / (N64_COUNT_HZ / 1000));
    ps_printf("[perf] %u frames gfx en %u ms (CPU ocupado %u ms) libre: vi %u timer %u audio %u ms, retrazos adelantados %u\n", (u32)sGfxFrames, ms, busy,
              (u32)(sIdleVi / 1000), (u32)(sIdleTimer / 1000), (u32)(sIdleAud / 1000), sIdleWarps);
    sIdleVi = sIdleTimer = sIdleAud = 0; sIdleWarps = 0;
    { extern void ps_os_thread_times(void); ps_os_thread_times(); }
    { static int d; if (sGfxFrames == 0 && d++ == 3) { extern void ps_os_dump_threads(void); ps_os_dump_threads(); } }
    sStatStart = now; sGfxFrames = 0; sViFrames = 0; sIdle = 0;
}

/* Audio del RSP en otro nucleo (3DS): la plataforma avisa cuando termino */
int ps_audio_async_busy(void) __attribute__((weak));
int ps_audio_async_poll(void) __attribute__((weak));

void ps_events_poll(void) {
    u64 now = ps_now_count();
    if (ps_audio_async_poll && ps_audio_async_poll()) ps_post_event(OS_EVENT_SP);
    if (sNextVi == 0) sNextVi = now + VI_PERIOD;
    /* Los retrazos que pasaron mientras el host dibujaba se entregan (hasta 8, ~133 ms), pero de uno en uno: el
     * siguiente sale cuando todos los hilos ya atendieron el anterior (estan esperando) o paso un periodo real.
     * El hilo de audio arma un cuadro de sonido por retrazo y su fila es de 1: entregados todos juntos solo veia
     * uno, y con cuadros del juego de mas de ~46 ms (un cuadro de audio al maximo) el sonido se quedaba corto. */
    if (now >= sNextVi && (sViIdle || now - sLastViAt >= VI_PERIOD)) {
        sLastViAt = now;
        sViIdle = 0;
        sNextVi += VI_PERIOD;
        if (sViHold) { /* la imagen espera a la musica del intro (ps_frame_clock_shift) */
            const u64 h = sViHold < VI_PERIOD / 2 ? sViHold : VI_PERIOD / 2;
            sNextVi += h;
            sViHold -= h;
        }
        /* Mas atras que eso (una carga) se vuelve a sincronizar. */
        if (sNextVi + 8 * VI_PERIOD < now) sNextVi = now + VI_PERIOD;
        /* ... y fuera del intro no se entregan mas retrazos atrasados que los cuadros que se pueden recuperar */
        else if (ps_catchup_frames() < 4 && sNextVi + 2 * (u64)ps_catchup_frames() * VI_PERIOD < now)
            sNextVi = now - 2 * (u64)ps_catchup_frames() * VI_PERIOD;
        if (sViNextFb) sViCurFb = sViNextFb;
        ps_post_event(OS_EVENT_VI);
        if (sViMq && ++sViCounter >= sViCount) { sViCounter = 0; ps_send_from_event(sViMq, sViMsg); }
        sViFrames++;
        stats_tick(now);
    }
    for (int i = 0; i < PS_TIMERS; i++) {
        if (sTimers[i].t && now >= sTimers[i].when) {
            OSTimer* t = sTimers[i].t;
            ps_send_from_event(t->mq, t->msg);
            if (t->interval) sTimers[i].when += t->interval;
            else sTimers[i].t = NULL;
        }
    }
    while (sPendCount) {
        OSMesgQueue* mq = sPending[sPendHead].mq;
        OSMesg msg = sPending[sPendHead].msg;
        if (++sPendHead >= PS_PENDING) sPendHead = 0;
        sPendCount--;
        ps_send_from_event(mq, msg);
    }
}

void ps_request_exit(void);

int gPsIdleDump;
void ps_events_idle_wait(void) {
    sViIdle = 1; /* todos esperan: ya se puede entregar otro retrazo atrasado */
    if (!gPsHost->pump()) { ps_request_exit(); return; }
    u64 now = ps_now_count();
    u64 next = sNextVi;
    int why = 0; /* 0 vi, 1 timer, 2 audio */
    for (int i = 0; i < PS_TIMERS; i++) {
        if (sTimers[i].t && sTimers[i].when < next) { next = sTimers[i].when; why = 1; }
    }
    if (sPendCount) next = now;
    if (gPsViWarp && next > now && next == sNextVi && ps_game_behind(now)) {
        sNextVi = now; /* ps_events_poll lo entrega en la siguiente vuelta */
        sIdleWarps++;
        return;
    }
    if (next > now) {
        u64 us = (next - now) * 8ull / 375ull;
        if (us > 20000) us = 20000;
        if (us > 500 && ps_audio_async_busy && ps_audio_async_busy()) { us = 500; why = 2; } /* el audio termina pronto */
        if (us) {
            extern void ps_os_account_idle(u64 a, u64 b);
            u64 a = gPsHost->time_us();
            gPsHost->sleep_us((u32)us);
            u64 b = gPsHost->time_us();
            ps_os_account_idle(a, b);
            if (why == 0) sIdleVi += b - a; else if (why == 1) sIdleTimer += b - a; else sIdleAud += b - a;
            if (gPsIdleDump && sIdleDumps < 0 && b - a > 3000) {
                extern void ps_os_dump_threads(void);
                sIdleDumps++;
                ps_printf("[libre] %u us esperando %s\n", (u32)(b - a), why == 0 ? "vi" : why == 1 ? "timer" : "audio");
                ps_os_dump_threads();
            }
        }
        sIdle += ps_now_count() - now;
    }
}

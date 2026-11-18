/* Hilos y mensajes de libultra sobre un planificador cooperativo.
 * El N64 solo cambia de hilo cuando uno se bloquea, cede o lo despierta un evento de mayor prioridad;
 * aqui se reproduce eso con fibras en un solo hilo del sistema. */
#include "ps_os.h"
int snprintf(char* s, __SIZE_TYPE__ n, const char* f, ...);

#define PS_MAX_THREADS 32
#define PS_STACK_SIZE  (256 * 1024)

typedef struct {
    OSThread* t;
    void (*entry)(void*);
    void* arg;
    PsFiber* fiber;
    void* waitpc;   /* depuracion: quien llamo la ultima espera */
    u64 busyUs;     /* tiempo corriendo desde el ultimo reporte */
} PsThreadInfo;

static PsThreadInfo sInfo[PS_MAX_THREADS];
static OSThread sBootThread;          /* el hilo del sistema; solo se reanuda para salir */
OSThread* gPsCurThread = &sBootThread;
static OSThread* sRunQueue = NULL;    /* ordenada por prioridad descendente */
static PsFiber* sZombie = NULL;       /* fibra terminada, se libera desde otra */
static int sExitRequested = 0;

static PsThreadInfo* info_of(OSThread* t) {
    for (int i = 0; i < PS_MAX_THREADS; i++) {
        if (sInfo[i].t == t) return &sInfo[i];
    }
    return NULL;
}

static PsFiber* fiber_of(OSThread* t) {
    if (t == &sBootThread) return ps_fiber_current_boot();
    PsThreadInfo* in = info_of(t);
    return in ? in->fiber : NULL;
}

/* Inserta por prioridad (FIFO entre iguales), como __osEnqueueThread */
static void ps_enqueue(OSThread** q, OSThread* t) {
    while (*q && (*q)->priority >= t->priority) q = &(*q)->next;
    t->next = *q;
    *q = t;
}

static void ps_remove(OSThread** q, OSThread* t) {
    for (; *q; q = &(*q)->next) {
        if (*q == t) { *q = t->next; t->next = NULL; return; }
    }
}

static OSThread* ps_pop(OSThread** q) {
    OSThread* t = *q;
    if (t) { *q = t->next; t->next = NULL; }
    return t;
}

void ps_make_runnable(OSThread* t) {
    t->state = OS_STATE_RUNNABLE;
    t->queue = &sRunQueue;
    ps_enqueue(&sRunQueue, t);
}

static u64 sLastSwitchUs, sBootUs;
/* El tiempo dormido esperando eventos no cuenta para ningun hilo (va en "sis") */
void ps_os_account_idle(u64 startUs, u64 endUs) {
    PsThreadInfo* pi = info_of(gPsCurThread);
    if (pi) pi->busyUs += startUs - sLastSwitchUs;
    sBootUs += endUs - startUs;
    sLastSwitchUs = endUs;
}
static void switch_to(OSThread* prev, OSThread* next) {
    u64 now = gPsHost->time_us();
    PsThreadInfo* pi = info_of(prev);
    if (pi) pi->busyUs += now - sLastSwitchUs; else sBootUs += now - sLastSwitchUs;
    sLastSwitchUs = now;
    gPsCurThread = next;
    { extern void ps_mem_event(u32 kind, void* addr, u32 size); ps_mem_event(9, (void*)(u32)next->id, 0); }
    if (next != prev) {
        PsFiber* from = fiber_of(prev);
        if (from == NULL) from = sZombie;
        ps_fiber_switch(from, fiber_of(next));
    }
    if (sZombie && fiber_of(gPsCurThread) != sZombie) {
        ps_fiber_destroy(sZombie);
        sZombie = NULL;
    }
}

/* Cambia al hilo listo de mayor prioridad. El hilo actual ya debe estar encolado o bloqueado.
 * Los hilos de prioridad 0 (idle del N64, un bucle infinito) no se ejecutan: se espera el siguiente evento. */
void ps_dispatch(void) {
    OSThread* prev = gPsCurThread;
    OSThread* next;
    for (;;) {
        if (sExitRequested) { next = &sBootThread; break; }
        ps_events_poll();
        if (sRunQueue && sRunQueue->priority > OS_PRIORITY_IDLE) { next = ps_pop(&sRunQueue); break; }
        ps_events_idle_wait();
    }
    next->state = OS_STATE_RUNNING;
    next->queue = NULL;
    switch_to(prev, next);
}

/* Si hay alguien listo con mas prioridad que el actual, cederle el CPU (preempcion del N64) */
void ps_preempt_check(void) {
    if (gPsCurThread == &sBootThread) return;
    if (sRunQueue && sRunQueue->priority > gPsCurThread->priority) {
        ps_make_runnable(gPsCurThread);
        ps_dispatch();
    }
}

/* El hilo del sistema entra aqui despues de que el juego crea sus hilos; regresa solo al salir. */
void ps_run_until_exit(void) {
    sBootThread.priority = OS_PRIORITY_APPMAX + 1;
    sBootThread.state = OS_STATE_WAITING;
    ps_dispatch();
}

void ps_request_exit(void) { sExitRequested = 1; }

static void thread_entry(void* p) {
    OSThread* t = (OSThread*)p;
    PsThreadInfo* in = info_of(t);
    in->entry(in->arg);
    osDestroyThread(NULL);
}

void osCreateThread(OSThread* t, OSId id, void (*entry)(void*), void* arg, void* sp, OSPri pri) {
    (void)sp; /* pilas propias: las del N64 son demasiado chicas para codigo nativo */
    int slot = -1;
    for (int i = 0; i < PS_MAX_THREADS; i++) {
        if (sInfo[i].t == t) { slot = i; break; }
        if (slot < 0 && sInfo[i].t == NULL) slot = i;
    }
    if (slot < 0) { ps_printf("[os] demasiados hilos\n"); return; }
    if (sInfo[slot].t == t && sInfo[slot].fiber) ps_fiber_destroy(sInfo[slot].fiber);
    sInfo[slot].t = t;
    sInfo[slot].entry = entry;
    sInfo[slot].arg = arg;
    sInfo[slot].fiber = ps_fiber_create(thread_entry, t, PS_STACK_SIZE);
    t->next = NULL;
    t->priority = pri;
    t->queue = NULL;
    t->tlnext = NULL;
    t->state = OS_STATE_STOPPED;
    t->flags = 0;
    t->id = id;
    t->fp = 0;
}

void osStartThread(OSThread* t) {
    if (t->state == OS_STATE_STOPPED) {
        ps_make_runnable(t);
        ps_preempt_check();
    }
}

void osStopThread(OSThread* t) {
    if (t == NULL || t == gPsCurThread) {
        gPsCurThread->state = OS_STATE_STOPPED;
        ps_dispatch();
        return;
    }
    if (t->queue) ps_remove(t->queue, t);
    t->queue = NULL;
    t->state = OS_STATE_STOPPED;
}

void osDestroyThread(OSThread* t) {
    if (t == NULL) t = gPsCurThread;
    PsThreadInfo* in = info_of(t);
    if (t != gPsCurThread) {
        if (t->queue) ps_remove(t->queue, t);
        if (in) { ps_fiber_destroy(in->fiber); in->t = NULL; in->fiber = NULL; }
        t->state = OS_STATE_STOPPED;
        return;
    }
    t->state = OS_STATE_STOPPED;
    if (in) { sZombie = in->fiber; in->t = NULL; in->fiber = NULL; }
    ps_dispatch();
}

OSPri osGetThreadPri(OSThread* t) { return (t ? t : gPsCurThread)->priority; }

void osSetThreadPri(OSThread* t, OSPri pri) {
    if (t == NULL) t = gPsCurThread;
    if (t->priority == pri) return;
    t->priority = pri;
    if (t != gPsCurThread && t->state == OS_STATE_RUNNABLE) {
        ps_remove(&sRunQueue, t);
        ps_enqueue(&sRunQueue, t);
    }
    if (t == gPsCurThread && sRunQueue && sRunQueue->priority > pri) {
        ps_make_runnable(t);
        ps_dispatch();
    } else {
        ps_preempt_check();
    }
}

OSId osGetThreadId(OSThread* t) { return (t ? t : gPsCurThread)->id; }
void osYieldThread(void) { ps_make_runnable(gPsCurThread); ps_dispatch(); }

/* ---------------- Mensajes ---------------- */
void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msg, s32 count) {
    mq->mtqueue = NULL;
    mq->fullqueue = NULL;
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msg;
}

static void wake_one(OSThread** q) {
    OSThread* t = ps_pop(q);
    if (t) ps_make_runnable(t);
}

/* Envio desde un "interrupt": nunca bloquea ni cambia de hilo */
s32 ps_send_from_event(OSMesgQueue* mq, OSMesg msg) {
    if (mq == NULL || mq->validCount >= mq->msgCount) return -1;
    s32 idx = mq->first + mq->validCount;
    if (idx >= mq->msgCount) idx -= mq->msgCount;
    mq->msg[idx] = msg;
    mq->validCount++;
    wake_one(&mq->mtqueue);
    return 0;
}

static void block_on(OSThread** q) {
    gPsCurThread->state = OS_STATE_WAITING;
    gPsCurThread->queue = q;
    ps_enqueue(q, gPsCurThread);
    ps_dispatch();
}

s32 osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    if (flag == OS_MESG_BLOCK && mq->validCount >= mq->msgCount) { PsThreadInfo* in = info_of(gPsCurThread); if (in) in->waitpc = __builtin_return_address(0); }
    while (mq->validCount >= mq->msgCount) {
        if (flag != OS_MESG_BLOCK) return -1;
        block_on(&mq->fullqueue);
    }
    s32 idx = mq->first + mq->validCount;
    if (idx >= mq->msgCount) idx -= mq->msgCount;
    mq->msg[idx] = msg;
    mq->validCount++;
    if (mq->mtqueue) { wake_one(&mq->mtqueue); ps_preempt_check(); }
    return 0;
}

s32 osJamMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    while (mq->validCount >= mq->msgCount) {
        if (flag != OS_MESG_BLOCK) return -1;
        block_on(&mq->fullqueue);
    }
    mq->first = mq->first + mq->msgCount - 1;
    if (mq->first >= mq->msgCount) mq->first -= mq->msgCount;
    mq->msg[mq->first] = msg;
    mq->validCount++;
    if (mq->mtqueue) { wake_one(&mq->mtqueue); ps_preempt_check(); }
    return 0;
}

/* Reporte de tiempo por hilo (ms desde el ultimo reporte) */
void ps_os_thread_times(void) {
    char buf[200];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "[hilos]");
    for (int i = 0; i < PS_MAX_THREADS && n < (int)sizeof(buf) - 16; i++) {
        if (!sInfo[i].t) continue;
        u32 ms = (u32)(sInfo[i].busyUs / 1000);
        if (ms) n += snprintf(buf + n, sizeof(buf) - n, " %d:%u", (int)sInfo[i].t->id, ms);
        sInfo[i].busyUs = 0;
    }
    {
        extern unsigned long long gPsAudioWaitUs __attribute__((weak)), gPsAudioWaitSchedUs __attribute__((weak));
        u32 w = &gPsAudioWaitUs ? (u32)(gPsAudioWaitUs / 1000) : 0;
        u32 ws = &gPsAudioWaitSchedUs ? (u32)(gPsAudioWaitSchedUs / 1000) : 0;
        if (&gPsAudioWaitUs) gPsAudioWaitUs = gPsAudioWaitSchedUs = 0;
        extern s32 gPsVoicesPeak __attribute__((weak)), gPsVoicesStolen __attribute__((weak));
        s32 vp = &gPsVoicesPeak ? gPsVoicesPeak : 0, vs = &gPsVoicesStolen ? gPsVoicesStolen : 0;
        if (&gPsVoicesPeak) gPsVoicesPeak = gPsVoicesStolen = 0;
        snprintf(buf + n, sizeof(buf) - n, " sis:%u espera_audio:%u (planif %u) voces:%d robadas:%d\n", (u32)(sBootUs / 1000), w, ws, (int)vp, (int)vs);
    }
    sBootUs = 0;
    ps_printf("%s", buf);
}

/* Depuracion: estado de todos los hilos */
void ps_os_dump_threads(void) {
    for (int i = 0; i < PS_MAX_THREADS; i++) {
        OSThread* t = sInfo[i].t;
        if (!t) continue;
        ps_printf("[hilo] id %d pri %d estado %d cola %p espera desde %p%s\n", (int)t->id, (int)t->priority, (int)t->state,
                  (void*)t->queue, sInfo[i].waitpc, t == gPsCurThread ? " (actual)" : "");
    }
}

s32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flag) {
    if (flag == OS_MESG_BLOCK && mq->validCount == 0) { PsThreadInfo* in = info_of(gPsCurThread); if (in) in->waitpc = __builtin_return_address(0); }
    while (mq->validCount == 0) {
        if (flag != OS_MESG_BLOCK) {
            /* sondeo: dejar avanzar el "hardware" por si el juego espera en un bucle */
            ps_events_poll();
            ps_preempt_check();
            if (mq->validCount == 0) return -1;
            break;
        }
        block_on(&mq->mtqueue);
    }
    if (msg) *msg = mq->msg[mq->first];
    mq->first++;
    if (mq->first >= mq->msgCount) mq->first = 0;
    mq->validCount--;
    if (mq->fullqueue) { wake_one(&mq->fullqueue); ps_preempt_check(); }
    return 0;
}

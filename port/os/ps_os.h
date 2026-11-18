#ifndef PS_OS_H
#define PS_OS_H
/* Capa de libultra del port: hilos cooperativos, mensajes, eventos de "hardware" y servicios del host.
 * El juego corre en un solo hilo del sistema; cada OSThread del N64 es una fibra. */
#include <ultra64.h>

/* ---- Servicios que da cada plataforma (Linux/Windows con libultraship, 3DS) ---- */
typedef struct PsHost {
    void (*print)(const char* s);
    u64 (*time_us)(void);                                   /* reloj monotono en microsegundos */
    void (*sleep_us)(u32 us);
    u32 (*rom_read)(u32 romOffset, void* dst, u32 size);    /* lectura de un rango de la ROM (desde el .o2r) */
    int (*pump)(void);                                      /* procesa la ventana/entrada; 0 = salir */
    void (*read_pad)(OSContPad* pads);                      /* estado de los controles */
    void (*gfx_task)(OSTask* task);                         /* dibuja una tarea de graficos del RSP */
    void (*exit_game)(void);
} PsHost;
extern const PsHost* gPsHost;
void ps_printf(const char* fmt, ...);

/* ---- Fibras (una por OSThread) ---- */
typedef struct PsFiber PsFiber;
PsFiber* ps_fiber_current_boot(void);                         /* contexto del hilo del sistema */
PsFiber* ps_fiber_create(void (*entry)(void*), void* arg, u32 stackSize);
void ps_fiber_switch(PsFiber* from, PsFiber* to);
void ps_fiber_destroy(PsFiber* f);

/* ---- Planificador ---- */
extern OSThread* gPsCurThread;
void ps_dispatch(void);
void ps_preempt_check(void);
void ps_make_runnable(OSThread* t);
s32  ps_send_from_event(OSMesgQueue* mq, OSMesg msg);
void ps_run_until_exit(void);                                 /* entra al planificador desde el hilo del sistema */

/* ---- Eventos de "hardware" ---- */
void ps_events_poll(void);
void ps_events_idle_wait(void);
void ps_post_event(s32 event);
void ps_post_msg(OSMesgQueue* mq, OSMesg msg);
u64  ps_now_count(void);                                      /* contador del N64 (46.875 MHz) */
void ps_stats_gfx_frame(void);

/* ---- Avisos de memoria (para la cache de modelos del host) ---- */
enum {
    PS_MEM_ASSET = 1,     /* un asset se copio a este rango */
    PS_MEM_ALLOC = 2,     /* main_pool_alloc */
    PS_MEM_SUBALLOC = 3,  /* mem_pool_alloc */
    PS_MEM_BUMP = 4,      /* func_80002DCC */
    PS_MEM_WRITEBACK = 5, /* osWritebackDCache: el CPU escribio datos para el RSP */
    PS_MEM_INVAL = 6,     /* osInvalDCache: va a llegar un DMA */
    PS_MEM_WB_ALL = 7,    /* osWritebackDCacheAll (sin rango) */
    PS_MEM_DMA = 8        /* lectura de la ROM por PI (lo avisa el host) */
};
void ps_mem_event(u32 kind, void* addr, u32 size);

#endif

/* Fibras para el bloque del juego en ARM (3DS): cambio de contexto propio (blob_asm.S). */
#include "global.h"
#include "os/ps_os.h"

typedef struct { u32 r[8]; u32 sp, lr; double d[8]; u32 fpscr; } PsArmCtx;

struct PsFiber {
    PsArmCtx ctx;
    void (*entry)(void*);
    void* arg;
    u8* stack;
};

void ps_arm_ctx_switch(PsArmCtx* save, PsArmCtx* load);
void ps_arm_fiber_trampoline(void);
u32 ps_arm_get_fpscr(void);

#define MAX_FIBERS 32
#define STACK_SIZE (64 * 1024)
static struct PsFiber sFibers[MAX_FIBERS];
static u8 sStacks[MAX_FIBERS][STACK_SIZE] __attribute__((aligned(16)));
static u8 sUsed[MAX_FIBERS];
static struct PsFiber sBoot;

PsFiber* ps_fiber_current_boot(void) { return &sBoot; }

void ps_arm_fiber_main(struct PsFiber* f) {
    f->entry(f->arg);
    for (;;) {}
}

PsFiber* ps_fiber_create(void (*entry)(void*), void* arg, u32 stackSize) {
    (void)stackSize;
    for (int i = 0; i < MAX_FIBERS; i++) {
        if (sUsed[i]) continue;
        sUsed[i] = 1;
        struct PsFiber* f = &sFibers[i];
        f->entry = entry;
        f->arg = arg;
        f->stack = sStacks[i];
        for (int k = 0; k < 8; k++) { f->ctx.r[k] = 0; f->ctx.d[k] = 0; }
        f->ctx.r[0] = (u32)f;                               /* r4 = fibra */
        f->ctx.sp = (u32)(f->stack + STACK_SIZE - 16);
        f->ctx.lr = (u32)ps_arm_fiber_trampoline;
        f->ctx.fpscr = ps_arm_get_fpscr() | 0x03000000u; /* RunFast: FZ+DN, sin rebotes de VFP al kernel */
        return f;
    }
    ps_printf("[fiber] sin fibras libres\n");
    return NULL;
}

void ps_fiber_switch(PsFiber* from, PsFiber* to) { ps_arm_ctx_switch(&from->ctx, &to->ctx); }

void ps_fiber_destroy(PsFiber* f) {
    if (f == NULL || f == &sBoot) return;
    sUsed[f - sFibers] = 0;
}

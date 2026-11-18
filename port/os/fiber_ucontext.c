/* Fibras con ucontext (Linux/macOS). En Windows se usa CreateFiber y en 3DS un cambio de contexto propio. */
#if !defined(_WIN32) && !defined(__3DS__)
#define _XOPEN_SOURCE 700
#include <ucontext.h>
#include <stdlib.h>
#include "ps_os.h"

struct PsFiber {
    ucontext_t ctx;
    void (*entry)(void*);
    void* arg;
    void* stack;
};

static struct PsFiber sBoot;

PsFiber* ps_fiber_current_boot(void) { return &sBoot; }

static void trampoline(unsigned lo, unsigned hi) {
    struct PsFiber* f = (struct PsFiber*)(((unsigned long)hi << 16 << 16) | (unsigned long)lo);
    f->entry(f->arg);
    /* entry nunca regresa: osDestroyThread despacha a otro hilo */
    abort();
}

/* Las pilas viven dentro de la memoria del juego: el RSP (HLE) recibe apuntadores a variables locales
 * y solo ve una ventana de 16 MB alrededor del ejecutable */
static unsigned char sStackArena[3 << 20] __attribute__((aligned(16)));
static unsigned long sStackUsed;
static void* stack_alloc(u32 size) {
    size = (size + 15) & ~15u;
    if (sStackUsed + size <= sizeof(sStackArena)) { void* p = sStackArena + sStackUsed; sStackUsed += size; return p; }
    return malloc(size);
}
static void stack_free(void* p) {
    if ((unsigned char*)p < sStackArena || (unsigned char*)p >= sStackArena + sizeof(sStackArena)) free(p);
}

PsFiber* ps_fiber_create(void (*entry)(void*), void* arg, u32 stackSize) {
    struct PsFiber* f = calloc(1, sizeof(*f));
    f->entry = entry;
    f->arg = arg;
    f->stack = stack_alloc(stackSize);
    getcontext(&f->ctx);
    f->ctx.uc_stack.ss_sp = f->stack;
    f->ctx.uc_stack.ss_size = stackSize;
    f->ctx.uc_link = NULL;
    unsigned long p = (unsigned long)f;
    makecontext(&f->ctx, (void (*)(void))trampoline, 2, (unsigned)(p & 0xFFFFFFFFu), (unsigned)(p >> 16 >> 16));
    return f;
}

void ps_fiber_switch(PsFiber* from, PsFiber* to) {
    swapcontext(&from->ctx, &to->ctx);
}

void ps_fiber_destroy(PsFiber* f) {
    if (f == NULL || f == &sBoot) return;
    stack_free(f->stack);
    free(f);
}
#endif

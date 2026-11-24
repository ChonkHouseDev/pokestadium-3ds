/* Puente entre las tareas del RSP del juego y el HLE de mupen64plus-rsp-hle (GPLv2).
 * El juego corre en big-endian (MIPS en el tracer, ARM BE8 en el 3DS), asi que el HLE se compila
 * con M64P_BIG_ENDIAN y lee la memoria del juego tal cual.
 * Las direcciones que el juego manda al RSP son apuntadores del port; el HLE las enmascara a 24 bits,
 * por eso "dram" es la base alineada a 16 MB donde vive el juego (0x10000000 tracer, 0x0A000000 3DS). */
#include <stdarg.h>
#include <stdint.h>
void* memcpy(void* d, const void* s, __SIZE_TYPE__ n);
#include "ultra64.h"
#include "os/ps_os.h"
#include "hle_internal.h"

void alist_process_naudio(struct hle_t* hle);
void jpeg_decode_PS(struct hle_t* hle);

static struct hle_t sHle;
static uint8_t sDmem[0x1000] __attribute__((aligned(16)));
static uint8_t sImem[0x1000] __attribute__((aligned(16)));
static unsigned int sRegs[32];
static int sInit;

static void ps_rsp_init(void) {
    sHle.dram = (unsigned char*)((uintptr_t)&sHle & 0xFF000000u);
    sHle.dmem = sDmem;
    sHle.imem = sImem;
    unsigned int** r = &sHle.mi_intr;
    for (int i = 0; i < 17; i++) r[i] = &sRegs[i];
    sInit = 1;
}

void rsp_break(struct hle_t* hle, unsigned int setbits) { *hle->sp_status |= setbits | SP_STATUS_BROKE | SP_STATUS_HALT; }

void HleVerboseMessage(void* u, const char* m, ...) { (void)u; (void)m; }
void HleInfoMessage(void* u, const char* m, ...) { (void)u; (void)m; }
void HleErrorMessage(void* u, const char* m, ...) { (void)u; ps_printf("[rsp] error: %s\n", m); }
void HleWarnMessage(void* u, const char* m, ...) { (void)u; ps_printf("[rsp] aviso: %s\n", m); }
void HleCheckInterrupts(void* u) { (void)u; }
void HleProcessDlistList(void* u) { (void)u; }
void HleProcessAlistList(void* u) { (void)u; }
void HleProcessRdpList(void* u) { (void)u; }
void HleShowCFB(void* u) { (void)u; }
int HleForwardTask(void* u) { (void)u; return -1; }
void mp3_task(struct hle_t* h, unsigned int i, uint32_t a) { (void)h; (void)i; (void)a; } /* Stadium no usa mp3 */

/* Regresa 1 si la tarea se proceso */
static u64 sTAud, sTJpeg;
static u32 sNAud;
int ps_rsp_hle_task(OSTask* t) {
    int r = 0;
    u64 t0 = gPsHost->time_us();
    extern int ps_rsp_hle_task_impl(OSTask* t);
    r = ps_rsp_hle_task_impl(t);
    u64 dt = gPsHost->time_us() - t0;
    if (t->t.type == M_AUDTASK) {
        sTAud += dt;
        if (++sNAud == 120) {
            ps_printf("[rsp] 120 tareas audio %u ms, jpeg %u ms\n", (u32)(sTAud / 1000), (u32)(sTJpeg / 1000));
            sNAud = 0; sTAud = sTJpeg = 0;
        }
    } else sTJpeg += dt;
    return r;
}

int ps_rsp_hle_task_impl(OSTask* t) {
    if (!sInit) ps_rsp_init();
    memcpy(sDmem + 0xFC0, t, 0x40);
    if (t->t.type == M_AUDTASK) {
        alist_process_naudio(&sHle);
        return 1;
    }
    if (t->t.type == 4) {
        jpeg_decode_PS(&sHle);
        return 1;
    }
    return 0;
}

/* Resto de libultra: PI (ROM desde el .o2r), RSP/RDP, VI, audio, controles, EEPROM, Transfer Pak y 64DD. */
#include <stdint.h>
#include "ps_os.h"
#include <PR/os_gbpak.h>
#ifndef DEVICE_TYPE_FLASH
#define DEVICE_TYPE_FLASH 8
#endif

/* ---------------- variables globales de libultra ---------------- */
u32 osMemSize = 0x800000;
s32 osTvType = OS_TV_NTSC;
s32 osResetType = 0;
u32 __OSGlobalIntMask = 0x003FFF01;
u32 osGetMemSize(void) { return osMemSize; }

void osInitialize(void) { osSetTime(0); }
s32 osAfterPreNMI(void) { return 0; }
OSIntMask osSetIntMask(OSIntMask m) { (void)m; return OS_IM_ALL; }
u32 __osDisableInt(void) { return 0; }
void __osRestoreInt(u32 m) { (void)m; }
u32 __osSetFpcCsr(u32 v) { (void)v; return 0x01000800; }
void set_watch_lohi(u32 v) { (void)v; }

/* caches: no hay coherencia que mantener */
void osInvalDCache(void* p, s32 n) { if (n > 0) ps_mem_event(PS_MEM_INVAL, p, (u32)n); }
void osInvalICache(void* p, s32 n) { (void)p; (void)n; }
/* sin coherencia que mantener, pero son la senal de que el CPU cambio datos que lee el RSP */
void osWritebackDCache(void* p, s32 n) { if (n > 0) ps_mem_event(PS_MEM_WRITEBACK, p, (u32)n); }
void osWritebackDCacheAll(void) { ps_mem_event(PS_MEM_WB_ALL, 0, 0); }
/* direcciones: en el port los punteros son nativos */
u32 osVirtualToPhysical(void* p) { return (u32)(uintptr_t)p; }
void* osPhysicalToVirtual(u32 p) { return (void*)(uintptr_t)p; }
void* __osGetActiveQueue(void) { return NULL; }

/* ---------------- PI: lectura de la ROM (rangos servidos desde el .o2r) ---------------- */
static OSPiHandle sCartHandle, sDriveHandle;
OSPiHandle* osCartRomInit(void) {
    sCartHandle.type = DEVICE_TYPE_CART;
    sCartHandle.baseAddress = 0xB0000000;
    return &sCartHandle;
}
OSPiHandle* osDriveRomInit(void) {
    sDriveHandle.type = DEVICE_TYPE_64DD;
    sDriveHandle.baseAddress = 0xA6000000;
    return &sDriveHandle;
}
void osCreatePiManager(OSPri pri, OSMesgQueue* q, OSMesg* m, s32 n) { (void)pri; (void)q; (void)m; (void)n; }
s32 osEPiLinkHandle(OSPiHandle* h) { (void)h; return 0; }
static u32 rom_offset(u32 dev) { return dev & 0x0FFFFFFF; } /* acepta 0x10000000|off o el offset directo */
/* ---------------- Flash de 128 KB (partidas guardadas) ----------------
 * El juego habla con el chip por comandos (estado, ID, borrar, programar pagina); aqui se emula con un
 * arreglo y la plataforma lo guarda (ps_save_load/ps_save_store). */
#define PS_FLASH_SIZE 0x20000
static u8 sFlash[PS_FLASH_SIZE];
static u8 sFlashBuf[128];
static int sFlashInit, sFlashMode; /* 0 lectura, 1 estado, 2 ID */
static u32 sFlashStatus;
static s32 sFlashErase = -1;        /* -2 = todo, >=0 = sector (pagina inicial) */
int sPsFlashDirty;
void ps_save_load(void* buf, u32 size) __attribute__((weak));
static int is_flash(OSPiHandle* h, u32 dev) {
    if (h && h->type == DEVICE_TYPE_FLASH) return 1;
    dev &= 0x1FFFFFFF;
    return dev >= 0x08000000u && dev < 0x10000000u;
}
static void flash_init(void) {
    if (sFlashInit) return;
    sFlashInit = 1;
    for (u32 i = 0; i < PS_FLASH_SIZE; i++) sFlash[i] = 0xFF;
    if (ps_save_load) ps_save_load(sFlash, PS_FLASH_SIZE);
}
static void flash_cmd(u32 data) {
    flash_init();
    switch (data >> 24) {
        case 0xD2: sFlashMode = 1; break;                         /* estado */
        case 0xE1: sFlashMode = 2; break;                         /* ID */
        case 0xF0: sFlashMode = 0; break;                         /* leer */
        case 0xB4: break;                                         /* cargar buffer de pagina (DMA) */
        case 0x3C: sFlashErase = -2; break;                       /* borrar todo */
        case 0x4B: sFlashErase = (s32)(data & 0xFFFF); break;     /* borrar sector (16 KB) */
        case 0x78:                                                /* ejecutar borrado */
            if (sFlashErase == -2) { for (u32 i = 0; i < PS_FLASH_SIZE; i++) sFlash[i] = 0xFF; }
            else if (sFlashErase >= 0) {
                u32 off = ((u32)sFlashErase & ~0x7Fu) * 128;
                for (u32 i = 0; i < 0x4000 && off + i < PS_FLASH_SIZE; i++) sFlash[off + i] = 0xFF;
            }
            sFlashErase = -1; sFlashStatus = 0x08; sPsFlashDirty = 1;
            break;
        case 0xA5: {                                              /* programar pagina */
            u32 off = (data & 0xFFFF) * 128;
            if (off + 128 <= PS_FLASH_SIZE) for (int i = 0; i < 128; i++) sFlash[off + i] = sFlashBuf[i];
            sFlashStatus = 0x04; sPsFlashDirty = 1;
            break;
        }
    }
}
const u8* ps_flash_data(void) { return sFlash; }

s32 osEPiReadIo(OSPiHandle* h, u32 dev, u32* data) {
    u8 b[4];
    if (h && h->type == DEVICE_TYPE_64DD) { *data = 0; return -1; }
    if (is_flash(h, dev)) { flash_init(); *data = sFlashStatus; return 0; }
    gPsHost->rom_read(rom_offset(dev), b, 4);
    *data = ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | b[3];
    return 0;
}
s32 osEPiWriteIo(OSPiHandle* h, u32 dev, u32 data) {
    if (is_flash(h, dev)) {
        if (dev & 0x10000) flash_cmd(data); else sFlashStatus = data;
    }
    return 0;
}
s32 osEPiStartDma(OSPiHandle* h, OSIoMesg* mb, s32 dir) {
    if (h && h->type == DEVICE_TYPE_FLASH) {
        flash_init();
        u8* d = (u8*)mb->dramAddr;
        if (dir == OS_READ && sFlashMode == 2) {
            static const u8 id[8] = { 0x11, 0x11, 0x80, 0x01, 0x00, 0xC2, 0x00, 0x1D }; /* MX 1 Mbit (paginas de 128) */
            for (u32 i = 0; i < mb->size && i < 8; i++) d[i] = id[i];
        } else if (dir == OS_READ) {
            u32 off = mb->devAddr & 0x1FFFF;
            for (u32 i = 0; i < mb->size && off + i < PS_FLASH_SIZE; i++) d[i] = sFlash[off + i];
        } else {
            for (u32 i = 0; i < mb->size && i < 128; i++) sFlashBuf[i] = d[i];
        }
        if (mb->hdr.retQueue) ps_post_msg(mb->hdr.retQueue, (OSMesg)mb);
        return 0;
    }
    if (dir == OS_READ && !(h && h->type == DEVICE_TYPE_64DD)) {
        u32 got = gPsHost->rom_read(rom_offset(mb->devAddr), mb->dramAddr, mb->size);
        if (got != mb->size) ps_printf("[pi] lectura corta rom=%08x size=%x got=%x\n", mb->devAddr, mb->size, got);
    }
    if (mb->hdr.retQueue) ps_post_msg(mb->hdr.retQueue, (OSMesg)mb);
    return 0;
}
s32 osEPiRawStartDma(OSPiHandle* h, s32 dir, u32 dev, void* dram, u32 size) {
    (void)h;
    if (dir == OS_READ) gPsHost->rom_read(rom_offset(dev), dram, size);
    return 0;
}

/* ---------------- RSP / RDP ---------------- */
int ps_rsp_hle_task(OSTask* t) __attribute__((weak));
int ps_audio_async_start(OSTask* t) __attribute__((weak));
void osSpTaskLoad(OSTask* t) { (void)t; }
void osSpTaskStartGo(OSTask* t) {
    if (t->t.type == M_GFXTASK) {
        ps_stats_gfx_frame();
        gPsHost->gfx_task(t);
        ps_post_event(OS_EVENT_SP);
        ps_post_event(OS_EVENT_DP);
    } else {
        /* audio (M_AUDTASK) y JPEG (tipo 4) via HLE en las builds big-endian.
         * En el 3DS el audio corre en el otro nucleo y el aviso de fin llega en ps_events_poll */
        if (t->t.type == M_AUDTASK && ps_audio_async_start && ps_audio_async_start(t)) {
            extern int ps_audio_async_early __attribute__((weak));
            if (&ps_audio_async_early && ps_audio_async_early) ps_post_event(OS_EVENT_SP);
            return;
        }
        {
            extern void ps_audio_async_wait(void) __attribute__((weak));
            if (ps_audio_async_wait) ps_audio_async_wait();
        }
        if (ps_rsp_hle_task) ps_rsp_hle_task(t);
        ps_post_event(OS_EVENT_SP);
    }
}
void osSpTaskYield(void) {}
OSYieldResult osSpTaskYielded(OSTask* t) { (void)t; return 0; }
void osDpSetStatus(u32 s) { (void)s; }
void osDpGetCounters(u32* c) { c[0] = c[1] = c[2] = c[3] = 0; }

/* ---------------- VI (lo dinamico esta en os_events.c) ---------------- */
void osCreateViManager(OSPri p) { (void)p; }
void osViSetMode(OSViMode* m) { (void)m; }
void osViBlack(u8 b) { (void)b; }
void osViRepeatLine(u8 b) { (void)b; }
void osViSetSpecialFeatures(u32 f) { (void)f; }
void osViSetYScale(f32 s) { (void)s; }
void osViSetXScale(f32 s) { (void)s; }
OSViMode osViModeTable[56];
OSViMode osViModeNtscLpn1;
OSViMode osViModeMpalLpn1;

/* ---------------- Audio (AI) ---------------- */
/* El host entrega las muestras (s16 estereo big-endian) a su salida de audio */
void ps_audio_push(const void* buf, u32 size, u32 freq) __attribute__((weak));
u32 ps_audio_pending(void) __attribute__((weak));
static u32 sAiFreq = 32000;
s32 osAiSetFrequency(u32 f) { sAiFreq = f; return (s32)f; }
s32 osAiSetNextBuffer(void* buf, u32 size) { if (ps_audio_push) ps_audio_push(buf, size, sAiFreq); return 0; }
u32 osAiGetLength(void) { return ps_audio_pending ? ps_audio_pending() : 0; }
u32 osAiGetStatus(void) { return 0; }
s32 __osAiDeviceBusy(void) { return 0; }

/* ---------------- Controles (SI) ---------------- */
int ps_gb_present(int ch) __attribute__((weak));
s32 osContInit(OSMesgQueue* mq, u8* bitpattern, OSContStatus* data) {
    (void)mq;
    *bitpattern = 1;
    for (int i = 0; i < MAXCONTROLLERS; i++) {
        /* el control 1 siempre; otro control solo "esta conectado" si la plataforma le puso un cartucho en su
         * Transfer Pak (3DS: el segundo Transfer Pak de la pantalla de abajo). Sin botones: solo lleva el cartucho. */
        const int on = i == 0 || (ps_gb_present && ps_gb_present(i));
        if (on) *bitpattern |= (u8)(1 << i);
        data[i].type = on ? CONT_TYPE_NORMAL : 0;
        data[i].status = 0;
        data[i].errno = on ? 0 : CONT_NO_RESPONSE_ERROR;
    }
    return 0;
}
s32 osContStartQuery(OSMesgQueue* mq) { (void)mq; ps_post_event(OS_EVENT_SI); return 0; }
void osContGetQuery(OSContStatus* data) { u8 b; osContInit(NULL, &b, data); }
s32 osContStartReadData(OSMesgQueue* mq) { (void)mq; ps_post_event(OS_EVENT_SI); return 0; }
void osContGetReadData(OSContPad* pad) { gPsHost->read_pad(pad); }
/* ---------------- Transfer Pak ----------------
 * El cartucho de Game Boy lo pone la plataforma (en el 3DS: un .sav de la SD elegido al arrancar, con su controlador
 * de bancos emulado en el host). Aqui van las funciones osGbpak* de libultra a nivel de su interfaz: estado, encendido,
 * lectura/escritura por direcciones del Game Boy y la cabecera. Sin plataforma o sin cartucho: "no conectado". */
int ps_gb_present(int ch) __attribute__((weak));
int ps_gb_rw(int ch, int write, u32 addr, u8* buf, u32 size) __attribute__((weak));
void ps_gb_power(int ch, int on) __attribute__((weak));
static u8 sGbPower[4];
static int gb_has(int ch) { return ch >= 0 && ch < 4 && ps_gb_present && ps_gb_present(ch); }

s32 osPfsIsPlug(OSMesgQueue* mq, u8* pattern) {
    int i;
    (void)mq;
    *pattern = 0;
    for (i = 0; i < 4; i++) if (gb_has(i)) *pattern |= (u8)(1 << i);
    return 0;
}
s32 __osContRamRead(OSMesgQueue* mq, int ch, u16 addr, u8* data) { (void)mq; (void)ch; (void)addr; (void)data; return PFS_ERR_NOPACK; }
s32 __osContRamWrite(OSMesgQueue* mq, int ch, u16 addr, u8* data, int force) { (void)mq; (void)ch; (void)addr; (void)data; (void)force; return PFS_ERR_NOPACK; }

s32 osGbpakInit(OSMesgQueue* mq, OSPfs* pfs, int ch) {
    pfs->status = 0;
    if (!gb_has(ch)) return PFS_ERR_NOPACK;
    pfs->queue = mq;
    pfs->status = PFS_GBPAK_INITIALIZED;
    pfs->channel = ch;
    pfs->activebank = 0x84;
    pfs->banks = 0xFF;
    pfs->version = 0xFF;
    pfs->dir_size = 0xFF;
    return 0;
}
s32 osGbpakPower(OSPfs* pfs, s32 flag) {
    if (!gb_has(pfs->channel)) return PFS_ERR_NOPACK;
    sGbPower[pfs->channel] = (u8)(flag != OS_GBPAK_POWER_OFF);
    if (ps_gb_power) ps_gb_power(pfs->channel, sGbPower[pfs->channel]);
    return 0;
}
s32 osGbpakGetStatus(OSPfs* pfs, u8* status) {
    *status = 0;
    if (!gb_has(pfs->channel)) return PFS_ERR_NOPACK;
    /* cartucho puesto; con corriente, la linea de reset arriba. Nunca "se movio el cartucho" ni "hubo reset". */
    *status = OS_GBPAK_GBCART_ON;
    if (sGbPower[pfs->channel]) *status |= OS_GBPAK_POWER | OS_GBPAK_RSTB_STATUS;
    return 0;
}
s32 osGbpakReadWrite(OSPfs* pfs, u16 flag, u16 address, u8* buffer, u16 size) {
    if (!gb_has(pfs->channel)) return PFS_ERR_NOPACK;
    size &= (u16)~0x1F; /* libultra mueve bloques de 32 bytes: lo que sobra no se toca */
    if (size == 0) return 0;
    return ps_gb_rw(pfs->channel, flag == OS_WRITE, address, buffer, size) ? 0 : PFS_ERR_CONTRFAIL;
}
s32 osGbpakReadId(OSPfs* pfs, OSGbpakId* id, u8* status) {
    static const u8 mmc_type[] = { 0x00, 0x01, 0x01, 0x01, 0xFF, 0x02, 0x02, 0xFF, 0x00, 0x00,
                                   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0x03, 0x03, 0x03, 0x03 };
    u8 buf[96];
    u8 isum = 0;
    int i;
    s32 ret = osGbpakGetStatus(pfs, status);
    if (ret != 0) return ret;
    if (!(*status & OS_GBPAK_POWER)) {
        ret = osGbpakPower(pfs, OS_GBPAK_POWER_ON);
        if (ret != 0) return ret;
    }
    ret = osGbpakReadWrite(pfs, OS_READ, 0x100, buf, sizeof(buf));
    if (ret != 0) return ret;
    ret = osGbpakGetStatus(pfs, status);
    if (ret != 0) return ret;
    for (i = 0x34; i < 0x4E; i++) isum += buf[i]; /* la suma de la cabecera, como libultra */
    if ((isum + 0x19) & 0xFF) return PFS_ERR_CONTRFAIL;
    bcopy(buf, id, 0x50);
    if (id->cart_type < 0x14) pfs->version = (int)mmc_type[id->cart_type];
    pfs->dir_size = (int)id->ram_size;
    return 0;
}
s32 osGbpakCheckConnector(OSPfs* pfs, u8* status) {
    /* libultra compara lecturas de la ROM para ver que el conector haga contacto; aqui no hay conector */
    s32 ret = osGbpakGetStatus(pfs, status);
    if (ret != 0) return ret;
    if (!(*status & OS_GBPAK_POWER)) {
        ret = osGbpakPower(pfs, OS_GBPAK_POWER_ON);
        if (ret != 0) return ret;
    }
    return osGbpakGetStatus(pfs, status);
}

/* ---------------- EEPROM 16K en RAM (guardado a disco: pendiente) ---------------- */
static u8 sEeprom[2048];
s32 osEepromProbe(OSMesgQueue* mq) { (void)mq; return EEPROM_TYPE_16K; }
s32 osEepromLongRead(OSMesgQueue* mq, u8 blk, u8* buf, int n) {
    (void)mq;
    for (int i = 0; i < n; i++) buf[i] = sEeprom[(blk * 8 + i) & 2047];
    return 0;
}
s32 osEepromLongWrite(OSMesgQueue* mq, u8 blk, u8* buf, int n) {
    (void)mq;
    for (int i = 0; i < n; i++) sEeprom[(blk * 8 + i) & 2047] = buf[i];
    return 0;
}

/* ---------------- 64DD: Stadium solo lo detecta; reportamos "no conectado" ---------------- */
u8 leoBootID[8];
#define LEO_STUB(name) s32 name() { return -1; }
LEO_STUB(LeoBootGame) LEO_STUB(LeoByteToLBA) LEO_STUB(LeoCJCreateLeoManager) LEO_STUB(LeoLBAToByte)
LEO_STUB(LeoReadDiskID) LEO_STUB(LeoReadRTC) LEO_STUB(LeoReadWrite) LEO_STUB(LeoReset) LEO_STUB(LeoResetClear)
LEO_STUB(LeoSeek) LEO_STUB(LeoSetRTC) LEO_STUB(LeoSpdlMotor) LEO_STUB(LeoTestUnitReady)
LEO_STUB(leoClr_queue) LEO_STUB(leoClr_reset) LEO_STUB(leoInquiry) LEO_STUB(leoMode_sel) LEO_STUB(leoRd_capacity)
LEO_STUB(leoRead) LEO_STUB(leoReadDiskId) LEO_STUB(leoReadTimer) LEO_STUB(leoRezero) LEO_STUB(leoSeek)
LEO_STUB(leoSetTimer) LEO_STUB(leoStart_stop) LEO_STUB(leoTest_unit_rdy) LEO_STUB(leoTranslate) LEO_STUB(leoWrite)

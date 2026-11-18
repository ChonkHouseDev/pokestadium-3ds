#include "3D140.h"
#include "lib/ultralib/include/PR/leo.h"
#include "src/373A0.h"
#include "src/libnumus/player.h"
#include "src/3D140.h"
#include "src/435D0.h"
#include "src/libnaudio/n_libaudio_sc.h"
#include "src/libnaudio/n_libaudio_sn_sc.h"
#include "src/45720.h"
#include "src/4A3E0.h"
#include "src/4BDC0.h"

typedef union {
    struct {
        s16 type;
    } gen;
    struct {
        s16 type;
        struct AudioInfo* info;
    } done;
} AudioMsg; // size = 0x8

typedef struct AudioInfo {
    /* 0x00 */ s16* data;
    /* 0x04 */ s16 frameSamples;
    /* 0x08 */ AudioMsg msg;
} AudioInfo; // size = 0x10

#define NUM_ACMD_LISTS 2
#define NUM_OUTPUT_BUFFERS 2
#define MAX_MESGS 3
#define NUM_DMA_BUFFERS 48
#define SAMPLES 184

typedef struct AMAudioMgr {
    /* 0x000 */ Acmd* ACMDList[NUM_ACMD_LISTS];
    /* 0x008 */ AudioInfo* audioInfo[NUM_OUTPUT_BUFFERS];
    /* 0x010 */ char unk10[0x228];
    ///* 0x010 */ OSThread thread;
    ///* 0x1C0 */ OSMesgQueue audioFrameMsgQ;
    ///* 0x1D8 */ OSMesg audioFrameMsgBuf[MAX_MESGS];
    ///* 0x1E0 */ OSMesgQueue audioReplyMsgQ;
    ///* 0x1F8 */ OSMesg audioReplyMsgBuf[MAX_MESGS];
    /* 0x238 */ ALGlobals g;
} AMAudioMgr; // size = 0x288

typedef struct AMDMABuffer {
    /* 0x00 */ ALLink node;
    /* 0x08 */ u32 startAddr;
    /* 0x0C */ u32 lastFrame;
    /* 0x10 */ char* ptr;
} AMDMABuffer; // size = 0x14

typedef struct AMDMAState {
    /* 0x00 */ u8 initialized;
    /* 0x04 */ AMDMABuffer* firstUsed;
    /* 0x08 */ AMDMABuffer* firstFree;
} AMDMAState; // size = 0xC

typedef struct unk_D_800FCB48 {
    /* 0x00 */ u16 unk_00[16];
    /* 0x20 */ s32 unk_20;
    /* 0x24 */ s32 unk_24;
    /* 0x28 */ u16 unk_28[16];
    /* 0x48 */ s32 unk_48;
    /* 0x4C */ s32 unk_4C;
    /* 0x50 */ u16 unk_50[16];
    /* 0x70 */ s32 unk_70;
    /* 0x74 */ s32 unk_74;
    /* 0x78 */ u32 unk_78;
    /* 0x7C */ s32 unk_7C;
    /* 0x80 */ s32 unk_80;
} unk_D_800FCB48; // size = 0x84

typedef struct unk_D_80078548 {
    /* 0x00 */ u8 unk_00;
    /* 0x01 */ u8 unk_01;
} unk_D_80078548;

// .data
extern u8 D_80077D90[];// = { 0, 0, 0, 0 };
extern f32 D_80077D94;// = 0.8000000119f;
extern u8 D_80077D98[];// = { 0, 0, 0, 0 };
extern u8 D_80077D9C[];// = { 0, 0, 0, 0 };
extern u16 D_80077DA0;
extern u32 D_80077DA4;
extern u8 D_80077DA8;
extern s32 D_80077DAC;
extern u32 audFrameCt;
extern u32 nextDMA;
#ifdef PORT
static s32 gPsBigFrames;
/* Diagnostico de sonidos que no salen (efectos, gritos, narrador): cada lugar donde el sintetizador o el reproductor
 * tiran algo en silencio. 0 actualizaciones de voz sin lugar (se pierde un arranque/volumen/tono), 1 minimo de
 * actualizaciones libres, 2 comandos del juego tirados (cola llena), 3 efectos sin canal libre, 4 notas sin voz
 * virtual, 5 notas sin voz fisica, 6 lecturas de muestras sin buffer, 7 cambios de banco de efectos con efectos en cola, 8 max comandos
 * del RSP por cuadro, 9 max cola de comandos (bytes), 10 efectos arrancados, 11 efectos pedidos, 12 efectos
 * rechazados (bloqueo o banco), 13 veces que una voz (narrador, grito) se quedo sin muestras a media frase, 14 actualizaciones libres ahora,
 * 15 voces robadas, 16 muestras de voz (narrador y gritos) entregadas, 17 muestras de voz que tocaba entregar en ese
 * tiempo (iguales si la voz sale a su velocidad), 18 voces que empezaron. */
u32 gPsSnd[20];
#endif
extern u32 curAcmdList;
#ifdef PORT
#define PS_AUDQ 6 /* tareas de audio que pueden estar pendientes en el otro nucleo (igual que PS_AUD_Q en blob_host.c) */
static Acmd* sPsACMDList[PS_AUDQ];
static AudioInfo* sPsInfo[PS_AUDQ];
#endif
extern AudioInfo* lastInfo;
extern s32 min_only_one;
// global
extern s32 mus_active_fade_handle;
extern f32 mus_fade_start_volume;
extern u8 mus_fade_target_volume;
extern f32 mus_fade_step_rate;
extern f32 D_80077DD4;
extern f32 D_80077DD8;
extern f32 D_80077DDC;
extern f32 D_80077DE0;
extern u8 mus_sound_effect_blocked;

extern u16 D_80077DF0[];
extern u16 D_80077E00[];
extern u16 D_80077E10[];
extern u16 D_80077E18[];
extern u16 D_80077E20[];
extern u16 D_80077E28[];
extern u16 D_80077E30[];
extern u16 D_80077E38[];
extern u16 D_80077E40[];
extern u16 D_80077E48[];
extern u16 D_80077E50[];
extern u16 D_80077E58[];
extern u16 D_80077E60[];
extern u16 D_80077E68[];
extern u16 D_80077E70[];
extern u16 D_80077E78[];
extern u16 D_80077E88[];
extern u16 D_80077E90[];
extern u16 D_80077E98;
extern u16 D_80077EA4;
extern u16 D_80077EB4;
extern u16 D_80077EC0;
extern u16 D_80077ECC;
extern u16 D_80077ED8;
extern u16 D_80077EE4;
extern u16 D_80077EF0;
extern u16 D_80077EF8;
extern u16 D_80077F04;
extern u16 D_80077F0C;
extern u16 D_80077F14;
extern u16 D_80077F1C;
extern u16 D_80077F24;
extern u16 D_80077F2C;
extern u16 D_80077F34;
extern u16 D_80077F3C;
extern u16 D_80077F44;
extern u16 D_80077F50;
extern u16 D_80077F58;
extern u16 D_80077F60;
extern u16 D_80077F6C[];
extern u16 D_80077F74[];
extern u16 D_80077F7C[];
extern u16 D_80077F88[];
extern u16 D_80077F90[];
extern u16 D_80077F98[];
extern u16 D_80077FA4[];
extern u16 D_80077FAC[];
extern u16 D_80077FB4[];
extern u16 D_80077FC0[];
extern u16 D_80077FC8[];
extern u16 D_80077FD0[];
extern u16 D_80077FD8[];
extern u16 D_80077FE4[];
extern u16 D_80077FEC[];
extern u16 D_80077FF4[];
extern u16 D_80077FFC[];
extern u16 D_80078004[];
extern u16 D_8007800C[];
extern u16 D_80078014[];
extern u8 D_8007801C;
extern u8 D_80078024;
extern u8 D_80078030;
extern u8 D_80078038;
extern u16 D_80078040[];
extern u16 D_80078048[];
extern u16 D_80078050;
extern u16 D_80078058;
extern u16 D_80078060;
extern u16 D_80078068[];
extern u8 D_80078070;
extern u16 D_80078078;
extern u16 D_80078080;
extern u16 D_80078088;
extern u16 D_80078090;
extern u16 D_80078098;
extern u16 D_800780A0;
extern u16 D_800780A8[];
extern u8 D_800780B3[]; // needs addressing
extern u8 D_800780B4[];
extern u8 D_80078180[];
extern u8 D_800781E0[];
extern u8 D_800782A4[];
extern s32 D_80078388;
extern u8 D_8007838C[4];
extern u8 D_80078390[2];
extern u8 D_80078394[4];
extern u8 D_80078398[4];
extern u8 D_8007839C[];
extern u8 D_800783A0;
extern u32 D_800783A4[4];
extern u32 D_800783B4;
extern s32 D_800783B8;
extern u8 D_800783BC;
extern u8 D_800783C0;
extern u8 D_800783C4;
extern u32 D_800783C8;
extern u32 D_800783CC;
extern u8 D_800783D0;
extern u8 D_800783D4;
extern u8 D_800783D8;
extern u8 D_800783DC;
extern u8 D_800783E0;
extern u8 D_800783E4;
extern u8 D_800783E8;
extern u8 D_800783EC;
extern s16 D_800783F0;
extern s16 D_800783F4;
extern s16 D_800783F8;
extern u8 D_800783FC;
extern u8 D_80078400;
extern u32 D_80078404;
extern s32 D_80078408;
extern u8 D_8007840C;
extern u16 D_80078410[1][9];
extern u16 D_80078446[1][9];
extern u16 D_800784EA[1][9];
extern u32 D_80078544;
extern unk_D_80078548 D_80078548[];
extern u8 D_80078554[];
extern u8 D_80078568[];

// .rodata

// const f64 D_8007C7E0 = 0.699999999999999956f;
// const f64 D_8007C7E8 = 0.599999999999999978f;
// const f64 D_8007C7F0 = 0.400000000000000022f;
// const f64 D_8007C7F8 = 0.299999999999999989f;

// const f32 D_8007CB80 = 1.100000024f;
// const f32 D_8007CB84 = 1.100000024f;
// const f32 D_8007CB88 = 1.200000048f;
// const f32 D_8007CB8C = 1.100000024f;
// const f32 D_8007CB90 = 0.009999999776f;

extern f64 D_8007C7E0;
extern f64 D_8007C7E8;
extern f64 D_8007C7F0;
extern f64 D_8007C7F8;

extern f32 D_8007CB80;
extern f32 D_8007CB84;
extern f32 D_8007CB88;
extern f32 D_8007CB8C;
extern f32 D_8007CB90;

// .bss
s32 D_800FC820;
s8 D_800FC824;
u8 D_800FC825;
s32 D_800FC828;
AMAudioMgr __am;
AMDMAState dmaState;
s32 minFrameSize;
u32 frameSize;
s32 maxFrameSize;
s32 maxRSPCmds;
s32 D_800FCAD4;
s32 D_800FCAD8;
OSMesgQueue audDMAMessageQ;
AMDMABuffer* dmaBuffs;
OSIoMesg* audDMAIOMesgBuf;
OSMesg* audDMAMessageBuf;
u32 dmaBufferLen;
OSPiHandle* D_800FCB08[4];
// s32 D_800FCB0C[3];
unk_D_800FCB18* D_800FCB18[4];
s32 D_800FCB28[2];
u8 D_800FCB30[2];
u8 D_800FCB38[2][8];
unk_D_800FCB48 D_800FCB48[2];
s32 D_800FCC50[20];
s8 D_800FCCA0;
s8 D_800FCCA1;
u8 D_800FCCA2;
u32 D_800FCCA4;
s32 D_800FCCA8;
u8 D_800FCCAC;
u8 D_800FCCAD;
u8 D_800FCCAE;
u8 D_800FCCAF;
u8 D_800FCCB0;
u8 D_800FCCB1;
u8 D_800FCCB2;
u8 D_800FCCB3;
u8 D_800FCCB4;
u8 D_800FCCB5;
u8 D_800FCCB6;
u8 D_800FCCB7;
u8 D_800FCCB8[2];
u8 D_800FCCBA[2];
u8 D_800FCCBC[2];
u8 D_800FCCBE[2];
u8 D_800FCCC0[2];
u8 D_800FCCC2[2];
s8 D_800FCCC4[2];
u8 D_800FCCC6[2];
u8 D_800FCCC8[2];
u8 D_800FCCCA[2];
u8 D_800FCCCC[2];
u8 D_800FCCCE[2];
u8 D_800FCCD0[2];
u8 D_800FCCD2[2];
u8 D_800FCCD4[2];
u8 D_800FCCD6[2];
s8 D_800FCCD8[0x18];
u32 D_800FCCF0[10];
u8 D_800FCD18[0x28];

// function prototypes
void func_800420F0(u16, u16*, u8*);
u32 func_80042158(u32, unk_D_800FCB18*, unk_D_800FCB18*);
s32 func_80042850(u8);
ALDMAproc __amDmaNew(AMDMAState** state);

void amCreateAudioMgr(ALSynConfig* c, amConfig* amc, u32 num_dma_buffers, s32 arg3, s32 arg4) {
    u32 i;
    f32 fsize;

    D_800FCB08[0] = osCartRomInit();
    dmaBufferLen = arg3;

    dmaState.initialized = FALSE;

    c->dmaproc = __amDmaNew;
    c->outputRate = amc->outputRate;

    i = num_dma_buffers;
    i <<= 1;
    dmaBuffs = alHeapAlloc(c->heap, 1, num_dma_buffers * sizeof(AMDMABuffer));
    audDMAIOMesgBuf = alHeapAlloc(c->heap, 1, i * sizeof(OSIoMesg));
    audDMAMessageBuf = alHeapAlloc(c->heap, 1, i * sizeof(OSMesg));

    fsize = (f32)amc->framesPerField * c->outputRate / arg4;
    frameSize = (s32)fsize;
    if (frameSize < fsize) {
        frameSize++;
    }

    frameSize = ((frameSize / SAMPLES) + 1) * SAMPLES;
    minFrameSize = frameSize - SAMPLES;
    /* PORT: los cuadros de sonido son del tamano del N64 (552 o 368 muestras, uno por retrazo de sonido). Antes
     * se armaba uno grande por cuadro del juego (hasta 1472) y todo lo que el juego hace "una vez por cuadro de
     * sonido" iba a la mitad de velocidad o no cabia: la voz del narrador y los gritos (se decodifican 320 muestras
     * por cuadro y su buffer es de 69 ms), sus esperas y desvanecidos, el eco, las voces que se liberan. Cuando el
     * juego tarda, el hilo de audio arma varios cuadros seguidos (DDC0.c). */
    maxFrameSize = frameSize + 0x100;
#ifdef PORT
    {   /* para comparar en el emulador: cuadrosgrandes.txt = 1 vuelve a los cuadros grandes de antes */
        extern s32 ps_flag(const char* name, s32 dflt);
        gPsBigFrames = ps_flag("cuadrosgrandes", 0) == 1;
        if (gPsBigFrames) maxFrameSize = 8 * SAMPLES;
    }
#endif

    alInit(&__am.g, c);

    dmaBuffs[0].node.prev = NULL;
    dmaBuffs[0].node.next = NULL;

    for (i = 0; i < num_dma_buffers - 1; i++) {
        alLink(&dmaBuffs[i + 1].node, &dmaBuffs[i].node);
        dmaBuffs[i].ptr = alHeapAlloc(c->heap, 1, arg3);
    }

    dmaBuffs[i].ptr = alHeapAlloc(c->heap, 1, arg3);

    for (i = 0; i < 2; i++) {
        __am.ACMDList[i] = alHeapAlloc(c->heap, 1, amc->maxACMDSize * sizeof(Acmd));
    }
#ifdef PORT
    /* PORT: tercera lista de comandos: el juego puede armar un cuadro mientras el nucleo 1 procesa otro y hay uno formado */
    sPsACMDList[0] = __am.ACMDList[0];
    sPsACMDList[1] = __am.ACMDList[1];
    for (i = 2; i < PS_AUDQ; i++) sPsACMDList[i] = alHeapAlloc(c->heap, 1, amc->maxACMDSize * sizeof(Acmd));
#endif

    maxRSPCmds = amc->maxACMDSize;

    for (i = 0; i < 3; i++) {
        __am.audioInfo[i] = alHeapAlloc(c->heap, 1, sizeof(AudioInfo));
        __am.audioInfo[i]->msg.done.type = 0;
        __am.audioInfo[i]->msg.done.info = __am.audioInfo[i];
        __am.audioInfo[i]->data = alHeapAlloc(c->heap, 1, maxFrameSize * 4);
    }

#ifdef PORT
    /* PORT: un buffer de salida por tarea pendiente */
    for (i = 0; i < PS_AUDQ; i++) {
        if (i < 3) { sPsInfo[i] = __am.audioInfo[i]; continue; }
        sPsInfo[i] = alHeapAlloc(c->heap, 1, sizeof(AudioInfo));
        sPsInfo[i]->msg.done.type = 0;
        sPsInfo[i]->msg.done.info = sPsInfo[i];
        sPsInfo[i]->data = alHeapAlloc(c->heap, 1, maxFrameSize * 4);
    }
#endif
    osCreateMesgQueue(&audDMAMessageQ, audDMAMessageBuf, num_dma_buffers << 1);

    D_800FCAD4 = c->outputRate;
    D_800FCAD8 = c->maxVVoices;
    D_800FC824 = 0;

    func_80044B20(c->heap, minFrameSize, maxFrameSize);
}

s32 __amDMA(s32 addr, s32 len, void* state) {
    void* foundBuffer;
    s32 delta;
    AMDMABuffer* sp34;
    OSIoMesg* ioMesg;
    AMDMABuffer* dmaPtr;
    AMDMABuffer* lastDmaPtr;
    OSPiHandle* sp2C;
    s32 buffEnd;

    if (D_800FC820 & 0x80000000) {
        return osVirtualToPhysical(addr);
    }

    sp2C = D_800FCB08[0];
    dmaPtr = dmaState.firstUsed;
    lastDmaPtr = NULL;
    while (dmaPtr) {
        buffEnd = dmaPtr->startAddr + dmaBufferLen;

        if (dmaPtr->startAddr > addr) {
            break;
        }

        if (buffEnd >= (addr + len)) {
            dmaPtr->lastFrame = audFrameCt;
            return osVirtualToPhysical(dmaPtr->ptr + addr - dmaPtr->startAddr);
        }

        lastDmaPtr = dmaPtr;
        dmaPtr = dmaPtr->node.next;
    }

    dmaPtr = dmaState.firstFree;
    if (!dmaPtr) {
#ifdef PORT
        gPsSnd[6]++;
#endif
        return osVirtualToPhysical(dmaState.firstUsed);
    }

    dmaState.firstFree = dmaPtr->node.next;
    alUnlink(&dmaPtr->node);

    if (lastDmaPtr != NULL) {
        alLink(&dmaPtr->node, &lastDmaPtr->node);
    } else if (dmaState.firstUsed) {
        lastDmaPtr = dmaState.firstUsed;

        dmaState.firstUsed = dmaPtr;
        dmaPtr->node.next = lastDmaPtr;
        dmaPtr->node.prev = NULL;

        lastDmaPtr->node.prev = &dmaPtr->node;
    } else {
        dmaState.firstUsed = dmaPtr;
        dmaPtr->node.next = NULL;
        dmaPtr->node.prev = NULL;
    }

    foundBuffer = dmaPtr->ptr;
    delta = addr & 1;
    addr -= delta;
    dmaPtr->startAddr = addr;
    dmaPtr->lastFrame = audFrameCt;

    ioMesg = audDMAIOMesgBuf + nextDMA++;
    ioMesg->hdr.pri = OS_MESG_PRI_HIGH;
    ioMesg->hdr.retQueue = &audDMAMessageQ;
    ioMesg->devAddr = addr;
    ioMesg->dramAddr = foundBuffer;
    ioMesg->size = dmaBufferLen;

    osInvalDCache(foundBuffer, dmaBufferLen);
    osEPiStartDma(sp2C, ioMesg, OS_READ);

    return osVirtualToPhysical(foundBuffer) + delta;
}

#ifdef PORT
u32 gPsAudTargetMs; /* nivel de sonido en cola que se esta manteniendo (para el registro) */
u32 gPsGameVi;      /* reloj del juego en retrazos: lo avanza dp_intro.c al presentar cada cuadro */
static s32 sPsCredit; /* muestras de sonido que el reloj del juego ya debe y todavia no se arman */
static s32 sPsDiscard; /* este cuadro de sonido se arma (la musica avanza) pero no se entrega */
/* Verificacion del intro (los Pokemon brincan al ritmo de la musica). 0 estado: 0 fuera, 2 intro sin musica todavia,
 * 1 musica sonando; 1 reloj del juego (retrazos) cuando arranco la musica; 2 muestras armadas en ese momento;
 * 3 toques pendientes de la cruz (con signo: negativo adelanta la musica, positivo la atrasa; los pone el host);
 * 4 ajuste aplicado en muestras (con signo). */
u32 gPsIntro[8]; /* 5 ajuste a mano en toques (con signo, lo pone el host: corre el objetivo), 6 cuadros de adelanto con
                  * que se mando la musica, 7 toques que dio el ajuste automatico (con signo) */
extern u32 gPsAudSync[12];
static s32 sPsLvAvg;   /* reserva de la salida, promedio movil (muestras) */
static s32 sPsLeadVi;  /* retrazos que faltaban para el cuadro original cuando se mando la musica */

#define PS_INTRO_LAG_MS 30 /* la musica se deja este tanto detras de la imagen (en el N64 tambien sale 1-2 retrazos despues) */
/* cuantos cuadros antes hay que mandar la musica del intro para que se oiga a tiempo: lo que mide la reserva ahora */
s32 ps_intro_lead(void) {
    s32 ms = (s32)((s64)sPsLvAvg * 1000 / (D_800FCAD4 ? D_800FCAD4 : 32000)) - PS_INTRO_LAG_MS;
    s32 k = (ms + 16) / 33;
    if (k < 0) k = 0;
    if (k > 11) k = 11; /* el juego espera 11 cuadros antes de mandarla: no se puede antes de eso */
    sPsLeadVi = 2 * k;
    gPsIntro[6] = (u32)k;
    return k;
}
static s32 sPsSkip; /* muestras de musica que se arman y no se entregan (adelantar la musica a mano) */
void ps_intro_mark(int k) {
    if (k == 1) {
        /* el reloj de referencia es el del cuadro original (la musica se mando sPsLeadVi retrazos antes) */
        if (gPsIntro[0] == 2) {
            /* el atraso que traiga el juego de antes (la carga del intro) se perdona: si lo recuperara ahora, la
             * imagen brincaria adelante de la musica que apenas arranca y luego tendria que esperarla */
            extern void ps_frame_clock_resync(void);
            ps_frame_clock_resync();
            gPsIntro[1] = gPsGameVi + (u32)sPsLeadVi; gPsIntro[2] = gPsAudSync[1]; gPsIntro[0] = 1; gPsIntro[4] = 0; gPsIntro[7] = 0;
        }
    } else {
        gPsIntro[0] = (u32)k;
        if (k == 2) { gPsIntro[3] = 0; gPsIntro[4] = 0; gPsIntro[5] = 0; gPsIntro[6] = 0; gPsIntro[7] = 0; }
    }
}
/* para el registro del host: 0 reloj del juego (retrazos), 1 muestras armadas, 2 muestras acreditadas sin cuadros
 * del juego (espera), 3 muestras de credito tiradas (tope), 4 muestras de ajuste de reserva, 5 frecuencia,
 * 6 suma de la reserva al armar (muestras), 7 cuadros armados, 8 reserva maxima, 9 muestras descartadas */
u32 gPsAudSync[12]; /* 10 reserva objetivo (ms), 11 reserva minima de la ultima ventana (muestras) */
/* En cada retrazo que recibe el hilo de audio: suma al credito lo que avanzo el reloj del juego; 1 si ya toca
 * armar un cuadro de sonido (si no, ese retrazo se deja pasar). */
static s32 sPsFillTarget; /* reserva que se mantiene en la salida (muestras) */
/* 1: el sonido se arma segun lo que falta en la salida. Tambien la musica del intro: ahi la imagen sigue a la musica
 * (mas abajo, "la imagen sigue a la musica") y no al reves.
 * sonidoreloj.txt = 1 regresa al modo anterior (credito por tiempo real, con silencios y descartes; en el intro,
 * credito por el reloj del juego y toques de 34 ms a la musica) para comparar. */
static s32 ps_audio_fill(void) {
    static s32 sOld = -1;
    extern s32 ps_flag(const char* name, s32 dflt);
    if (sOld < 0) sOld = ps_flag("sonidoreloj", 0) == 1;
    return !sOld && !gPsBigFrames;
}
s32 ps_audio_due(void) {
    /* Reloj del sonido.
     * Normal: no hay reloj, se arma lo que falta en la salida para mantener la reserva (ps_audio_fill, mas abajo).
     * La musica va al ritmo real y no depende de cuantos cuadros presente el juego.
     * Con sonidoreloj.txt (modo anterior): credito por TIEMPO REAL; si este hilo estuvo parado mas de 0.4 s (una
     * carga), ese sonido no se arma.
     * Mientras suena la musica del intro: el RELOJ DEL JUEGO (cuadros presentados). Ahi los Pokemon brincan al
     * ritmo de la musica y lo que importa es que vayan juntos: si la imagen se detiene o recupera un atraso, la
     * musica hace lo mismo. */
    static OSTime sT0;
    static u32 sFrac, sFracVi, sVi0, sWait;
    const OSTime now = osGetTime();
    const u32 rate = (u32)(D_800FCAD4 ? D_800FCAD4 : 32000);
    const u32 vi = gPsGameVi;
    u32 us = sT0 ? (u32)((now - sT0) * 8 / 375) : 0;
    sT0 = now;
    if (ps_audio_fill()) {
        /* sin credito */
    } else if (gPsIntro[0] == 1) {
        u32 d = vi - sVi0;
        if (d > 24) { gPsAudSync[3] += (d - 24) * (rate / 60); d = 24; }
        sFracVi += d * rate;
        sPsCredit += (s32)(sFracVi / 60);
        sFracVi %= 60;
    } else {
        u64 acc;
        if (us > 400000) { gPsAudSync[3] += (u32)((u64)(us - 400000) * rate / 1000000); us = 400000; }
        acc = (u64)us * rate + sFrac;
        sPsCredit += (s32)(acc / 1000000);
        sFrac = (u32)(acc % 1000000);
    }
    sVi0 = vi;
    if (ps_audio_fill()) {
        /* Fuera del intro: la salida se mantiene llena hasta la reserva y nada mas (como el N64 con AI_LEN). Si
         * falta, se arma; si sobra, no se arma y la salida se va vaciando sola. Nunca se tira sonido ya armado ni
         * se mete silencio: antes, cada vez que el juego se atoraba mas que la reserva salia un silencio y luego se
         * descartaban cuadros de sonido (cortes a media musica, se oian como estatica). Ahora un atoron solo deja
         * el hueco de lo que duro, y la musica sigue donde iba.
         * La reserva cubre lo que tarda este hilo en volver a correr (el mayor hueco reciente + margen). */
        static u32 sGapMax;
        u32 g = us > 250000 ? 250000 : us; /* una carga no cuenta */
        u32 targetUs;
        if (g > sGapMax) sGapMax = g; else sGapMax -= (sGapMax - g) / 64;
        targetUs = sGapMax + sGapMax / 4 + 60000;
        if (targetUs < 100000) targetUs = 100000;
        if (targetUs > 320000) targetUs = 320000;
        /* Intro: reserva grande (0.4 s). Ahi cada escena carga sus modelos (atorones de 0.2 a 0.35 s) y con la reserva
         * normal la musica se quedaba sin sonido en cada carga. El retraso no importa: la musica se manda antes
         * (ps_intro_lead) y la imagen sigue a lo que se oye. */
        if (gPsIntro[0] != 0 && targetUs < 400000) targetUs = 400000;
        sPsFillTarget = (s32)(((u64)targetUs * rate) / 1000000);
        gPsAudTargetMs = targetUs / 1000;
        gPsAudSync[10] = targetUs / 1000;
        gPsAudSync[0] = vi; gPsAudSync[5] = rate;
        sPsCredit = 0; sFrac = 0; sFracVi = 0;
        return (s32)(osAiGetLength() >> 2) < sPsFillTarget;
    }
    if (sPsCredit > (s32)(rate * 45 / 100)) { gPsAudSync[3] += (u32)(sPsCredit - (s32)(rate * 45 / 100)); sPsCredit = (s32)(rate * 45 / 100); }
    gPsAudSync[0] = vi; gPsAudSync[5] = rate;
    /* con 3/4 de cuadro del juego ya se arma: un cuadro de sonido por cada cuadro del juego */
    if (sPsCredit >= (s32)frameSize + (s32)frameSize / 2) { sWait = 0; return 1; }
    /* un resto chico no se queda esperando mas de 3 retrazos */
    if (sPsCredit >= minFrameSize - SAMPLES / 2 && ++sWait >= 3) { sWait = 0; return 1; }
    return 0;
}
#endif
#ifdef PORT
/* Despues de armar un cuadro de sonido: 1 si todavia se debe otro (el hilo de audio arma varios seguidos, cada uno
 * del tamano de un retrazo como en el N64, hasta ponerse al corriente con su reloj). */
s32 ps_audio_big(void) { return gPsBigFrames; }
s32 ps_audio_more(void) {
    extern int ps_audio_room(void) __attribute__((weak));
    if (gPsBigFrames) return 0;
    if (ps_audio_room && !ps_audio_room()) return 0; /* la fila del otro nucleo esta llena: lo que falte, en el siguiente retrazo */
    if (ps_audio_fill()) return (s32)(osAiGetLength() >> 2) < sPsFillTarget;
    return sPsCredit >= (s32)frameSize - SAMPLES / 2;
}
/* Los buffers de voz que el RSP lee directo de la memoria del juego (narrador, gritos, sonido de Game Boy) se copian
 * para cada tarea: el otro nucleo la procesa despues, cuando el juego ya escribio (o borro) ahi lo que sigue. */
static s16 sPsSnap[PS_AUDQ][0x900];
static u32 sPsSnapPos, sPsSnapSlot;
void* ps_stream_snap(const void* src, u32 bytes) {
    s16* dst;
    if (sPsSnapPos + bytes > sizeof(sPsSnap[0])) return (void*)src;
    dst = (s16*)((u8*)sPsSnap[sPsSnapSlot] + sPsSnapPos);
    bcopy(src, dst, bytes);
    sPsSnapPos += (bytes + 15) & ~15u;
    return dst;
}
#endif
OSTask* func_8003CADC(OSTask* arg0) {
    s16* audioPtr;
    Acmd* cmdp;
    s32 cmdLen;
    s32 samplesLeft;
    OSTask sp38;
    s32 pad[2];
    AudioInfo* info;
    AMAudioMgr* mgr;

#ifdef PORT
    { extern void ps_audio_frame_begin(void); ps_audio_frame_begin(); }
#endif
#ifdef PORT
    info = sPsInfo[audFrameCt % PS_AUDQ];
#else
    info = __am.audioInfo[audFrameCt % 3];
#endif
    audioPtr = osVirtualToPhysical(info->data);
#ifdef PORT
    samplesLeft = osAiGetLength() >> 2; // PORT: lo que le queda a la salida de audio del host
#else
    samplesLeft = IO_READ(AI_LEN_REG) >> 2;
#endif

    if ((D_80077DA8 == 0) && (lastInfo != NULL)) {
        osAiSetNextBuffer(lastInfo->data, lastInfo->frameSamples << 2);
    }

#ifdef PORT
    /* PORT: el tamano del cuadro sale del reloj del juego (ps_audio_due), no del tiempo real ni de AI_LEN.
     * En el N64 el hilo de audio arma ~533 muestras por retrazo y el juego casi siempre cumple sus 30 cuadros;
     * en el 3DS el juego se atrasa (cuadros pesados, cargas) y si el sonido sigue al tiempo real la musica se
     * le adelanta cada vez mas. Aqui cada cuadro del juego vale lo mismo en sonido: van juntos. */
    {
        /* reserva en la salida: lo que tarda el juego entre dos cuadros de audio (el mayor reciente) + margen */
        static OSTime sPsLastT;
        static u32 sPsMaxUs;
        const OSTime nowT = osGetTime();
        u32 us = sPsLastT ? (u32)((nowT - sPsLastT) * 8 / 375) : 0;
        u32 targetUs;
        s32 target, want, adj;
        const s32 fill = ps_audio_fill();
        sPsLastT = nowT;
        if (us > 250000) us = 250000; /* una carga no cuenta */
        if (us > sPsMaxUs) sPsMaxUs = us; else sPsMaxUs -= (sPsMaxUs - us) / 64; /* baja a la mitad en menos de 1 s (60 cuadros de sonido por segundo) */
        /* La reserva ES el retraso del sonido contra la imagen (lo que se arma ahora suena cuando se acabe lo que
         * ya esta en la salida): lo menos posible. Tiene que cubrir lo que tarda el juego en volver a este hilo. */
        targetUs = sPsMaxUs + sPsMaxUs / 4 + 35000;
        if (targetUs < 70000) targetUs = 70000;
        if (targetUs > 240000) targetUs = 240000;
        target = (s32)(((u64)targetUs * (u32)D_800FCAD4) / 1000000);
        if (fill) { target = sPsFillTarget; targetUs = gPsAudTargetMs * 1000; }
        /* los dos relojes (el del sonido del 3DS y el de los retrazos) no son identicos: una pizca (0.4 % a lo
         * mas) para que la reserva no se vaya llenando o vaciando con los minutos */
        adj = (target - samplesLeft) / 64;
        if (adj > 2) adj = 2;
        if (adj < -2) adj = -2;
        if (fill) adj = 0;
        sPsCredit += adj;
        gPsAudSync[4] += (u32)adj;
        sPsLvAvg += (samplesLeft - sPsLvAvg) / 16;
        if (gPsIntro[0] == 1 && fill) {
            /* La imagen sigue a la musica. La musica del intro sale a su ritmo (modo reserva: sin silencios ni
             * descartes para "ajustarla") y lo que se corrige es el reloj de cuadros del juego (os_events.c): cada
             * ~0.2 s se compara lo que se oye (armado - lo que espera en la salida) contra el retrazo en que deberia
             * ir la imagen, y la imagen se adelanta (cuadros sin dibujar) o espera lo que haga falta para quedar
             * PS_INTRO_LAG_MS adelante de la musica (mas el ajuste a mano).
             * Antes era al reves: la musica seguia al reloj del juego. Con el 3DS apurado (cuadros lentos, el otro
             * nucleo sin alcanzar a armar el sonido) la musica se quedaba atras con silencios, la imagen recuperaba
             * su atraso corriendo y se le adelantaba, y los toques de 34 ms no alcanzaban a juntarlas. */
            extern s32 ps_frame_target_vi256(u32 baseVi);
            extern void ps_frame_clock_shift(s32 us);
            static s32 sSum, sN;
            const s32 rate = (s32)(D_800FCAD4 ? D_800FCAD4 : 32000);
            const s32 heard = (s32)(gPsAudSync[1] - gPsIntro[2]) - samplesLeft;
            /* Lo que se ve es el cuadro que el juego ya presento (gPsGameVi), y ese va detras de donde "deberia ir"
             * por el atraso que trae en promedio (cuadros lentos). Ese promedio (lento, ~1 s) se descuenta; un atoron
             * suelto no cuenta: se recupera solo, sin mover el reloj. */
            static s32 sBias;
            const s32 t256 = ps_frame_target_vi256(gPsIntro[1]);
            const s32 img = (s32)(((s64)(t256 - sBias) * rate) / (60 * 256));
            const s32 tgt = -(rate * PS_INTRO_LAG_MS / 1000) - (s32)gPsIntro[5] * 2 * (s32)frameSize;
            static u32 sMark;
            if (sMark != gPsIntro[2]) { sMark = gPsIntro[2]; sSum = 0; sN = 0; sBias = 0; } /* otra vuelta del intro */
            {
                s32 d = t256 - (s32)(gPsGameVi - gPsIntro[1]) * 256;
                if (d > 256 * 8) d = 256 * 8;
                if (d < -256 * 2) d = -256 * 2;
                sBias += (d - sBias) / 64;
            }
            gPsIntro[3] = 0; /* los toques a mano ya no mueven la musica: corren el objetivo (gPsIntro[5]) */
            sSum += heard - img - tgt;
            if (++sN >= 12) {
                s32 us = (s32)((s64)(sSum / sN) * 1000000 / rate); /* > 0: la musica va adelante de la imagen */
                sSum = 0; sN = 0;
                if (us > 10000 || us < -10000) {
                    /* la mitad de lo medido y a lo mas 40 ms por vez (no se nota); mas de 150 ms, todo de una vez */
                    if (us < 150000 && us > -150000) { us /= 2; if (us > 40000) us = 40000; if (us < -40000) us = -40000; }
                    if (us > 1000000) us = 1000000;
                    if (us < -1000000) us = -1000000;
                    ps_frame_clock_shift(us);
                    /* para el resumen: 7 lo que la imagen se adelanto, 4 lo que espero (ms, acumulado) */
                    if (us > 0) gPsIntro[7] += (u32)(us / 1000); else gPsIntro[4] += (u32)(-us / 1000);
                }
            }
        } else if (gPsIntro[0] == 1) {
            /* (modo anterior, sonidoreloj.txt) */
            /* Mientras suena la musica del intro: que lo que se oye quede PS_INTRO_LAG_MS detras de la imagen (mas el
             * ajuste a mano). La reserva cambia durante el intro (cuadros mas o menos pesados) y con ella el
             * retraso: cada ~0.4 s, si se salio 25 ms del objetivo, un toque (34 ms) para alla. */
            static s32 sSum, sN;
            const s32 rate = (s32)(D_800FCAD4 ? D_800FCAD4 : 32000);
            const s32 lead = (s32)(gPsAudSync[1] - gPsIntro[2]) - (s32)(gPsGameVi - gPsIntro[1]) * (rate / 60);
            sSum += lead - samplesLeft;
            if (++sN >= 24) {
                const s32 des = sSum / sN;
                const s32 tgt = -(rate * PS_INTRO_LAG_MS / 1000) - (s32)gPsIntro[5] * 2 * (s32)frameSize;
                const s32 tol = rate * 25 / 1000;
                if (gPsIntro[3] == 0) {
                    if (des < tgt - tol) { gPsIntro[3] = (u32)-1; gPsIntro[7]--; }
                    else if (des > tgt + tol) { gPsIntro[3] = 1; gPsIntro[7]++; }
                }
                sSum = 0; sN = 0;
            }
        }
        if (gPsIntro[0] != 0 && gPsIntro[3] != 0) {
            /* ajuste a mano durante el intro, un cuadro del juego (34 ms) por toque. Adelantar: se arma musica de
             * mas y esa parte no se entrega. Atrasar: silencio en lugar de esa musica. La reserva queda igual. */
            const s32 n = (s32)gPsIntro[3], step = 2 * (s32)frameSize;
            if (n < 0) { sPsCredit += step; sPsSkip += step; gPsIntro[3] = (u32)(n + 1); gPsIntro[4] = (u32)((s32)gPsIntro[4] - step); }
            else {
                extern void ps_audio_pad(u32 bytes, u32 freq);
                sPsCredit -= step;
                ps_audio_pad((u32)step << 2, (u32)D_800FCAD4);
                gPsIntro[3] = (u32)(n - 1); gPsIntro[4] = (u32)((s32)gPsIntro[4] + step);
            }
        }
        {
            /* un cuadro por retrazo de sonido (1/60 s), del tamano del N64: 552 casi siempre y 368 cuando lo armado
             * se adelanta a la cuenta (en promedio 533.3). sLead3 = (armado - ideal) x 3. */
            static s32 sLead3;
            want = sLead3 >= 3 * (SAMPLES - 20) ? minFrameSize : (s32)frameSize;
            sLead3 += want * 3 - (s32)((u32)D_800FCAD4 / 20);
        }
        if (gPsBigFrames) {
            want = sPsCredit;
            if (want > maxFrameSize) want = maxFrameSize;
            if (want < minFrameSize) want = minFrameSize;
            want = ((want + SAMPLES / 2) / SAMPLES) * SAMPLES;
            if (want > maxFrameSize) want -= SAMPLES;
            if (want < minFrameSize) want = minFrameSize;
        }
        if (!fill) sPsCredit -= want;
        /* Reserva de mas = sonido tarde. Pasa cuando el juego recupera un atraso (corre un momento mas rapido que el
         * tiempo real y arma sonido de mas): ese sonido se descarta, igual que la imagen se salto esos cuadros.
         * Con mucha de mas (70 ms) se descarta ya; con poca (20 ms durante medio segundo seguido), un cuadro de sonido. */
        {
            static s32 sMinL = 0x7FFFFFFF;
            static u32 sWin, sShed;
            const s32 hard = (s32)((u32)D_800FCAD4 * 70 / 1000), soft = (s32)((u32)D_800FCAD4 * 20 / 1000);
            if (samplesLeft < sMinL) sMinL = samplesLeft;
            if (++sWin >= 30) {
                gPsAudSync[11] = (u32)sMinL;
                if (sMinL > target + soft) sShed = 1;
                sWin = 0;
                sMinL = 0x7FFFFFFF;
            }
            sPsDiscard = 0;
            if (fill) sShed = 0;
            else if (samplesLeft > target + hard) sPsDiscard = 1;
            else if (sShed) { sPsDiscard = 1; sShed = 0; }
            if (sPsDiscard) gPsAudSync[9] += (u32)want;
        }
        gPsAudSync[1] += (u32)want;
        gPsAudSync[6] += (u32)samplesLeft; gPsAudSync[7]++;
        if ((u32)samplesLeft > gPsAudSync[8]) gPsAudSync[8] = (u32)samplesLeft;
        info->frameSamples = want;
        /* La salida se vacio (el juego estuvo parado o lento y no genero sonido): silencio hasta volver a tener la
         * reserva. Asi la musica se detiene lo mismo que se detuvo el juego en vez de adelantarsele. */
        {
            /* lo que de verdad esta sonando: sin lo que el otro nucleo todavia procesa */
            extern u32 ps_audio_inflight(void);
            extern void ps_audio_pad(u32 bytes, u32 freq);
            const s32 playable = samplesLeft - (s32)(ps_audio_inflight() >> 2);
            if (!fill && playable < (s32)frameSize / 2 && samplesLeft < target) ps_audio_pad((u32)(target - samplesLeft) << 2, (u32)D_800FCAD4);
            /* Modo reserva: si la salida se vacio de verdad, medio objetivo de silencio antes de seguir. Sin eso cada
             * cuadro de sonido salia en cuanto estaba listo y se acababa antes de que llegara el siguiente (tartamudeo);
             * asi queda un solo hueco y, mientras suena el silencio, detras se rehace la reserva. */
            if (fill && playable < (s32)frameSize / 2) {
                s32 half = target / 2;
                s32 pad;
                if (half > (s32)((u32)D_800FCAD4 * 16 / 100)) half = (s32)((u32)D_800FCAD4 * 16 / 100); /* a lo mas 0.16 s (la reserva del intro es mayor) */
                pad = half - samplesLeft;
                if (pad > 0) ps_audio_pad((u32)pad << 2, (u32)D_800FCAD4);
            }
        }
        gPsAudTargetMs = targetUs / 1000;
        gPsAudSync[10] = targetUs / 1000;
    }
#else
    if ((samplesLeft >= 0x1A9) && (min_only_one != 0)) {
        info->frameSamples = minFrameSize;
        min_only_one = 0;
    } else {
        info->frameSamples = frameSize;
        min_only_one = 1;
    }

#endif

    __clearAudioDMA();

    if (D_80077DAC != 0) {
        D_800FC828 = LeoTestUnitReady(&D_800FC825);
    }

#ifdef PORT
    sPsSnapPos = 0;
    sPsSnapSlot = (u32)curAcmdList;
    cmdp = alAudioFrame(sPsACMDList[curAcmdList], &cmdLen, audioPtr, info->frameSamples);
    if ((u32)(cmdp - sPsACMDList[curAcmdList]) > gPsSnd[8]) gPsSnd[8] = (u32)(cmdp - sPsACMDList[curAcmdList]);
    /* PORT: la salida de esta tarea; el host la entrega al terminarla en el otro nucleo (blob_host.c) */
    {
        extern void ps_audio_next_out(const void* buf, u32 size, u32 freq);
        s32 skip = sPsSkip < info->frameSamples ? sPsSkip : info->frameSamples; /* ajuste a mano: el principio no se entrega */
        sPsSkip -= skip;
        if (sPsDiscard) skip = info->frameSamples;
        ps_audio_next_out((const u8*)info->data + ((u32)skip << 2), (u32)(info->frameSamples - skip) << 2, (u32)D_800FCAD4);
    }
    sp38.t.data_ptr = sPsACMDList[curAcmdList];
    sp38.t.data_size = (cmdp - sPsACMDList[curAcmdList]) * sizeof(Acmd);
#else
    cmdp = alAudioFrame(__am.ACMDList[curAcmdList], &cmdLen, audioPtr, info->frameSamples);
    sp38.t.data_ptr = __am.ACMDList[curAcmdList];
    sp38.t.data_size = (cmdp - __am.ACMDList[curAcmdList]) * sizeof(Acmd);
#endif

    sp38.t.type = M_AUDTASK;
    sp38.t.ucode_boot = &rspbootTextStart;
    sp38.t.ucode_boot_size = (s32)_binary_assets_us_F3DEX2_bin_start - (s32)rspbootTextStart;
    sp38.t.flags = 0;
    sp38.t.ucode = &aspMainTextStart;
    sp38.t.ucode_data = &aspMainDataStart;
    sp38.t.ucode_data_size = SP_UCODE_DATA_SIZE;
    sp38.t.dram_stack = NULL;
    sp38.t.dram_stack_size = 0;
    sp38.t.output_buff = NULL;
    sp38.t.output_buff_size = 0;
    sp38.t.yield_data_ptr = NULL;
    sp38.t.yield_data_size = 0;

#ifdef PORT
    curAcmdList = (curAcmdList + 1) % PS_AUDQ;
#else
    curAcmdList ^= 1;
#endif
#ifdef PORT
    lastInfo = sPsInfo[audFrameCt % PS_AUDQ];
#else
    lastInfo = __am.audioInfo[audFrameCt % 3];
#endif
    audFrameCt++;

    if (D_80077D9C[0] != 0) {
        func_8003D32C();
    }

    if (D_80077D90[0] != 0) {
        func_800429D0();
        if (lastInfo != NULL) {
            func_800497E0(lastInfo->data, 0, ((audFrameCt % 3) == 0) ? 0x216 : 0x215, 0);
        }
    }

    if (D_80077D98[0] != 0) {
        func_80044EA4();
    }

    func_800416BC();

    if (mus_active_fade_handle != 0) {
        mus_fade_start_volume += mus_fade_step_rate;
        if (mus_fade_step_rate > 0.0f) {
            if (mus_fade_target_volume <= mus_fade_start_volume) {
                mus_fade_start_volume = mus_fade_target_volume;
            }
        } else {
            if (mus_fade_start_volume <= mus_fade_target_volume) {
                mus_fade_start_volume = mus_fade_target_volume;
            }
        }

        MusSetVolumeScale(mus_active_fade_handle, (u32)mus_fade_start_volume & 0xFF);

        if (mus_fade_start_volume == mus_fade_target_volume) {
            mus_active_fade_handle = 0;
        }
    }

    if (D_80077DD4 != 0.0f) {
        D_80077DD4 -= D_80077DD8;
        if (D_80077DD4 <= 0) {
            D_80077DD4 = 0.0f;
            MusSetAllChannelsDuration(2, 1);
        }
        MusSetMasterVolume(2, D_80077DD4);
    }

    if (D_80077DDC != 0.0f) {
        D_80077DDC -= D_80077DE0;
        if (D_80077DDC <= 0) {
            D_80077DDC = 0.0f;
            MusSetAllChannelsDuration(1, 1);
        }
        MusSetMasterVolume(1, D_80077DDC);
    }

    if (D_80077DA8 != 0) {
        sp38.t.data_size = 0;
    }

    *arg0 = sp38;

    return arg0;
}

ALDMAproc __amDmaNew(AMDMAState** state) {
    if (!dmaState.initialized) {
        dmaState.firstUsed = NULL;
        dmaState.firstFree = &dmaBuffs[0];
        dmaState.initialized = TRUE;
    }

    *state = &dmaState;

    return __amDMA;
}

#ifdef PORT
#define FRAME_LAG PS_AUDQ /* PORT: las tareas de audio pendientes leen estos buffers */
#else
#define FRAME_LAG 1
#endif

void __clearAudioDMA(void) {
    s32 i;
    void* sp40;
    AMDMABuffer* var_s0_2;
    ALLink* node;

    for (i = 0; i < nextDMA; i++) {
        osRecvMesg(&audDMAMessageQ, &sp40, 0);
    }

    var_s0_2 = dmaState.firstUsed;
    while (var_s0_2 != NULL) {
        node = var_s0_2->node.next;

        if ((var_s0_2->lastFrame + FRAME_LAG) < audFrameCt) {
            if (var_s0_2 == dmaState.firstUsed) {
                dmaState.firstUsed = var_s0_2->node.next;
            }

            alUnlink(var_s0_2);

            if (dmaState.firstFree != NULL) {
                alLink(var_s0_2, dmaState.firstFree);
            } else {
                dmaState.firstFree = var_s0_2;
                var_s0_2->node.next = NULL;
                var_s0_2->node.prev = NULL;
            }
        }

        var_s0_2 = node;
    }

    nextDMA = 0;
}


void func_8003D264(s32 arg0) {
    const f32 D_8007C7C0 = 0.8000000119f;
    if (arg0 != 0) {
        func_800498A8(0x7D00, 0x78, 0x640);
        D_80077D90[0] = 1;
        D_80077D94 = D_8007C7C0;
        return;
    }
    D_80077D90[0] = 0;
}

void func_8003D2B8(s32 arg0) {
    if (D_80077D90[0] == 0) {
        func_800498A8(0x7D00, 0x78, 0x320);
        D_80077D90[0] = 1;
        D_80077D9C[0] = 1;
        D_80077D94 = 0.0f;
        D_80077DA0 = arg0;
        D_80077DA4 = 0;
        D_800FC824 = 0;
    }
}

void func_8003D32C(void) {
    switch (D_80077D9C[0]) {
        case 1:
            MusSetupVolumeFade(D_80078E70, 0x50, 0x18, 3);
            func_80042AB0(D_80077DA0);
            D_80077D9C[0]++;
            D_80077DA4 = 0;
            break;

        case 2:
            D_80077DA4++;
            if (D_80077DA4 >= 2) {
                D_80077DA4 = 0;
                D_80077D9C[0]++;
                D_800FC824 = 1;
            }
            break;

        case 3:
            if (func_800449F8() == 0) {
                D_80077D9C[0]++;
                D_80077DA4 = 0;
            }
            break;

        case 4:
            if (D_80077DA4 == 0) {
                MusSetupVolumeFade(D_80078E70, 0x18, 0x50, 0x14);
            }

            if (D_80077DA4 >= 0x1F) {
                D_80077D9C[0]++;
            }

            D_80077DA4++;
            break;

        case 5:
            D_80077D9C[0] = 0;
            D_80077D90[0] = 0;
            D_800FC824 = 0;
            break;
    }
}

s32 func_8003D494(void) {
    return D_80077D90[0];
}

s32 func_8003D4A0(s32 arg0) {
    if (arg0 == 0) {
        if (mus_free_voices_count != D_800FCAD8) {
            return 0;
        }
        D_80077DA8 = 1;
    } else {
        osAiSetFrequency(D_800FCAD4);
        lastInfo = NULL;
        dmaState.initialized = 0;
        D_80077DA8 = 0;
    }
    return 1;
}

s32 func_8003D514(u8* arg0) {
    if (D_80077DAC == 0) {
        *arg0 = 7;
        return 8;
    }
    *arg0 = D_800FC825;
    return D_800FC828;
}

void func_8003D54C(s32 arg0) {
    if (arg0 == 1) {
        D_80077DAC = 1;
        return;
    }
    D_80077DAC = 0;
}

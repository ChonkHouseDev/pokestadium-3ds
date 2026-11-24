#include "global.h"
#include "6A40.h"

typedef struct UnkStruct800A7428 {
    u32* unk0;
    u32* unk4;
    u32 unk8;
    s32 unkC;
    s16 unk10;
} UnkStruct800A7428; // size = 0x12

Gfx* gDisplayListHead;
UnkStruct800A7428 D_800A7428;
unk_D_800A7440 D_800A7440;

void func_80005EDC(void);

#ifdef PORT
/* PORT: lo que quedo libre en el bufer de la lista de dibujo al cerrar cada cuadro (minimo desde que se armo) y su
   tamano. La lista crece desde el inicio y las matrices/luces del cuadro desde el final; si se cruzan, la lista se
   come los datos (o al reves) y el interprete termina leyendo vertices como comandos. */
#ifdef PORT
/* PORT: la hora del 3DS (la escribe el host cada cuadro; -1 si no hay) y un fondo forzado para probar el mapa */
s32 gPsHour3DS = -1;
/* PORT: hora del dia escogida en Options: 0 Clock (la del 3DS), 1 Day, 2 Sunset, 3 Night (el host la guarda en la SD) */
s32 gPsTimeMode = 0;
/* PORT: cuenta los retos terminados (copa del Stadium, gimnasio del castillo): el host suelta el equipo del reto */
u32 gPsChalEnd = 0;
s32 gPsBgTest3DS = 0;
/* PORT: pantalla gigante del estadio en la batalla: el host copia aqui la pantalla de abajo
 * (toda la pantalla, 320x240 RGBA16 en 10x8 bloques de 32x32, uno tras otro) y pone gPsJumboOn = 1 */
u16 gPsJumboTex[80 * 32 * 32] __attribute__((aligned(16)));
u32 gPsJumboOn = 0;
u32 gPsJumboDbg[2] = { 0, 0 }; /* llamadas y veces que encontro la camara (para revisar en un volcado) */
#endif
s32 gPsDynFree = 0x7FFFFFFF;
u32 gPsDynSize;
#endif
void func_80005E40(u32 arg0, s32 arg1) {
#ifdef PORT
    gPsDynSize = arg0;
    gPsDynFree = 0x7FFFFFFF;
#endif
    D_800A7428.unk10 = 0;
    D_800A7428.unkC = arg0;
    D_800A7428.unk0 = main_pool_alloc_with_func(arg0, arg1, 'DYN0', NULL);
    D_800A7428.unk4 = main_pool_alloc_with_func(arg0, arg1, 'DYN1', NULL);
    func_80005EDC();
}

void func_80005EAC(void) {
    main_pool_try_free(D_800A7428.unk4);
    main_pool_try_free(D_800A7428.unk0);
}

void func_80005EDC(void) {
    s32* temp_v0 = (s32*)&D_800A7428;

#ifdef PORT
    if (gDisplayListHead != NULL && D_800A7428.unk8 != 0) {
        s32 left = (s32)(D_800A7428.unk8 - (uintptr_t)gDisplayListHead);
        if (left < gPsDynFree) gPsDynFree = left;
    }
#endif
    D_800A7428.unk10 ^= 1;
    temp_v0 += D_800A7428.unk10;

    D_800A7428.unk8 = *temp_v0 + D_800A7428.unkC;
    gDisplayListHead = *temp_v0;
}

void func_80005F1C(Gfx** arg0, Gfx** arg1) {
    s32* ptr = (s32*)&D_800A7428;
    *arg1 = (Gfx*)((uintptr_t)gDisplayListHead - ptr[D_800A7428.unk10]);
    *arg0 = (Gfx*)(ptr[D_800A7428.unk10]);
}

void* func_80005F5C(s32 arg0) {
    u32 temp_v0;
    u32 ret;

    arg0 = ALIGN8(arg0);
    temp_v0 = (D_800A7428.unk8 - arg0);
    ret = NULL;
    if (temp_v0 >= (uintptr_t)gDisplayListHead) {
        D_800A7428.unk8 = temp_v0;
        ret = temp_v0;
    }
    return ret;
}

s32 func_80005F9C(void) {
    return D_800A7428.unk8 - (uintptr_t)gDisplayListHead;
}

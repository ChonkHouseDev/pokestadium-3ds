#ifndef PS_JP_H
#define PS_JP_H
/* Zona fija del bloque para cada fragment de la ROM japonesa que usa la escena (tools/port/gen_jp_scene.py) */
/* nvram: la direccion del N64 invertida (~), ver gen_jp_scene.py */
typedef struct { unsigned int nvram, romStart, romEnd, size; unsigned char* mem; } PsJpImage;
#define PS_JP_VRAM(x) (~(x).nvram)
extern const PsJpImage gPsJpImages[];
extern const unsigned int gPsJpImagesCount;
/* Direccion del juego japones -> simbolo nativo equivalente */
typedef struct { unsigned int nvram; char* native; } PsJpSym;
extern const PsJpSym gPsJpSyms[];
extern const unsigned int gPsJpSymsCount;
#endif

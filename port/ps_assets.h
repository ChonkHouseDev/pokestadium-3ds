#ifndef PS_ASSETS_H
#define PS_ASSETS_H
/* Assets del juego servidos desde pokestadium.o2r (generado por Torch a partir de la ROM). */

enum { PS_ASSET_RAW = 0, PS_ASSET_PERS_SZP = 1 };

typedef struct {
    unsigned int rom;      /* offset en la ROM del N64 */
    unsigned int size;     /* tamano en la ROM */
    unsigned char type;    /* PS_ASSET_* */
    const char* path;      /* recurso dentro del .o2r */
} PsAssetEntry;

extern const PsAssetEntry gPsAssetIndex[];
extern const unsigned int gPsAssetIndexCount;

/* Contenido de un recurso Blob del .o2r (lo da la plataforma: libultraship, lector de .o2r, etc.) */
typedef const void* (*PsResGetFn)(const char* path, unsigned int* size);
extern PsResGetFn gPsResGet;

const PsAssetEntry* ps_asset_at(unsigned int rom);              /* entrada que empieza en rom */
const PsAssetEntry* ps_asset_containing(unsigned int rom);      /* entrada cuyo rango contiene rom */
unsigned int ps_rom_read(unsigned int rom, void* dst, unsigned int size);

#endif

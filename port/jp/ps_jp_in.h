#ifndef PS_JP_IN_H
#define PS_JP_IN_H
/* Lo que el juego le pasa a la escena japonesa, sin depender de las estructuras de ninguno de los dos juegos */
typedef struct {
    char name[16], title[16];
    unsigned char trainerIndex, count;
    const void* mon[6];        /* el Pokemon en el formato del juego de EE. UU. (para su icono) */
    unsigned char head[6][0x30]; /* sus primeros 0x30 bytes: especie, nivel, estadisticas (iguales en los dos juegos) */
    char nick[6][13];
} PsJpSideIn;
typedef struct { int mode, ruleCategory, modeSelection, round; PsJpSideIn side[2]; } PsJpIn;
#endif

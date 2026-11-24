// F2: dibujo de modelos con huesos desde buffers residentes, con la transformacion, la luz, las coordenadas de
// textura y la niebla en el PICA200 (ver ps_resident_interp.inc y ps_resident_backend.inc).
//
// El lote ya esta en la cache de modelos (F1). Su copia para el GPU vive en memoria lineal y es del mismo dueno:
// se libera cuando la cache suelta el lote. Por cuadro el CPU solo calcula las matrices de los huesos y el estado
// del material; no se recalcula ni se copia ningun vertice.
#pragma once
#include <cstdint>

// Vertice residente (16 bytes): posicion de objeto y fila del hueso (lugar * 6) en w; U,V del N64 ya escalados
// por G_TEXTURE (como los calcula GfxSpVertex); color o normal + alfa tal cual del F3DVtx.
struct PsResidentVertex3DS {
    int16_t x, y, z, boneRow;
    int16_t u, v;
    uint8_t c[4];
};
static_assert(sizeof(PsResidentVertex3DS) == 16, "PsResidentVertex3DS");

// Limites del camino residente dentro del programa de vertices (96 registros de flotantes): reusa skinBones (66
// filas), staticUv, staticColor/Mask, nativeMatrix[0..2], skinLightColor y skinFogParams; agrega 1 booleano.
static constexpr int kPsResMaxBones3DS = 11; // por dibujo (66 filas de skinBones / 6)

struct PsResidentDraw3DS {
    const PsResidentVertex3DS* vertices = nullptr;
    const uint16_t* indices = nullptr;
    uint32_t indexCount = 0;
    // un vertice del lote en el formato del interprete: de ahi salen las constantes del combinador, los recortes,
    // el color de la niebla y la escala de grises, igual que en el camino anterior
    const float* materialVertex = nullptr;
    int varyingInput = 0;
    int nBones = 0;
    float bones[kPsResMaxBones3DS][6][4];      // por hueso: 4 filas de MV*P (columnas) + direccion de 2 luces (solo nBones)
    float uvK[4] = {}, uvO[4] = {}, uvC[4] = {}, uvM[4] = {}, uvA[4] = {}; // texturas 0 (xy) y 1 (zw)
    float color[4] = {}, colorMask[4] = {}; // entrada variable: sombra * mascara + constante (unidades 0-255)
    bool lit = false;
    float lightColor[3][4] = {}; // difusa 1, difusa 2, ambiente (0-255)
    bool fogVtx = false, fogAlpha = false;
    float fogParams[4] = {}; // fog_mul, fog_offset, coordenada = alfa * z + w (sin niebla por vertice)
    int cull = 0;            // 0 ninguno, 1 frente, 2 atras
};

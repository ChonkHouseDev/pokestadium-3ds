#pragma once
#include "model_cache.h"
// Conversion de display lists del juego (big-endian) a display lists nativas para libultraship.
#include <cstddef>
#include <cstdint>
#include <vector>

struct GfxConvertConfig {
    bool (*isBlobPtr)(uint32_t addr);      // la direccion es memoria del juego
    uint32_t (*n64ToBlob)(uint32_t addr);  // direccion KSEG0/KSEG1 del N64 -> direccion del juego (0 si no aplica)
    const uint8_t* (*toHost)(uint32_t addr); // direccion del juego -> apuntador legible (nullptr = identidad)
};

struct GfxConvertStats {
    uint32_t commands;
    uint32_t vertices;
    uint32_t errors;
    uint32_t bytes;
};

void GfxConvertInit(const GfxConvertConfig& cfg, size_t arenaBytes);
// Convierte la display list del frame; regresa la direccion de la lista nativa (0 si fallo)
uint32_t GfxConvertFrame(uint32_t dlAddr, GfxConvertStats* stats);
// Direcciones de texturas (G_SETTIMG) usadas en el ultimo frame convertido
const std::vector<const uint8_t*>& GfxConvertTextures();
// Ancho del framebuffer del ultimo frame (G_SETCIMG), 0 si no hubo
uint32_t GfxConvertFramebufferWidth();
// El ultimo frame dibujo a una textura (no se debe saltar)
bool GfxConvertSawOffscreen();
// Modelos con huesos convertidos a G_PS_SKIN desde la ultima llamada
void GfxConvertMeshStats(uint32_t* out);
// Recorrido rapido sin convertir: true si el cuadro dibuja a una textura fuera de pantalla
bool GfxConvertScanOffscreen(uint32_t dlAddr, uint32_t* fbWidth);
// Comandos que recorrio el ultimo GfxConvertScanOffscreen (sin contar si encontro una imagen aparte)
uint32_t GfxConvertLastScanCount();
uint32_t GfxConvertLastScanTris();
bool GfxConvertLastScanCovers(); // rectangulos opacos tapan toda la pantalla (cuadro 2D completo)
extern uint32_t gGfxScanDbg[5];
bool GfxConvertLastScanClears(); // el ultimo cuadro recorrido rellena toda la pantalla (se puede saltar)
void GfxConvertSkinStats(uint32_t* batches, uint32_t* tris);
// Aviso de memoria del juego (PS_MEM_* de port/os/ps_os.h): invalida los modelos horneados que toque
void GfxConvertMemEvent(uint32_t kind, uint32_t addr, uint32_t size);
// Cache de modelos horneados (model_cache.h). Presupuesto 0 = apagada (camino de siempre)
void GfxConvertModelCacheConfig(uint32_t budgetBytes, bool verify);
mc::Stats GfxConvertModelCacheStats();
extern bool gGfxConvertSkin;
extern bool gGfxConvertGpuFb;
extern uint32_t gGfxConvertDbgDrop;
extern bool gGfxConvertHud, gGfxConvertHudDbg; // capa del HUD (G_PS_HUD)
extern uint32_t gGfxConvertHudStats[4], gGfxConvertHudWhy;
extern bool gGfxConvertF2; // F2: lotes de la cache como PSK2 (buffers residentes)
// Lo que el RDP por software dibujo fuera de pantalla en el ultimo frame
void GfxConvertSoftStats(uint32_t* rects, uint32_t* fills, uint32_t* tris);

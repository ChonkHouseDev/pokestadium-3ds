// Caja negra (compilacion PS_BLACKBOX=1): registro en RAM de lo que se le manda al GPU, con validacion de cada
// direccion fisica que el PICA200 va a leer o escribir. Se engancha a libctru con --wrap (GX_ProcessCommandList,
// GX_DisplayTransfer, GX_TextureCopy, GX_MemoryFill): el renderizador no cambia.
#pragma once
#include <cstdint>

enum BoxEvKind : uint8_t { BOX_LIST = 1, BOX_XFER, BOX_COPY, BOX_FILL };
struct BoxEvent {
    uint32_t seq, frame, tick;  // tick: svcGetSystemTick() >> 18 (~1 ms)
    uint8_t kind, bad, pad[2];
    uint32_t a, b, c, d;        // por tipo: lista (direccion, palabras, dibujos, -); transferencias (origen, destino, dims, banderas)
    uint32_t badReg, badVal;    // primer problema encontrado (registro del PICA o 0xFFFF para argumentos)
};
static constexpr int kBoxEvents = 32;
struct BoxState {
    BoxEvent ev[kBoxEvents];
    volatile uint32_t seq;                  // ultimo evento escrito
    volatile uint32_t lists, xfers, copies, fills, draws;
    volatile uint32_t bad, badNan;          // direcciones invalidas, uniformes NaN
    volatile uint32_t lastListTick, maxListWords;
    BoxEvent lastBad;
    volatile uint32_t nanReg, nanVal, nanMode; // ultimo uniforme NaN: registro c#, bits, modo (1 = f32)
};
extern BoxState gBox;
extern volatile uint32_t gBoxFrame; // lo pone el host en cada cuadro

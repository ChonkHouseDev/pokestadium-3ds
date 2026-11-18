// Visor de frames: toma un volcado del juego en big-endian (host_trace, PS_DUMP) y lo dibuja con el
// mismo convertidor de display lists del 3DS + el interprete Fast3D de libultraship + un rasterizador
// por software. Sirve para ver lo que saldria en la consola sin tenerla.
// Uso: ps_viewer frame.psfd salida.ppm
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#include "sw_backend.h"
#include "fast/interpreter.h"
#include "fast/debug/GfxDebugger.h"
#include "gfx_convert.h"

extern "C" {
#include "libultraship/libultra/gbi.h"
}

namespace Fast {
void GfxSetInstance(std::shared_ptr<Interpreter> interpreter);
}

static std::vector<uint8_t> sMem;
static uint32_t sBase;

static bool isBlob(uint32_t a) { return a >= sBase && a - sBase < sMem.size(); }
static uint32_t n64ToBlob(uint32_t) { return 0; }
static const uint8_t* toHost(uint32_t a) { return isBlob(a) ? sMem.data() + (a - sBase) : nullptr; }

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "uso: %s frame.psfd salida.ppm\n", argv[0]); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    uint8_t hdr[16];
    fread(hdr, 1, 16, f);
    if (be32(hdr) != 0x50534644u) { fprintf(stderr, "no es un volcado\n"); return 1; }
    sBase = be32(hdr + 4);
    uint32_t size = be32(hdr + 8), dl = be32(hdr + 12);
    sMem.resize(size);
    fread(sMem.data(), 1, size, f);
    fclose(f);

    Fast::GfxWindowBackendNull window;
    Fast::GfxRenderingAPISoft renderer(320, 240);
    auto interp = std::make_shared<Fast::Interpreter>();
    Fast::GfxSetInstance(interp);
    interp->SetGfxDebugger(std::make_shared<Fast::GfxDebugger>());
    interp->Init(&window, &renderer, "Pokemon Stadium (visor)", false, 320, 240, 0, 0);
    Fast::gfx_set_target_ucode(ucode_f3dex2);

    GfxConvertInit(GfxConvertConfig{ isBlob, n64ToBlob, toHost }, 4 * 1024 * 1024);
    GfxConvertStats st = {};
    uint32_t nativeDl = GfxConvertFrame(dl, &st);
    printf("dl %08x -> %08x: %u comandos, %u vertices, %u errores\n", dl, nativeDl, st.commands, st.vertices, st.errors);
    if (GfxConvertFramebufferWidth() == 640) interp->SetNativeDimensions(640, 480);
    if (nativeDl) {
        std::unordered_map<Mtx*, MtxF> none;
        interp->StartFrame();
        interp->Run(reinterpret_cast<::Gfx*>(nativeDl), none);
        interp->EndFrame();
    }
    printf("triangulos dibujados: %u\n", renderer.drawnTriangles);
    renderer.SavePng(argv[2]);
    return 0;
}

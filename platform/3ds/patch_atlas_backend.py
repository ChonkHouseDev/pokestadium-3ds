#!/usr/bin/env python3
"""Atlas de texturas en el backend citro3d (Stadium).

Las texturas chicas con modo clamp (letras del menu, piezas del estadio) se suben a paginas compartidas de
512x512. Cada textura ocupa una celda con 8 texeles de margen que repiten el borde (clamp del N64), asi
el filtro bilineal y la poca precision del GPU en una pagina grande no toman texeles de la vecina. Con varias texturas en la misma pagina
el interprete ya no tiene que vaciar su lote en cada cambio: las coordenadas se pasan a coordenadas de pagina en
el CPU (PsAtlasXform3DS) y la escala del shader queda en 1.

Uso: patch_atlas_backend.py gfx_citro3d_stadium.cpp   (modifica en el lugar)
"""
import sys

p = sys.argv[1]
s = open(p).read()


def rep(old, new, count=1):
    global s
    if s.count(old) < count:
        sys.exit("patch_atlas_backend: no encontre: " + old[:80])
    s = s.replace(old, new, count)


# celda del atlas por textura
rep("""        uint16_t sourceHeight = 0;
        size_t allocatedBytes = 0;
    };""", """        uint16_t sourceHeight = 0;
        size_t allocatedBytes = 0;
        int8_t psAtlasPage = -1;
        uint16_t psCellX = 0, psCellY = 0, psCellW = 0, psCellH = 0, psPotW = 0, psPotH = 0;
    };""")

# paginas y asignador (antes de la clase)
rep("struct GfxRenderingAPICitro3D::Impl {", r'''namespace {
constexpr int kPsAtlasPages = 40;
constexpr uint32_t kPsAtlasSmall = 128;  // texturas de hasta 32: pagina chica (mas precision al muestrear)
constexpr uint32_t kPsAtlasBig = 512;    // texturas de 64 (estadio)
constexpr uint32_t kPsAtlasMaxPot = 64;
constexpr uint32_t kPsAtlasGutter = 2;
struct PsAtlasShelf { uint16_t y, h, x; };
struct PsAtlasFree { int8_t page; uint16_t x, y, w, h; uint32_t frame; };
struct PsAtlasPage {
    C3D_Tex tex = {};
    bool ready = false, linear = false;
    uint16_t size = 0;
    uint16_t top = 0;
    std::vector<PsAtlasShelf> shelves;
};
PsAtlasPage sPsAtlas[kPsAtlasPages];
std::vector<PsAtlasFree> sPsAtlasFree;
uint32_t sPsAtlasFrame = 0;
} // namespace
extern "C" {
bool gPsAtlasEnabled3DS = true;
bool gPsAtlasPointOk3DS = true;
uint32_t gPsAtlasOnlyW3DS = 0, gPsAtlasOnlyH3DS = 0;
bool gPsAtlasDbg3DS = false; // pruebas: solo este tamano   // tambien texturas con filtro de punto
bool gPsAtlasNext3DS = false;       // la siguiente UploadTexture puede ir al atlas
bool gPsAtlasNextLinear3DS = false; // con filtro bilineal
uint32_t gPsAtlasStats3DS[4];       // subidas al atlas, sueltas elegibles sin lugar, paginas, celdas libres
}

static bool PsAtlasAlloc3DS(bool linear, uint16_t w, uint16_t h, int8_t& page, uint16_t& x, uint16_t& y) {
    const uint16_t want = (w > 32 + 2 * kPsAtlasGutter || h > 32 + 2 * kPsAtlasGutter) ? kPsAtlasBig : kPsAtlasSmall;
    for (size_t i = 0; i < sPsAtlasFree.size(); ++i) {
        const auto& f = sPsAtlasFree[i];
        if (f.w == w && f.h == h && sPsAtlas[f.page].linear == linear && sPsAtlas[f.page].size == want && sPsAtlasFrame - f.frame >= 3) {
            page = f.page; x = f.x; y = f.y;
            sPsAtlasFree[i] = sPsAtlasFree.back();
            sPsAtlasFree.pop_back();
            return true;
        }
    }
    for (int pg = 0; pg < kPsAtlasPages; ++pg) {
        auto& P = sPsAtlas[pg];
        if (!P.ready) {
            if (!C3D_TexInit(&P.tex, want, want, GPU_RGBA8)) return false;
            std::memset(P.tex.data, 0, size_t(want) * want * 4);
            P.size = want;
            P.ready = true;
            P.linear = linear;
            P.top = 0;
            P.shelves.clear();
            const GPU_TEXTURE_FILTER_PARAM f = linear ? GPU_LINEAR : GPU_NEAREST;
            C3D_TexSetFilter(&P.tex, f, f);
            C3D_TexSetWrap(&P.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
            gPsAtlasStats3DS[2]++;
        }
        if (P.linear != linear || P.size != want) continue;
        for (auto& sh : P.shelves) {
            if (sh.h == h && sh.x + w <= P.size) {
                page = (int8_t)pg; x = sh.x; y = sh.y;
                sh.x += w;
                return true;
            }
        }
        if (P.top + h <= P.size) {
            P.shelves.push_back({ P.top, h, w });
            page = (int8_t)pg; x = 0; y = P.top;
            P.top += h;
            return true;
        }
    }
    return false;
}

static void PsAtlasRelease3DS(int8_t page, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    sPsAtlasFree.push_back({ page, x, y, w, h, sPsAtlasFrame });
}
extern "C" void PsLog3DS(const char*, ...);
struct PsAtlasXf { float xf[4]; int page; };
static std::vector<PsAtlasXf> sPsAtlasXf;
static void PsAtlasSetXf3DS(uint32_t id, int page, float a, float b, float c, float d) {
    if (id >= sPsAtlasXf.size()) sPsAtlasXf.resize(id + 64, PsAtlasXf{ {}, -1 });
    sPsAtlasXf[id] = PsAtlasXf{ { a, b, c, d }, page };
}
// u_pagina = u_norm * xf0 + xf1, v_pagina = v_norm * xf2 + xf3; page = pagina*2 + bilineal
extern "C" bool PsAtlasXform3DS(uint32_t id, float* xf, int* page) {
    if (id >= sPsAtlasXf.size() || sPsAtlasXf[id].page < 0) return false;
    const auto& e = sPsAtlasXf[id];
    xf[0] = e.xf[0]; xf[1] = e.xf[1]; xf[2] = e.xf[2]; xf[3] = e.xf[3];
    *page = e.page;
    return true;
}

struct GfxRenderingAPICitro3D::Impl {''')

# soltar la celda en vez de borrar la pagina
rep("""    auto& slot = mImpl->textures[textureId];
    if (slot.initialized) {
        C3D_TexDelete(&slot.texture);
        slot.initialized = false;
        slot.allocatedBytes = 0;
    }
}""", """    auto& slot = mImpl->textures[textureId];
    if (slot.psAtlasPage >= 0) {
        PsAtlasSetXf3DS(textureId, -1, 0, 0, 0, 0);
        PsAtlasRelease3DS(slot.psAtlasPage, slot.psCellX, slot.psCellY, slot.psCellW, slot.psCellH);
        slot.psAtlasPage = -1;
        slot.texture = {};
        slot.initialized = false;
        slot.allocatedBytes = 0;
        return;
    }
    if (slot.initialized) {
        C3D_TexDelete(&slot.texture);
        slot.initialized = false;
        slot.allocatedBytes = 0;
    }
}""")
rep("""        for (auto& slot : textures) {
            if (slot.initialized) {
                C3D_TexDelete(&slot.texture);""", """        for (auto& slot : textures) {
            if (slot.initialized && slot.psAtlasPage < 0) {
                C3D_TexDelete(&slot.texture);""")
rep("""    if(!slot.initialized || !slot.texture.data || slot.texture.width!=bw || slot.texture.height!=bh || slot.texture.fmt!=GPU_RGBA8) {
        if(slot.initialized)C3D_TexDelete(&slot.texture);""", """    if(slot.psAtlasPage>=0){PsAtlasSetXf3DS(id,-1,0,0,0,0);PsAtlasRelease3DS(slot.psAtlasPage,slot.psCellX,slot.psCellY,slot.psCellW,slot.psCellH);slot.psAtlasPage=-1;slot.texture={};slot.initialized=false;}
    if(!slot.initialized || !slot.texture.data || slot.texture.width!=bw || slot.texture.height!=bh || slot.texture.fmt!=GPU_RGBA8) {
        if(slot.initialized)C3D_TexDelete(&slot.texture);""")

# subida: al atlas si se puede
rep("""    const uint16_t textureWidth = NextPowerOfTwo(width);
    const uint16_t textureHeight = NextPowerOfTwo(height);
    auto& slot = mImpl->textures[textureId];
    // Fast3D recycles LRU texture IDs heavily""", """    const uint16_t textureWidth = NextPowerOfTwo(width);
    const uint16_t textureHeight = NextPowerOfTwo(height);
    auto& slot = mImpl->textures[textureId];
    {
        const bool want = gPsAtlasEnabled3DS && gPsAtlasNext3DS && (gPsAtlasNextLinear3DS || gPsAtlasPointOk3DS) && textureWidth >= 8 && textureHeight >= 8 &&
                          textureWidth <= kPsAtlasMaxPot && textureHeight <= kPsAtlasMaxPot &&
                          (gPsAtlasOnlyW3DS == 0 || (width == gPsAtlasOnlyW3DS && height == gPsAtlasOnlyH3DS));
        gPsAtlasNext3DS = false;
        const uint16_t cw = textureWidth + 2 * kPsAtlasGutter, ch = textureHeight + 2 * kPsAtlasGutter;
        if (slot.psAtlasPage >= 0 && !(want && slot.psCellW == cw && slot.psCellH == ch &&
                                        sPsAtlas[slot.psAtlasPage].linear == gPsAtlasNextLinear3DS)) {
            PsAtlasSetXf3DS(textureId, -1, 0, 0, 0, 0);
            PsAtlasRelease3DS(slot.psAtlasPage, slot.psCellX, slot.psCellY, slot.psCellW, slot.psCellH);
            slot.psAtlasPage = -1;
            slot.texture = {};
            slot.initialized = false;
            slot.allocatedBytes = 0;
        }
        int8_t page = slot.psAtlasPage;
        uint16_t cx = slot.psCellX, cy = slot.psCellY;
        if (want && page < 0) {
            if (!PsAtlasAlloc3DS(gPsAtlasNextLinear3DS, cw, ch, page, cx, cy)) { page = -1; gPsAtlasStats3DS[1]++; }
        }
        if (want && page >= 0) {
            if (slot.initialized && slot.psAtlasPage < 0) C3D_TexDelete(&slot.texture);
            auto& P = sPsAtlas[page];
            uint32_t* pagePixels = static_cast<uint32_t*>(P.tex.data);
            const uint32_t tilesPerRow = P.size / 8U;
            uint8_t combinedAlpha = 0xFF;
            for (uint32_t my = 0; my < ch; ++my) {
                // fila de memoria de la textura suelta equivalente (con repeticion en el margen)
                // margen: repite el borde (clamp del N64); el GPU pierde precision al muestrear la pagina grande
                const uint32_t m = my < kPsAtlasGutter ? 0U : (my - kPsAtlasGutter >= textureHeight ? textureHeight - 1U : my - kPsAtlasGutter);
                const uint32_t sourceRow = SourceRowForBackingRow(textureHeight, height, m);
                const uint32_t py = cy + my;
                for (uint32_t mx = 0; mx < cw; ++mx) {
                    const uint32_t c = mx < kPsAtlasGutter ? 0U : (mx - kPsAtlasGutter >= textureWidth ? textureWidth - 1U : mx - kPsAtlasGutter);
                    const uint32_t sourceColumn = c % width;
                    const uint8_t* sourcePixel = rgba32Buf + (static_cast<size_t>(sourceRow) * width + sourceColumn) * 4;
                    if (mx >= kPsAtlasGutter && my >= kPsAtlasGutter && mx < kPsAtlasGutter + textureWidth && my < kPsAtlasGutter + textureHeight) combinedAlpha &= sourcePixel[3];
                    uint32_t pixel;
                    std::memcpy(&pixel, sourcePixel, sizeof(pixel));
                    const uint32_t px = cx + mx;
                    pagePixels[(static_cast<size_t>(py / 8U) * tilesPerRow + px / 8U) * 64U + MortonOffset8x8(px & 7U, py & 7U)] =
                        __builtin_bswap32(pixel);
                }
            }
            if (width == 16 && height == 12 && gPsAtlasDbg3DS) {
                for (uint32_t r = 0; r < height; ++r) {
                    char line[80]; int o = 0;
                    for (uint32_t q = 0; q < width; ++q) o += snprintf(line + o, sizeof line - o, "%x", rgba32Buf[(r * width + q) * 4 + 3] >> 4);
                    PsLog3DS("[atl] id %u src %2u %s\\n", textureId, r, line);
                }
                for (uint32_t my = 0; my < ch; ++my) {
                    char line[80]; int o = 0;
                    for (uint32_t mx = 0; mx < cw; ++mx) {
                        const uint32_t px = cx + mx, py = cy + my;
                        uint32_t v = pagePixels[(static_cast<size_t>(py / 8U) * tilesPerRow + px / 8U) * 64U + MortonOffset8x8(px & 7U, py & 7U)];
                        o += snprintf(line + o, sizeof line - o, "%x", (v & 0xFF) >> 4);
                    }
                    PsLog3DS("[atl] id %u mem %2u %s\\n", textureId, my, line);
                }
            }
            const size_t rowBytes = static_cast<size_t>(tilesPerRow) * 64U * 4U;
            Mk64System3DSCleanDataCache(static_cast<uint8_t*>(P.tex.data) + (cy / 8U) * rowBytes, ((cy + ch + 7U) / 8U - cy / 8U) * rowBytes);
            slot.texture = P.tex; // misma memoria; el filtro de la copia sigue al de la pagina
            slot.initialized = true;
            slot.allocatedBytes = 0;
            slot.hasTransparency = combinedAlpha != 0xFF;
            slot.sourceWidth = static_cast<uint16_t>(width);
            slot.sourceHeight = static_cast<uint16_t>(height);
            slot.psAtlasPage = page;
            slot.psCellX = cx; slot.psCellY = cy; slot.psCellW = cw; slot.psCellH = ch;
            slot.psPotW = textureWidth; slot.psPotH = textureHeight;
            {
                const float inv = 1.0f / P.size;
                PsAtlasSetXf3DS(textureId, page * 2 + (P.linear ? 1 : 0), width * inv, (cx + float(kPsAtlasGutter)) * inv, height * inv,
                                1.0f - (cy + float(kPsAtlasGutter) + textureHeight) * inv);
            }
            ++mImpl->textureCacheUploadCount;
            gPsAtlasStats3DS[0]++;
            C3D_TexBind(mImpl->selectedTextureUnit, &slot.texture);
            return;
        }
    }
    // Fast3D recycles LRU texture IDs heavily""")

# la escala del shader no aplica a texturas del atlas (el CPU ya escribio coordenadas de pagina)
rep("""            const auto& slot = mImpl->textures[textureId];
            textureScaleU[texture] *= static_cast<float>(slot.sourceWidth) / slot.texture.width;
            textureScaleV[texture] *= static_cast<float>(slot.sourceHeight) / slot.texture.height;""",
    """            const auto& slot = mImpl->textures[textureId];
            if (slot.psAtlasPage < 0) {
                textureScaleU[texture] *= static_cast<float>(slot.sourceWidth) / slot.texture.width;
                textureScaleV[texture] *= static_cast<float>(slot.sourceHeight) / slot.texture.height;
            }""")

rep("void GfxRenderingAPICitro3D::StartFrame() {", "void GfxRenderingAPICitro3D::StartFrame() {\n    ++sPsAtlasFrame;")

open(p, "w").write(s)
print("patch_atlas_backend: ok")

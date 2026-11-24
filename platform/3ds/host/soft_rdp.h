// RDP por software para lo que el juego dibuja FUERA de la pantalla (render-to-texture).
// Stadium arma texturas en RAM cambiando el color image (paneles del Game Pak Check, cuadros de menus...);
// el interprete Fast3D solo dibuja a la pantalla, asi que esos comandos se rasterizan aqui directo en la
// memoria del juego (big-endian) y despues el juego los usa como textura normal.
//
// Cubre lo que usan los menus 2D: rectangulos con textura (1 ciclo, 2 ciclos, copia), rectangulos de
// relleno, combinador generico, blender basico, alpha compare, TMEM con LOADBLOCK/LOADTILE/LOADTLUT.
// Los triangulos fuera de pantalla solo se cuentan.
#pragma once
#include <cstdint>

class SoftRdp {
  public:
    // Todos los comandos RDP en orden de ejecucion. w0/w1 nativos; ptr = w1 ya resuelto para
    // SETTIMG/SETCIMG (nullptr si no aplica). half1/half2 = palabras de los G_RDPHALF que siguen al texrect.
    void Command(uint32_t w0, uint32_t w1, uint8_t* ptr, uint32_t half1, uint32_t half2);
    void SetDrawing(bool offscreen) { mOffscreen = offscreen; }
    bool Offscreen() const { return mOffscreen; }
    // la imagen la dibuja el GPU (G_PS_FB): aqui solo se sigue el estado, sin escribir en la memoria del juego
    void SetSkip(bool skip) { mSkip = skip; }
    void EndFrame();
    void ResetFrame() { mRects = mFills = mTris = 0; }
    uint32_t mRects = 0, mFills = 0, mTris = 0;
    uint32_t DbgOtherHi() const { return mOtherHi; }
    uint32_t DbgOtherLo() const { return mOtherLo; }
    uint32_t DbgTile(int t) const { const Tile& x = mTiles[t & 7]; return (x.fmt << 28) | (x.siz << 26) | (x.tmem << 16) | (x.line << 6) | (x.masks << 2) | x.cms; }

  private:
    struct Tile {
        uint8_t fmt = 0, siz = 0, palette = 0, cms = 0, cmt = 0, masks = 0, maskt = 0, shifts = 0, shiftt = 0;
        uint16_t line = 0, tmem = 0;
        uint16_t uls = 0, ult = 0, lrs = 0, lrt = 0; // 10.2
    };
    struct Rgba { int r, g, b, a; };

    void TexRect(uint32_t w0, uint32_t w1, uint32_t half1, uint32_t half2, bool flip);
    void FillRect(uint32_t w0, uint32_t w1);
    Rgba Sample(int tileIdx, int s, int t); // s,t en 10.5
    Rgba Combine(int cycle, const Rgba& t0, const Rgba& t1, const Rgba& comb);
    void Plot(int x, int y, Rgba c);

    bool mOffscreen = false;
    bool mSkip = false;
    uint8_t mTmem[4096] = {};
    Tile mTiles[8];
    uint32_t mOtherHi = 0, mOtherLo = 0, mCombHi = 0, mCombLo = 0;
    Rgba mPrim{255, 255, 255, 255}, mEnv{255, 255, 255, 255}, mBlend{0, 0, 0, 0}, mFog{0, 0, 0, 0};
    uint8_t mPrimLod = 0;
    uint32_t mFill = 0;
    uint8_t* mTimg = nullptr;
    uint8_t mTimgSiz = 0;
    uint16_t mTimgWidth = 0;
    uint8_t* mCimg = nullptr;
    uint8_t mCimgSiz = 2;
    uint16_t mCimgWidth = 320;
    int mScX0 = 0, mScY0 = 0, mScX1 = 320, mScY1 = 240; // 10.2 -> enteros
};

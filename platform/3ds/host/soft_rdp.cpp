// RDP por software para render-to-texture (ver soft_rdp.h).
#include "soft_rdp.h"

#include <cstring>
#ifndef __3DS__
#include <cstdio>
#include <cstdlib>
// Banco de pruebas (visor x86): SOFT_DUMP=carpeta guarda cada textura dibujada fuera de pantalla
static void soft_dump(const uint8_t* img, int w, int h, int siz) {
    const char* dir = getenv("SOFT_DUMP");
    if (!dir || !img || siz != 2 || w <= 0 || h <= 0) return;
    static int n;
    char path[512];
    snprintf(path, sizeof path, "%s/soft_%02d_%dx%d.ppm", dir, n++, w, h);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint16_t p = (img[i * 2] << 8) | img[i * 2 + 1];
        uint8_t rgb[3] = { (uint8_t)(((p >> 11) & 31) << 3), (uint8_t)(((p >> 6) & 31) << 3), (uint8_t)(((p >> 1) & 31) << 3) };
        if (!(p & 1)) { rgb[0] = 255; rgb[1] = 0; rgb[2] = 255; }
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
#else
static void soft_dump(const uint8_t*, int, int, int) {}
#endif

namespace {
inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
inline uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
} // namespace

void SoftRdp::Command(uint32_t w0, uint32_t w1, uint8_t* ptr, uint32_t half1, uint32_t half2) {
    uint8_t op = w0 >> 24;
    switch (op) {
        case 0xE3: case 0xE2: { // G_SETOTHERMODE_H / _L (F3DEX2)
            uint32_t len = (w0 & 0xFF) + 1;
            uint32_t sft = 32 - ((w0 >> 8) & 0xFF) - len;
            uint32_t mask = (len >= 32 ? 0xFFFFFFFFu : ((1u << len) - 1)) << sft;
            uint32_t& r = op == 0xE3 ? mOtherHi : mOtherLo;
            r = (r & ~mask) | (w1 & mask);
            break;
        }
        case 0xEF: mOtherHi = w0 & 0xFFFFFF; mOtherLo = w1; break;
        case 0xFC: mCombHi = w0 & 0xFFFFFF; mCombLo = w1; break;
        case 0xFA: mPrimLod = w0 & 0xFF; mPrim = { (int)(w1 >> 24), (int)((w1 >> 16) & 0xFF), (int)((w1 >> 8) & 0xFF), (int)(w1 & 0xFF) }; break;
        case 0xFB: mEnv = { (int)(w1 >> 24), (int)((w1 >> 16) & 0xFF), (int)((w1 >> 8) & 0xFF), (int)(w1 & 0xFF) }; break;
        case 0xF9: mBlend = { (int)(w1 >> 24), (int)((w1 >> 16) & 0xFF), (int)((w1 >> 8) & 0xFF), (int)(w1 & 0xFF) }; break;
        case 0xF8: mFog = { (int)(w1 >> 24), (int)((w1 >> 16) & 0xFF), (int)((w1 >> 8) & 0xFF), (int)(w1 & 0xFF) }; break;
        case 0xF7: mFill = w1; break;
        case 0xFD: mTimg = ptr; mTimgSiz = (w0 >> 19) & 3; mTimgWidth = (w0 & 0xFFF) + 1; break;
        case 0xFF: if (mOffscreen) soft_dump(mCimg, mCimgWidth, mScY1, mCimgSiz); mCimg = ptr; mCimgSiz = (w0 >> 19) & 3; mCimgWidth = (w0 & 0xFFF) + 1; break;
        case 0xED:
            mScX0 = ((w0 >> 12) & 0xFFF) >> 2; mScY0 = (w0 & 0xFFF) >> 2;
            mScX1 = ((w1 >> 12) & 0xFFF) >> 2; mScY1 = (w1 & 0xFFF) >> 2;
            break;
        case 0xF5: { // G_SETTILE
            Tile& t = mTiles[(w1 >> 24) & 7];
            t.fmt = (w0 >> 21) & 7; t.siz = (w0 >> 19) & 3; t.line = (w0 >> 9) & 0x1FF; t.tmem = w0 & 0x1FF;
            t.palette = (w1 >> 20) & 0xF; t.cmt = (w1 >> 18) & 3; t.maskt = (w1 >> 14) & 0xF; t.shiftt = (w1 >> 10) & 0xF;
            t.cms = (w1 >> 8) & 3; t.masks = (w1 >> 4) & 0xF; t.shifts = w1 & 0xF;
            break;
        }
        case 0xF2: { // G_SETTILESIZE
            Tile& t = mTiles[(w1 >> 24) & 7];
            t.uls = (w0 >> 12) & 0xFFF; t.ult = w0 & 0xFFF; t.lrs = (w1 >> 12) & 0xFFF; t.lrt = w1 & 0xFFF;
            break;
        }
        case 0xF3: { // G_LOADBLOCK
            Tile& t = mTiles[(w1 >> 24) & 7];
            if (!mTimg) break;
            uint32_t uls = (w0 >> 12) & 0xFFF, ult = w0 & 0xFFF, lrs = (w1 >> 12) & 0xFFF;
            uint32_t count = lrs - uls + 1;
            uint32_t bytes = mTimgSiz == 0 ? count / 2 : count << (mTimgSiz - 1);
            uint32_t off = mTimgSiz == 0 ? (ult * mTimgWidth + uls) / 2 : (ult * mTimgWidth + uls) << (mTimgSiz - 1);
            if (bytes > 4096) bytes = 4096;
            for (uint32_t i = 0; i < bytes; i++) mTmem[(t.tmem * 8 + i) & 0xFFF] = mTimg[off + i];
            t.uls = uls << 2; t.ult = ult << 2; t.lrs = lrs << 2; t.lrt = w1 & 0xFFF;
            break;
        }
        case 0xF4: { // G_LOADTILE
            Tile& t = mTiles[(w1 >> 24) & 7];
            if (!mTimg) break;
            t.uls = (w0 >> 12) & 0xFFF; t.ult = w0 & 0xFFF; t.lrs = (w1 >> 12) & 0xFFF; t.lrt = w1 & 0xFFF;
            int s0 = t.uls >> 2, t0 = t.ult >> 2, s1 = t.lrs >> 2, t1 = t.lrt >> 2;
            for (int r = t0; r <= t1; r++) {
                uint32_t n = s1 - s0 + 1, off = r * mTimgWidth + s0;
                if (mTimgSiz == 0) { n /= 2; off /= 2; } else { n <<= mTimgSiz - 1; off <<= mTimgSiz - 1; }
                uint32_t dst = t.tmem * 8 + (r - t0) * t.line * 8;
                for (uint32_t i = 0; i < n; i++) mTmem[(dst + i) & 0xFFF] = mTimg[off + i];
            }
            break;
        }
        case 0xF0: { // G_LOADTLUT
            Tile& t = mTiles[(w1 >> 24) & 7];
            if (!mTimg) break;
            uint32_t uls = ((w0 >> 12) & 0xFFF) >> 2, lrs = ((w1 >> 12) & 0xFFF) >> 2;
            for (uint32_t i = 0; i < (lrs - uls + 1) * 2 && i < 512; i++) mTmem[(t.tmem * 8 + i) & 0xFFF] = mTimg[uls * 2 + i];
            break;
        }
        case 0xE4: case 0xE5:
            if (mOffscreen) { mRects++; if (!mSkip) TexRect(w0, w1, half1, half2, op == 0xE5); }
            break;
        case 0xF6:
            if (mOffscreen) { mFills++; if (!mSkip) FillRect(w0, w1); }
            break;
        case 0x05: case 0x06: case 0x07:
            if (mOffscreen) mTris++;
            break;
        default: break;
    }
}

SoftRdp::Rgba SoftRdp::Sample(int ti, int s, int t) {
    const Tile& T = mTiles[ti & 7];
    auto coord = [](int v, int shift, int lo, int hi, int mask, int cm) {
        if (shift <= 10) v >>= shift; else v <<= 16 - shift;
        v = (v - (lo << 3)) >> 5;
        int size = (hi - lo) >> 2;
        if ((cm & 2) || mask == 0) { if (v < 0) v = 0; if (v > size) v = size; }
        if (mask) {
            if ((cm & 1) && ((v >> mask) & 1)) v = ~v;
            v &= (1 << mask) - 1;
        }
        return v;
    };
    int ss = coord(s, T.shifts, T.uls, T.lrs, T.masks, T.cms);
    int tt = coord(t, T.shiftt, T.ult, T.lrt, T.maskt, T.cmt);
    uint32_t base = T.tmem * 8 + tt * T.line * 8;
    uint32_t v = 0;
    switch (T.siz) {
        case 0: { uint8_t b = mTmem[(base + ss / 2) & 0xFFF]; v = (ss & 1) ? (b & 0xF) : (b >> 4); break; }
        case 1: v = mTmem[(base + ss) & 0xFFF]; break;
        case 2: v = rd16(&mTmem[(base + ss * 2) & 0xFFE]); break;
        default: { uint32_t a = (base + ss * 4) & 0xFFC; v = (mTmem[a] << 24) | (mTmem[a + 1] << 16) | (mTmem[a + 2] << 8) | mTmem[a + 3]; break; }
    }
    auto rgba16 = [](uint32_t p) {
        return Rgba{ (int)((p >> 11) & 31) * 255 / 31, (int)((p >> 6) & 31) * 255 / 31, (int)((p >> 1) & 31) * 255 / 31, (p & 1) ? 255 : 0 };
    };
    switch (T.fmt) {
        case 0: // RGBA
            if (T.siz == 3) return { (int)(v >> 24), (int)((v >> 16) & 0xFF), (int)((v >> 8) & 0xFF), (int)(v & 0xFF) };
            return rgba16(v);
        case 2: { // CI
            uint32_t idx = T.siz == 0 ? (T.palette * 16 + v) : v;
            uint16_t p = rd16(&mTmem[(0x800 + idx * 2) & 0xFFE]);
            if (((mOtherHi >> 14) & 3) == 3) return { p >> 8, p >> 8, p >> 8, p & 0xFF };
            return rgba16(p);
        }
        case 3: // IA
            if (T.siz == 0) { int i = (v >> 1) * 255 / 7; return { i, i, i, (v & 1) ? 255 : 0 }; }
            if (T.siz == 1) { int i = (v >> 4) * 17; return { i, i, i, (int)(v & 15) * 17 }; }
            return { (int)(v >> 8), (int)(v >> 8), (int)(v >> 8), (int)(v & 0xFF) };
        case 4: { // I
            int i = T.siz == 0 ? (int)v * 17 : (int)(v & 0xFF);
            return { i, i, i, i };
        }
        default: return { 255, 255, 255, 255 };
    }
}

SoftRdp::Rgba SoftRdp::Combine(int cyc, const Rgba& t0, const Rgba& t1, const Rgba& comb) {
    uint32_t hi = mCombHi, lo = mCombLo;
    int a, b, c, d, aa, ab, ac, ad;
    if (cyc == 0) {
        a = (hi >> 20) & 0xF; c = (hi >> 15) & 0x1F; aa = (hi >> 12) & 7; ac = (hi >> 9) & 7;
        b = (lo >> 28) & 0xF; d = (lo >> 15) & 7; ab = (lo >> 12) & 7; ad = (lo >> 9) & 7;
    } else {
        a = (hi >> 5) & 0xF; c = hi & 0x1F; aa = (lo >> 21) & 7; ac = (lo >> 18) & 7;
        b = (lo >> 24) & 0xF; d = (lo >> 6) & 7; ab = (lo >> 3) & 7; ad = lo & 7;
    }
    const Rgba zero{ 0, 0, 0, 0 }, one{ 255, 255, 255, 255 }, shade{ 0, 0, 0, 0 };
    auto src = [&](int sel) -> Rgba {
        switch (sel) {
            case 0: return comb; case 1: return t0; case 2: return t1; case 3: return mPrim;
            case 4: return shade; case 5: return mEnv; default: return zero;
        }
    };
    Rgba A = a == 6 ? one : a == 7 ? Rgba{ 128, 128, 128, 128 } : (a < 6 ? src(a) : zero);
    Rgba B = b < 6 ? src(b) : zero;
    Rgba C;
    switch (c) {
        case 7: C = { comb.a, comb.a, comb.a, 0 }; break;
        case 8: C = { t0.a, t0.a, t0.a, 0 }; break;
        case 9: C = { t1.a, t1.a, t1.a, 0 }; break;
        case 10: C = { mPrim.a, mPrim.a, mPrim.a, 0 }; break;
        case 11: C = { shade.a, shade.a, shade.a, 0 }; break;
        case 12: C = { mEnv.a, mEnv.a, mEnv.a, 0 }; break;
        case 13: C = { 0, 0, 0, 0 }; break;
        case 14: C = { mPrimLod, mPrimLod, mPrimLod, 0 }; break;
        default: C = c < 6 ? src(c) : zero; break;
    }
    Rgba D = d == 6 ? one : d == 7 ? zero : src(d);
    auto asrc = [&](int sel) -> int {
        switch (sel) {
            case 0: return comb.a; case 1: return t0.a; case 2: return t1.a; case 3: return mPrim.a;
            case 4: return shade.a; case 5: return mEnv.a; case 6: return 255; default: return 0;
        }
    };
    int Ac = ac == 0 ? 0 : ac == 6 ? mPrimLod : ac == 7 ? 0 : asrc(ac);
    Rgba r;
    r.r = clamp255((A.r - B.r) * C.r / 255 + D.r);
    r.g = clamp255((A.g - B.g) * C.g / 255 + D.g);
    r.b = clamp255((A.b - B.b) * C.b / 255 + D.b);
    r.a = clamp255((asrc(aa) - asrc(ab)) * Ac / 255 + asrc(ad));
    return r;
}

void SoftRdp::Plot(int x, int y, Rgba c) {
    if (!mCimg) return;
    uint32_t lo = mOtherLo;
    bool two = ((mOtherHi >> 20) & 3) == 1;
    int P = two ? (lo >> 28) & 3 : (lo >> 30) & 3;
    int A = two ? (lo >> 24) & 3 : (lo >> 26) & 3;
    int M = two ? (lo >> 20) & 3 : (lo >> 22) & 3;
    int B = two ? (lo >> 16) & 3 : (lo >> 18) & 3;
    uint8_t* px = mCimg + (y * mCimgWidth + x) * (mCimgSiz == 3 ? 4 : 2);
    Rgba mem;
    if (mCimgSiz == 3) mem = { px[0], px[1], px[2], px[3] };
    else { uint16_t p = rd16(px); mem = { (int)((p >> 11) & 31) * 255 / 31, (int)((p >> 6) & 31) * 255 / 31, (int)((p >> 1) & 31) * 255 / 31, (p & 1) ? 255 : 0 }; }
    Rgba out = c;
    if (lo & 0x4000) { // FORCE_BL
        auto col = [&](int s) { return s == 0 ? c : s == 1 ? mem : s == 2 ? mBlend : mFog; };
        int a = A == 0 ? c.a : A == 1 ? mFog.a : 0;
        int b = B == 0 ? 255 - a : B == 1 ? mem.a : B == 2 ? 255 : 0;
        Rgba p = col(P), m = col(M);
        int div = B == 0 ? 255 : 256;
        out.r = clamp255((p.r * a + m.r * b) / div);
        out.g = clamp255((p.g * a + m.g * b) / div);
        out.b = clamp255((p.b * a + m.b * b) / div);
    }
    bool cvgSave = ((lo >> 8) & 3) == 3;
    if (mCimgSiz == 3) {
        px[0] = out.r; px[1] = out.g; px[2] = out.b; px[3] = cvgSave ? mem.a : 255;
    } else {
        uint16_t v = ((out.r >> 3) << 11) | ((out.g >> 3) << 6) | ((out.b >> 3) << 1) | (cvgSave ? (mem.a ? 1 : 0) : 1);
        px[0] = v >> 8; px[1] = v & 0xFF;
    }
}

void SoftRdp::TexRect(uint32_t w0, uint32_t w1, uint32_t half1, uint32_t half2, bool flip) {
    int xh = (w0 >> 12) & 0xFFF, yh = w0 & 0xFFF;
    int tile = (w1 >> 24) & 7;
    int xl = (w1 >> 12) & 0xFFF, yl = w1 & 0xFFF;
    int s = (int16_t)(half1 >> 16), t = (int16_t)(half1 & 0xFFFF);
    int dsdx = (int16_t)(half2 >> 16), dtdy = (int16_t)(half2 & 0xFFFF);
    int cyc = (mOtherHi >> 20) & 3;
    if (cyc >= 2) { xh += 4; yh += 4; }
    if (cyc == 2) dsdx >>= 2;
    int x0 = xl >> 2, y0 = yl >> 2, x1 = xh >> 2, y1 = yh >> 2;
    int cx0 = x0 < mScX0 ? mScX0 : x0, cy0 = y0 < mScY0 ? mScY0 : y0;
    int cx1 = x1 > mScX1 ? mScX1 : x1, cy1 = y1 > mScY1 ? mScY1 : y1;
    if (cx1 > mCimgWidth) cx1 = mCimgWidth;
    bool ac = (mOtherLo & 3) == 1;
    for (int y = cy0; y < cy1; y++) {
        for (int x = cx0; x < cx1; x++) {
            int dx = x - x0, dy = y - y0;
            int ss, tt;
            if (!flip) { ss = s + ((dx * dsdx) >> 5); tt = t + ((dy * dtdy) >> 5); }
            else { ss = s + ((dy * dsdx) >> 5); tt = t + ((dx * dtdy) >> 5); }
            Rgba t0 = Sample(tile, ss, tt);
            Rgba c;
            if (cyc == 2) {
                if (ac && t0.a == 0) continue;
                c = t0;
                // copia directa: sin blender
                if (!mCimg) return;
                if (mCimgSiz == 3) { uint8_t* px = mCimg + (y * mCimgWidth + x) * 4; px[0] = c.r; px[1] = c.g; px[2] = c.b; px[3] = c.a; }
                else { uint8_t* px = mCimg + (y * mCimgWidth + x) * 2; uint16_t v = ((c.r >> 3) << 11) | ((c.g >> 3) << 6) | ((c.b >> 3) << 1) | (c.a ? 1 : 0); px[0] = v >> 8; px[1] = v & 0xFF; }
                continue;
            }
            Rgba t1 = cyc == 1 ? Sample(tile + 1, ss, tt) : t0;
            c = Combine(0, t0, t1, Rgba{ 0, 0, 0, 0 });
            if (cyc == 1) c = Combine(1, t0, t1, c);
            if (ac && c.a < mBlend.a) continue;
            if (ac && mBlend.a == 0 && c.a == 0) continue;
            Plot(x, y, c);
        }
    }
}

void SoftRdp::FillRect(uint32_t w0, uint32_t w1) {
    int xh = (w0 >> 12) & 0xFFF, yh = w0 & 0xFFF, xl = (w1 >> 12) & 0xFFF, yl = w1 & 0xFFF;
    int cyc = (mOtherHi >> 20) & 3;
    if (cyc >= 2) { xh += 4; yh += 4; }
    int x0 = xl >> 2, y0 = yl >> 2, x1 = xh >> 2, y1 = yh >> 2;
    if (x0 < mScX0) x0 = mScX0; if (y0 < mScY0) y0 = mScY0;
    if (x1 > mScX1) x1 = mScX1; if (y1 > mScY1) y1 = mScY1;
    if (x1 > mCimgWidth) x1 = mCimgWidth;
    if (!mCimg) return;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            if (cyc == 3) {
                if (mCimgSiz == 3) { uint8_t* px = mCimg + (y * mCimgWidth + x) * 4; px[0] = mFill >> 24; px[1] = mFill >> 16; px[2] = mFill >> 8; px[3] = mFill; }
                else { uint16_t v = (x & 1) ? (mFill & 0xFFFF) : (mFill >> 16); uint8_t* px = mCimg + (y * mCimgWidth + x) * 2; px[0] = v >> 8; px[1] = v & 0xFF; }
            } else {
                Rgba z{ 0, 0, 0, 0 };
                Rgba c = Combine(0, z, z, z);
                if (cyc == 1) c = Combine(1, z, z, c);
                Plot(x, y, c);
            }
        }
}

void SoftRdp::EndFrame() {
    if (mOffscreen) soft_dump(mCimg, mCimgWidth, mScY1, mCimgSiz);
}

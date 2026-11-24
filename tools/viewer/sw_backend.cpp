// Rasterizador por software para el visor: aproxima el combinador del RDP (ciclo 1 y 2) lo suficiente
// para verificar que las display lists convertidas se ven como deberian.
#include "sw_backend.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Fast {

struct ShaderProgram {
    uint64_t id0, id1;
    bool alpha, twoCycle, alphaThreshold, invisible, textureEdge;
    bool usedTextures[2] = {};
    bool clamp[2][2] = {};
    uint8_t numInputs = 0;
    int texOff[2] = {};
    int inputOff[7] = {};
    int stride = 4;
    int comb[2][2][4];
};

static bool Opt(uint64_t o, int bit) { return (o >> bit) & 1; }

GfxRenderingAPISoft::GfxRenderingAPISoft(int w, int h) : mW(w), mH(h), mColor(w * h * 4, 0), mDepth(w * h, 1e30f) {
    mTex.resize(1);
}

ShaderProgram* GfxRenderingAPISoft::LookupShader(uint64_t id0, uint64_t id1) {
    for (auto* p : mPrograms) if (p->id0 == id0 && p->id1 == id1) return p;
    return nullptr;
}

ShaderProgram* GfxRenderingAPISoft::CreateAndLoadNewShader(uint64_t id0, uint64_t id1) {
    auto* p = new ShaderProgram();
    p->id0 = id0;
    p->id1 = id1;
    p->alpha = Opt(id1, 0);
    bool fog = Opt(id1, 1), gray = Opt(id1, 7);
    p->textureEdge = Opt(id1, 2);
    p->twoCycle = Opt(id1, 4);
    p->alphaThreshold = Opt(id1, 5);
    p->invisible = Opt(id1, 6);
    p->clamp[0][0] = Opt(id1, 8); p->clamp[0][1] = Opt(id1, 9);
    p->clamp[1][0] = Opt(id1, 10); p->clamp[1][1] = Opt(id1, 11);
    for (int c = 0; c < 2; c++)
        for (int ch = 0; ch < 2; ch++)
            for (int t = 0; t < 4; t++) {
                int src = (int)((id0 >> (c * 32 + ch * 16 + t * 4)) & 0xF);
                p->comb[c][ch][t] = src;
                if (src >= 1 && src <= 7) p->numInputs = std::max<uint8_t>(p->numInputs, src);
                if (src >= 8 && src <= 11) {
                    int tex = src <= 9 ? 0 : 1;
                    p->usedTextures[tex] = true;
                    if (p->twoCycle) p->usedTextures[tex ^ 1] = true;
                }
            }
    int off = 4;
    for (int t = 0; t < 2; t++) {
        if (!p->usedTextures[t]) continue;
        p->texOff[t] = off;
        off += 2 + (p->clamp[t][0] ? 1 : 0) + (p->clamp[t][1] ? 1 : 0);
    }
    if (fog) off += 4;
    if (gray) off += 4;
    for (int i = 0; i < p->numInputs; i++) { p->inputOff[i] = off; off += p->alpha ? 4 : 3; }
    p->stride = off;
    mPrograms.push_back(p);
    mCur = p;
    return p;
}

void GfxRenderingAPISoft::ShaderGetInfo(ShaderProgram* p, uint8_t* n, bool used[2]) {
    *n = p->numInputs;
    used[0] = p->usedTextures[0];
    used[1] = p->usedTextures[1];
}

uint32_t GfxRenderingAPISoft::NewTexture() { mTex.emplace_back(); return (uint32_t)mTex.size() - 1; }

void GfxRenderingAPISoft::UploadTexture(const uint8_t* rgba, uint32_t w, uint32_t h) {
    Tex& t = mTex[mBound[mSelected]];
    t.w = w; t.h = h;
    t.px.assign(rgba, rgba + w * h * 4);
    static int dbg = getenv("SW_DEBUG") ? 12 : 0;
    if (dbg > 0) {
        dbg--;
        unsigned long sum[4] = {};
        for (uint32_t i = 0; i < w * h; i++) for (int c = 0; c < 4; c++) sum[c] += rgba[i * 4 + c];
        fprintf(stderr, "[sw] textura %u: %ux%u promedio rgba %lu %lu %lu %lu\n", mBound[mSelected], w, h,
                sum[0] / (w * h), sum[1] / (w * h), sum[2] / (w * h), sum[3] / (w * h));
    }
}

void GfxRenderingAPISoft::SwitchFb(int fb) {
    // cada framebuffer tiene su propio color; el actual vive en mColor
    if ((int)mFbColor.size() <= std::max(fb, mFb)) mFbColor.resize(std::max(fb, mFb) + 1, std::vector<uint8_t>(mW * mH * 4, 0));
    mFbColor[mFb] = mColor;
    mFb = fb;
    mColor = mFbColor[fb];
}

void GfxRenderingAPISoft::ClearFramebuffer(bool color, bool depth) {
    if (getenv("SW_DEBUG")) fprintf(stderr, "[sw] limpiar fb %d color=%d depth=%d (tris %u)\n", mFb, color, depth, drawnTriangles);
    if (color) std::fill(mColor.begin(), mColor.end(), 0);
    if (depth) std::fill(mDepth.begin(), mDepth.end(), 1e30f);
}

namespace {
struct V { float x, y, z, iw; const float* a; };
inline float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
}

void GfxRenderingAPISoft::DrawTriangles(float vbo[], size_t len, size_t tris) {
    ShaderProgram* p = mCur;
    if (!p || p->invisible) return;
    const int S = p->stride;
    for (size_t t = 0; t < tris; t++) {
        V v[3];
        bool behind = false;
        for (int k = 0; k < 3; k++) {
            const float* a = vbo + (t * 3 + k) * S;
            if (a[3] <= 0.00001f) { behind = true; break; }
            float iw = 1.0f / a[3];
            float nx = a[0] * iw, ny = a[1] * iw;
            v[k] = { mVx + (nx + 1) * 0.5f * mVw, (float)mH - (mVy + (ny + 1) * 0.5f * mVh), a[2] * iw, iw, a };
        }
        if (behind) continue;
        static int dbg = getenv("SW_DEBUG") ? 40 : 0;
        if (dbg > 0) {
            dbg--;
            fprintf(stderr, "[sw] tri (%.1f,%.1f) (%.1f,%.1f) (%.1f,%.1f) z=%.3f vp=%d,%d %dx%d sc=%d,%d %dx%d id0=%016llx id1=%llx tex=%d,%d in=%d alpha=%d\n",
                    v[0].x, v[0].y, v[1].x, v[1].y, v[2].x, v[2].y, v[0].z, mVx, mVy, mVw, mVh, mSx, mSy, mSw, mSh,
                    (unsigned long long)p->id0, (unsigned long long)p->id1, p->usedTextures[0], p->usedTextures[1], p->numInputs, p->alpha);
            if (p->numInputs) fprintf(stderr, "      input1=%.2f %.2f %.2f\n", v[0].a[p->inputOff[0]], v[0].a[p->inputOff[0]+1], v[0].a[p->inputOff[0]+2]);
        }
        float minx = std::max<float>(std::min({ v[0].x, v[1].x, v[2].x }), std::max(0, mSx));
        float maxx = std::min<float>(std::max({ v[0].x, v[1].x, v[2].x }), std::min(mW, mSx + mSw) - 1);
        float miny = std::max<float>(std::min({ v[0].y, v[1].y, v[2].y }), std::max(0, mH - (mSy + mSh)));
        float maxy = std::min<float>(std::max({ v[0].y, v[1].y, v[2].y }), std::min(mH, mH - mSy) - 1);
        float area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y);
        if (std::fabs(area) < 1e-6f) continue;
        drawnTriangles++;
        for (int y = (int)std::ceil(miny - 0.5f); y <= (int)maxy; y++) {
            for (int x = (int)std::ceil(minx - 0.5f); x <= (int)maxx; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((v[1].x - px) * (v[2].y - py) - (v[2].x - px) * (v[1].y - py)) / area;
                float w1 = ((v[2].x - px) * (v[0].y - py) - (v[0].x - px) * (v[2].y - py)) / area;
                float w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float z = w0 * v[0].z + w1 * v[1].z + w2 * v[2].z;
                size_t pi = (size_t)y * mW + x;
                if (mDepthTest && z > mDepth[pi]) continue;
                // interpolacion con correccion de perspectiva
                float iw = w0 * v[0].iw + w1 * v[1].iw + w2 * v[2].iw;
                auto attr = [&](int o) {
                    return (w0 * v[0].a[o] * v[0].iw + w1 * v[1].a[o] * v[1].iw + w2 * v[2].a[o] * v[2].iw) / iw;
                };
                float texel[2][4] = { { 0, 0, 0, 1 }, { 0, 0, 0, 1 } };
                for (int tx = 0; tx < 2; tx++) {
                    if (!p->usedTextures[tx]) continue;
                    const Tex& tex = mTex[mBound[tx] < mTex.size() ? mBound[tx] : 0];
                    if (tex.w == 0) continue;
                    float u = attr(p->texOff[tx]), vv = attr(p->texOff[tx] + 1);
                    int iu = (int)std::floor(u * tex.w), iv = (int)std::floor(vv * tex.h);
                    iu = ((iu % (int)tex.w) + tex.w) % tex.w;
                    iv = ((iv % (int)tex.h) + tex.h) % tex.h;
                    const uint8_t* c = &tex.px[(iv * tex.w + iu) * 4];
                    for (int i = 0; i < 4; i++) texel[tx][i] = c[i] / 255.0f;
                }
                float in[7][4];
                for (int i = 0; i < p->numInputs; i++) {
                    in[i][0] = attr(p->inputOff[i]);
                    in[i][1] = attr(p->inputOff[i] + 1);
                    in[i][2] = attr(p->inputOff[i] + 2);
                    in[i][3] = p->alpha ? attr(p->inputOff[i] + 3) : 1.0f;
                }
                float comb[4] = { 0, 0, 0, 0 };
                for (int cyc = 0; cyc < (p->twoCycle ? 2 : 1); cyc++) {
                    float out[4];
                    for (int ch = 0; ch < 2; ch++) {
                        float val[4][3];
                        for (int term = 0; term < 4; term++) {
                            int s = p->comb[cyc][ch][term];
                            for (int c = 0; c < 3; c++) {
                                float r = 0;
                                int idx = ch == 0 ? c : 3;
                                if (s >= 1 && s <= 7) r = in[s - 1][idx];
                                else if (s == 8) r = texel[0][idx];
                                else if (s == 9) r = texel[0][3];
                                else if (s == 10) r = texel[1][idx];
                                else if (s == 11) r = texel[1][3];
                                else if (s == 12) r = 1;
                                else if (s == 13) r = comb[idx];
                                else if (s == 14) r = 0.5f;
                                val[term][c] = r;
                            }
                        }
                        if (ch == 0) for (int c = 0; c < 3; c++) out[c] = (val[0][c] - val[1][c]) * val[2][c] + val[3][c];
                        else out[3] = (val[0][0] - val[1][0]) * val[2][0] + val[3][0];
                    }
                    for (int i = 0; i < 4; i++) comb[i] = Clamp01(out[i]);
                }
                float a = p->alpha ? comb[3] : 1.0f;
                if ((p->alphaThreshold || p->textureEdge) && a < 0.125f) continue;
                uint8_t* d = &mColor[pi * 4];
                for (int c = 0; c < 3; c++) {
                    float src = comb[c];
                    float dst = d[c] / 255.0f;
                    float r = mUseAlpha ? src * a + dst * (1 - a) : src;
                    d[c] = (uint8_t)(Clamp01(r) * 255.0f + 0.5f);
                }
                d[3] = 255;
                if (mDepthMask) mDepth[pi] = z;
            }
        }
    }
}

bool GfxRenderingAPISoft::SavePng(const char* path) {
    // PPM del framebuffer del juego (el 1); el 0 es la pantalla, donde el interprete lo copia
    SwitchFb(mFbColor.size() > 1 ? 1 : mFb);
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", mW, mH);
    for (int i = 0; i < mW * mH; i++) fwrite(&mColor[i * 4], 1, 3, f);
    fclose(f);
    return true;
}

} // namespace Fast

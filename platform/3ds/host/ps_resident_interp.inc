// F2: lotes 'PSK2' (gfx_convert, cache de modelos de F1) dibujados desde buffers residentes en el PICA200.
// Se incluye en el interprete (patch_interp_skin.py), despues de PsSkinGpu3DS: usa su planeacion del material
// (begin, boneRows, lightFor, computeVertex) para que el resultado sea el mismo que el del camino anterior.
//
// Copia para el GPU de un lote (una vez; la libera la cache con el lote), en memoria lineal:
//   PsResHeader3DS, PsResGroup3DS[nGroups], PsResSeg3DS[nSegs], vertices (16 bytes, alineados a 16), indices u16
// Un grupo son segmentos seguidos que caben juntos (hasta 11 huesos, 65535 vertices) y comparten vertices e
// indices. Al dibujar, los segmentos de un grupo se juntan en un mismo dibujo mientras el modo de geometria no
// cambie, con la misma regla con la que el camino anterior juntaba sus envios.
#include "ps_resident.h"

extern "C" { void* PsModelGpuGet3DS(uint32_t id, uint32_t key); void* PsModelGpuAttach3DS(uint32_t id, uint32_t key, uint32_t bytes);
             bool Mk64System3DSCleanDataCache(const void* data, size_t size); }
// 0 lotes F2, 1 grupos F2, 2 lote al camino anterior: sin copia (memoria), 3 cerca del ojo, 4 grupo con mas de 11
// huesos, 5 grupo al camino anterior: texgen, 6 luz en el CPU, 7 sombra en dos entradas, 8 material, 9 copias
// armadas, 10 verificacion igual, 11 distinta, 12 ambas caras (no se dibuja), 13 lotes PSK2 por el camino anterior
extern "C" uint64_t gPsResStats3DS[16];
uint64_t gPsResStats3DS[16];
extern "C" bool gPsResVerify3DS;
bool gPsResVerify3DS = false;
static constexpr int kPsResGroupBones3DS = kPsResMaxBones3DS;

struct PsResHeader3DS { uint32_t nGroups, nSegs, bytes, pad; };
struct PsResGroup3DS {
    uint32_t seg0, nSeg;      // segmentos del lote que cubre
    uint32_t vtxOff, idxOff;  // desde el inicio de la copia
    uint32_t nVtx, nIdx;
    int32_t nBones;           // -1: mas de 11 huesos (no va por F2)
    uint8_t bone[kPsResGroupBones3DS];
    uint8_t pad;
    float sph[kPsResGroupBones3DS][4]; // esfera de los vertices de cada hueso (para saber si queda cerca del ojo)
};
struct PsResSeg3DS { uint32_t vtx0, nVtx, idx0, nIdx; }; // dentro de su grupo

static const uint8_t* PsResBuild3DS(uint32_t id, uint32_t key, const uint32_t* body, uint32_t nSegs, uint32_t nBones, uint16_t sfS, uint16_t sfT) {
    struct SegInfo { const uint32_t* ops; uint32_t nOps; const PsSkinVtx3DS* v; uint32_t nVtx; const uint16_t* ix; uint32_t nIdx; };
    static std::vector<SegInfo> segs;
    segs.clear();
    const uint32_t* q = body;
    for (uint32_t sg = 0; sg < nSegs; sg++) {
        SegInfo s{};
        s.nOps = q[0]; s.nVtx = q[1]; s.nIdx = q[2];
        s.ops = q + 3;
        q += 3 + s.nOps * 2;
        s.v = reinterpret_cast<const PsSkinVtx3DS*>(q);
        q += s.nVtx * sizeof(PsSkinVtx3DS) / 4;
        s.ix = reinterpret_cast<const uint16_t*>(q);
        q += (s.nIdx * 2 + 3) / 4;
        segs.push_back(s);
    }
    // grupos: un segmento con cambios de modo empieza grupo; tambien si ya no caben los huesos o los vertices
    static std::vector<PsResGroup3DS> groups;
    groups.clear();
    for (uint32_t sg = 0; sg < nSegs; sg++) {
        const SegInfo& s = segs[sg];
        bool start = groups.empty();
        uint8_t local[kPsResGroupBones3DS];
        int nLocal = 0;
        bool tooMany = false;
        for (uint32_t i = 0; i < s.nVtx && !tooMany; i++) {
            const uint8_t b = s.v[i].bone < nBones ? s.v[i].bone : 0;
            bool have = false;
            for (int k = 0; k < nLocal; k++) have |= local[k] == b;
            if (!have) { if (nLocal == kPsResGroupBones3DS) tooMany = true; else local[nLocal++] = b; }
        }
        if (!start) {
            PsResGroup3DS& g = groups.back();
            int extra = 0;
            for (int k = 0; k < nLocal; k++) {
                bool have = false;
                for (int j = 0; j < g.nBones; j++) have |= g.bone[j] == local[k];
                extra += !have;
            }
            if (g.nBones < 0 || tooMany || g.nBones + extra > kPsResGroupBones3DS || g.nVtx + s.nVtx > 65535) start = true;
        }
        if (start) {
            PsResGroup3DS g{};
            g.seg0 = sg; g.nSeg = 0; g.nBones = 0;
            groups.push_back(g);
        }
        PsResGroup3DS& g = groups.back();
        g.nSeg++;
        g.nVtx += s.nVtx;
        g.nIdx += s.nIdx;
        if (tooMany) g.nBones = -1;
        for (int k = 0; k < nLocal && g.nBones >= 0; k++) {
            bool have = false;
            for (int j = 0; j < g.nBones; j++) have |= g.bone[j] == local[k];
            if (!have) g.bone[g.nBones++] = local[k];
        }
    }
    uint32_t vtxTotal = 0, idxTotal = 0;
    for (auto& g : groups) { vtxTotal += g.nVtx; idxTotal += g.nIdx; }
    const uint32_t head = (uint32_t)(sizeof(PsResHeader3DS) + groups.size() * sizeof(PsResGroup3DS) + nSegs * sizeof(PsResSeg3DS) + 15) & ~15u;
    const uint32_t bytes = head + vtxTotal * (uint32_t)sizeof(PsResidentVertex3DS) + ((idxTotal * 2 + 15) & ~15u);
    uint8_t* mem = static_cast<uint8_t*>(PsModelGpuAttach3DS(id, key, bytes));
    if (!mem) return nullptr;
    auto* hdr = reinterpret_cast<PsResHeader3DS*>(mem);
    hdr->nGroups = (uint32_t)groups.size(); hdr->nSegs = nSegs; hdr->bytes = bytes; hdr->pad = 0;
    auto* gout = reinterpret_cast<PsResGroup3DS*>(mem + sizeof(PsResHeader3DS));
    auto* sout = reinterpret_cast<PsResSeg3DS*>(mem + sizeof(PsResHeader3DS) + groups.size() * sizeof(PsResGroup3DS));
    auto* vout = reinterpret_cast<PsResidentVertex3DS*>(mem + head);
    auto* iout = reinterpret_cast<uint16_t*>(mem + head + vtxTotal * sizeof(PsResidentVertex3DS));
    uint32_t vAt = 0, iAt = 0;
    for (size_t gi = 0; gi < groups.size(); gi++) {
        PsResGroup3DS g = groups[gi];
        g.vtxOff = head + vAt * (uint32_t)sizeof(PsResidentVertex3DS);
        g.idxOff = head + vtxTotal * (uint32_t)sizeof(PsResidentVertex3DS) + iAt * 2;
        float mn[kPsResGroupBones3DS][3], mx[kPsResGroupBones3DS][3];
        for (int k = 0; k < kPsResGroupBones3DS; k++)
            for (int a = 0; a < 3; a++) { mn[k][a] = 1e30f; mx[k][a] = -1e30f; }
        uint32_t base = 0, ibase = 0;
        for (uint32_t sg = g.seg0; sg < g.seg0 + g.nSeg; sg++) {
            const SegInfo& s = segs[sg];
            sout[sg] = { base, s.nVtx, ibase, s.nIdx };
            ibase += s.nIdx;
            for (uint32_t i = 0; i < s.nVtx; i++) {
                const PsSkinVtx3DS& sv = s.v[i];
                const uint8_t b = sv.bone < nBones ? sv.bone : 0;
                int slot = 0;
                if (g.nBones > 0)
                    for (int k = 0; k < g.nBones; k++) if (g.bone[k] == b) { slot = k; break; }
                PsResidentVertex3DS& o = vout[vAt + base + i];
                o.x = sv.v.v.ob[0]; o.y = sv.v.v.ob[1]; o.z = sv.v.v.ob[2];
                o.boneRow = (int16_t)(slot * kPsSkinRows3DS);
                // como GfxSpVertex / computeVertex: (short)(tc * escala >> 16)
                o.u = (short)(sv.v.v.tc[0] * sfS >> 16);
                o.v = (short)(sv.v.v.tc[1] * sfT >> 16);
                std::memcpy(o.c, sv.v.v.cn, 4);
                for (int a = 0; a < 3; a++) {
                    const float c = sv.v.v.ob[a];
                    if (c < mn[slot][a]) mn[slot][a] = c;
                    if (c > mx[slot][a]) mx[slot][a] = c;
                }
            }
            for (uint32_t t = 0; t < s.nIdx; t++) iout[iAt + t] = (uint16_t)(s.ix[t] < s.nVtx ? s.ix[t] + base : base);
            iAt += s.nIdx;
            base += s.nVtx;
        }
        for (int k = 0; k < (g.nBones > 0 ? g.nBones : 0); k++) {
            float rr = 0.0f;
            for (int a = 0; a < 3; a++) {
                if (mn[k][a] > mx[k][a]) { mn[k][a] = mx[k][a] = 0.0f; }
                g.sph[k][a] = 0.5f * (mn[k][a] + mx[k][a]);
                const float e = 0.5f * (mx[k][a] - mn[k][a]);
                rr += e * e;
            }
            g.sph[k][3] = sqrtf(rr);
        }
        vAt += g.nVtx;
        gout[gi] = g;
    }
    Mk64System3DSCleanDataCache(mem, bytes);
    gPsResStats3DS[9]++;
    return mem;
}

// Verificacion (compilacion de diagnostico): cada vertice del grupo se calcula por el camino anterior
// (computeVertex, igual que se mandaba al backend) y por la formula del camino residente con los mismos
// uniformes; se comparan posicion de recorte del hueso, coordenadas de textura, color/normal y niebla.
static uint32_t sPsResVerifyLogged3DS = 0;
static void PsResVerify3DS(PsSkinGpu3DS& g, const PsResidentDraw3DS& d, const PsResidentVertex3DS* rv, uint32_t nSegRun, uint32_t nVtxRun,
                           const PsSkinVtx3DS* const* segV, const uint32_t* segN, const uint32_t* bones, uint32_t nBones) {
    (void)bones;
    // offsets del formato del interprete (mismo orden que computeVertex)
    int off = 4, uvOff[2] = { -1, -1 };
    for (int t = 0; t < 2; t++) {
        if (!g.uv[t].on) continue;
        uvOff[t] = off;
        off += 2 + (g.uv[t].writeS ? 1 : 0) + (g.uv[t].writeT ? 1 : 0);
    }
    const int fogOff = g.m.use_fog ? off : -1;
    if (g.m.use_fog) off += 4;
    if (g.m.use_grayscale) off += 4;
    const int inOff = off + d.varyingInput * (3 + (g.alphaIn ? 1 : 0));
    float out[64];
    uint32_t vi = 0;
    for (uint32_t sg = 0; sg < nSegRun; sg++) {
        for (uint32_t i = 0; i < segN[sg]; i++, vi++) {
            const PsSkinVtx3DS& sv = segV[sg][i];
            float w;
            g.computeVertex(sv, out, w);
            const char* why = nullptr;
            const PsResidentVertex3DS& r = rv[vi];
            // posicion: filas del hueso del camino anterior contra las del lugar residente
            const int oldRow = (int)out[3];
            const float* R0 = gPsSkinRows3DS[oldRow];
            const int slot = r.boneRow / kPsSkinRows3DS;
            if (slot < 0 || slot >= d.nBones) why = "hueso";
            for (int row = 0; row < 4 && !why; row++)
                for (int c = 0; c < 4; c++)
                    if (std::fabs(d.bones[slot][row][c] - gPsSkinRows3DS[oldRow + row][c]) > 1e-4f * (1.0f + std::fabs(R0[c]))) { why = "matriz"; break; }
            if (!why && (out[0] != r.x || out[1] != r.y || out[2] != r.z)) why = "posicion";
            for (int t = 0; t < 2 && !why; t++) {
                if (uvOff[t] < 0) continue;
                for (int a = 0; a < 2; a++) {
                    const int k = t * 2 + a;
                    float x = (a ? r.v : r.u) * d.uvK[k] + d.uvO[k];
                    x = std::min(x, d.uvC[k]);
                    x = x * d.uvM[k] + d.uvA[k];
                    const float ref = out[uvOff[t] + a];
                    if (std::fabs(x - ref) > 1e-5f + 1e-5f * std::fabs(ref)) { why = "textura"; break; }
                }
            }
            if (!why) {
                // entrada variable en bytes (como la empaca el backend)
                float shade[4];
                for (int c = 0; c < 4; c++) shade[c] = r.c[c];
                if (d.lit)
                    for (int c = 0; c < 3; c++) shade[c] = (float)((r.c[c] + 128) & 255);
                const int nc = g.alphaIn ? 4 : 3;
                for (int c = 0; c < nc && !why; c++) {
                    const bool fogA = c == 3 && d.fogVtx && d.fogAlpha;
                    if (fogA) continue; // el GPU la calcula (igual que el camino de huesos de siempre)
                    const float mine = shade[c] * d.colorMask[c] + d.color[c];
                    const float ref = std::floor(out[inOff + c] * 255.0f + 0.5f);
                    if (std::fabs(mine - ref) > 0.5f) why = "color";
                }
            }
            if (!why && fogOff >= 0 && !d.fogVtx && !g.m.use_blend_color) {
                const float mine = r.c[3] * d.fogParams[2] + d.fogParams[3];
                const float ref = (out[fogOff + 3] * 255.0f + 0.5f) * (1.0f / 256.0f);
                if (std::fabs(mine - ref) > 1e-5f) why = "niebla";
            }
            gPsResStats3DS[why ? 11 : 10]++;
            if (why && sPsResVerifyLogged3DS < 20) {
                sPsResVerifyLogged3DS++;
                PsLog3DS("[f2] verificacion fallo (%s): vertice %u de %u, huesos %d, luz %d, niebla %d\n", why, (unsigned)vi, (unsigned)nVtxRun,
                         d.nBones, (int)d.lit, (int)d.fogVtx);
            }
        }
    }
    g.resetPalette();
    (void)nBones;
}

// Un segmento por el camino anterior (para los grupos que F2 no puede dibujar): mismo cuerpo que runSegments
static void PsResOldSegment3DS(Interpreter* gfx, PsSkinGpu3DS& g, const PsSkinVtx3DS* v, uint32_t nVtx, const uint16_t* ix, uint32_t nIdx,
                               const uint32_t* bones, uint32_t nBones) {
    if (nIdx >= 3) {
        if (!(gPsSkinMerge3DS && g.active && gfx->mRsp->geometry_mode == g.geom)) g.begin();
        if (g.active) { g.append(v, nVtx, ix, nIdx); return; }
    }
    g.submit();
    g.active = false;
    const uint8_t mvLoad = get_attr(MTX_LOAD);
    int& loadedBone = g.cpuBone;
    for (uint32_t t = 0; t + 2 < nIdx; t += 3) {
        if (ix[t] >= nVtx || ix[t + 1] >= nVtx || ix[t + 2] >= nVtx) continue;
        for (int k = 0; k < 3; k++) {
            const PsSkinVtx3DS& sv = v[ix[t + k]];
            if (sv.bone != loadedBone && sv.bone < nBones && bones[sv.bone] != 0) {
                gfx->GfxSpMatrix(mvLoad, reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(bones[sv.bone])));
                loadedBone = sv.bone;
            }
            gfx->GfxSpVertex(1, k, &sv.v);
        }
        gfx->GfxSpTri1(0, 1, 2, false);
    }
}

// Dibuja un lote PSK2. false: no se dibujo nada y el lote va completo por el camino anterior.
static bool PsResDraw3DS(Interpreter* gfx, const uint32_t* hdr) {
    const uint32_t nBones = hdr[1], nSegs = hdr[2], id = hdr[3];
    const uint32_t* body = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(hdr[4]));
    const uint32_t* bones = hdr + 6;
    RSP* rsp = gfx->mRsp;
    auto& g = sPsSkinGpu3DS;
    const uint32_t key = ((uint32_t)rsp->texture_scaling_factor.s << 16) | rsp->texture_scaling_factor.t;
    const uint8_t* blk = static_cast<const uint8_t*>(PsModelGpuGet3DS(id, key));
    if (!blk) blk = PsResBuild3DS(id, key, body, nSegs, nBones, rsp->texture_scaling_factor.s, rsp->texture_scaling_factor.t);
    if (!blk) { gPsResStats3DS[2]++; return false; }
    const auto* rh = reinterpret_cast<const PsResHeader3DS*>(blk);
    const auto* groups = reinterpret_cast<const PsResGroup3DS*>(blk + sizeof(PsResHeader3DS));
    if (rh->nSegs != nSegs) { gPsResStats3DS[2]++; return false; }

    // filas de cada hueso (una vez por lote) y prueba de cercania al ojo antes de dibujar nada
    static std::vector<float> rowsOf;
    static std::vector<uint8_t> rowsDone;
    rowsOf.resize(size_t(nBones) * 16);
    rowsDone.assign(nBones, 0);
    g.invertY = gfx->mRapi->GetClipParameters().invertY;
    auto rowsFor = [&](uint32_t b) -> float (*)[4] {
        float (*rows)[4] = reinterpret_cast<float (*)[4]>(&rowsOf[size_t(b) * 16]);
        if (!rowsDone[b]) { g.boneRows((int)b, rows); rowsDone[b] = 1; }
        return rows;
    };
    for (uint32_t gi = 0; gi < rh->nGroups; gi++) {
        const PsResGroup3DS& gr = groups[gi];
        for (int k = 0; k < gr.nBones; k++) {
            if (gr.bone[k] >= nBones) { gPsResStats3DS[2]++; return false; }
            const float (*rows)[4] = rowsFor(gr.bone[k]);
            const float* R3 = rows[3];
            const float wc = R3[0] * gr.sph[k][0] + R3[1] * gr.sph[k][1] + R3[2] * gr.sph[k][2] + R3[3];
            const float len = sqrtf(R3[0] * R3[0] + R3[1] * R3[1] + R3[2] * R3[2]);
            if (wc - len * gr.sph[k][3] < PsSkinGpu3DS::kNearW) { gPsResStats3DS[3]++; return false; }
        }
    }

    // segmentos del cuerpo (para el camino anterior de los grupos que no van por F2 y para la verificacion)
    static std::vector<const uint32_t*> segOps;
    static std::vector<const PsSkinVtx3DS*> segV;
    static std::vector<uint32_t> segNOps, segN, segNIdx;
    static std::vector<const uint16_t*> segIx;
    segOps.resize(nSegs); segV.resize(nSegs); segNOps.resize(nSegs); segN.resize(nSegs); segNIdx.resize(nSegs); segIx.resize(nSegs);
    {
        const uint32_t* q = body;
        for (uint32_t sg = 0; sg < nSegs; sg++) {
            segNOps[sg] = q[0]; segN[sg] = q[1]; segNIdx[sg] = q[2];
            segOps[sg] = q + 3;
            q += 3 + segNOps[sg] * 2;
            segV[sg] = reinterpret_cast<const PsSkinVtx3DS*>(q);
            q += segN[sg] * sizeof(PsSkinVtx3DS) / 4;
            segIx[sg] = reinterpret_cast<const uint16_t*>(q);
            q += (segNIdx[sg] * 2 + 3) / 4;
        }
    }

    g.submit(); // lo pendiente del camino anterior va antes
    if (gfx->mBufVboNumTris) gfx->Flush();
    g.resetPalette();
    g.cpuBone = -1;
    g.active = false;
    auto* rapi = static_cast<GfxRenderingAPICitro3D*>(gfx->mRapi);
    // estado de la racha: sigue vivo entre grupos mientras g no cambie de estado (como el envio del camino anterior)
    int runWhy = -1, runVarying = 0;
    const auto* segR = reinterpret_cast<const PsResSeg3DS*>(blk + sizeof(PsResHeader3DS) + rh->nGroups * sizeof(PsResGroup3DS));
    for (uint32_t gi = 0; gi < rh->nGroups; gi++) {
        const PsResGroup3DS& gr = groups[gi];
        if (gr.nBones < 0) {
            // no cabe en un dibujo: todo el grupo por el camino anterior
            gPsResStats3DS[4]++;
            for (uint32_t sg = gr.seg0; sg < gr.seg0 + gr.nSeg; sg++) {
                for (uint32_t o = 0; o < segNOps[sg]; o++) gfx->GfxSpGeometryMode(segOps[sg][o * 2], segOps[sg][o * 2 + 1]);
                PsResOldSegment3DS(gfx, g, segV[sg], segN[sg], segIx[sg], segNIdx[sg], bones, nBones);
            }
            g.submit();
            g.active = false; // el siguiente grupo vuelve a planear su estado
            continue;
        }
        const auto* gVtx = reinterpret_cast<const PsResidentVertex3DS*>(blk + gr.vtxOff);
        const auto* gIdx = reinterpret_cast<const uint16_t*>(blk + gr.idxOff);
        // racha: segmentos seguidos con el mismo estado (se dibujan juntos)
        uint32_t run0 = 0, runN = 0;
        auto drawRun = [&]() {
            if (!runN) return;
            const uint32_t s0 = run0, s1 = run0 + runN;
            runN = 0;
            if (runWhy >= 0) {
                gPsResStats3DS[runWhy]++;
                for (uint32_t sg = s0; sg < s1; sg++) PsResOldSegment3DS(gfx, g, segV[sg], segN[sg], segIx[sg], segNIdx[sg], bones, nBones);
                return;
            }
            if (g.cull == 3) { gPsResStats3DS[12]++; return; } // el N64 no dibuja nada
            uint32_t nIdx = 0, nVtx = 0;
            for (uint32_t sg = s0; sg < s1; sg++) { nIdx += segR[sg].nIdx; nVtx += segR[sg].nVtx; }
            if (nIdx < 3) return;
            g.submit();
            if (gfx->mBufVboNumTris) gfx->Flush();
            PsResidentDraw3DS d;
            d.vertices = gVtx;
            d.indices = gIdx + segR[s0].idx0;
            d.indexCount = nIdx;
            d.varyingInput = runVarying;
            d.nBones = gr.nBones;
            for (int k = 0; k < gr.nBones; k++) {
                const float (*rows)[4] = rowsFor(gr.bone[k]);
                std::memcpy(d.bones[k], rows, sizeof(float) * 16);
                std::memset(d.bones[k][4], 0, sizeof(float) * 8);
                if (g.gpuLit) {
                    const float (*lb)[3] = g.lightFor(gr.bone[k]);
                    for (int i = 0; i < g.nLights && i < 2; i++) { d.bones[k][4 + i][0] = lb[i][0]; d.bones[k][4 + i][1] = lb[i][1]; d.bones[k][4 + i][2] = lb[i][2]; }
                }
            }
            for (int t = 0; t < 2; t++) {
                const auto& q = g.uv[t];
                const int k = t * 2;
                if (!q.on) { d.uvK[k] = d.uvK[k + 1] = 0.0f; d.uvC[k] = d.uvC[k + 1] = 0x1p30f; d.uvM[k] = d.uvM[k + 1] = 1.0f; continue; }
                d.uvK[k] = q.ku; d.uvK[k + 1] = q.kv;
                d.uvO[k] = q.ou; d.uvO[k + 1] = q.ov;
                d.uvC[k] = q.cpuClampS ? q.clampS : 0x1p30f;
                d.uvC[k + 1] = q.cpuClampT ? q.clampT : 0x1p30f;
                d.uvM[k] = q.mu; d.uvM[k + 1] = q.mv;
                d.uvA[k] = q.au; d.uvA[k + 1] = q.av;
            }
            {
                const auto& ip = g.inp[runVarying < 8 ? runVarying : 0];
                for (int c = 0; c < 3; c++) {
                    d.colorMask[c] = ip.shadeRgb ? 1.0f : 0.0f;
                    d.color[c] = ip.shadeRgb ? 0.0f : std::floor(ip.c[c] * 255.0f + 0.5f);
                }
                if (!g.alphaIn) { d.colorMask[3] = 0.0f; d.color[3] = 255.0f; }
                else if (ip.shadeA) { d.colorMask[3] = 1.0f; d.color[3] = 0.0f; }
                else { d.colorMask[3] = 0.0f; d.color[3] = std::floor(ip.c[3] * 255.0f + 0.5f); }
                d.fogAlpha = g.alphaIn && ip.shadeA;
            }
            d.lit = g.gpuLit;
            if (d.lit)
                for (int k = 0; k < 3; k++) for (int c = 0; c < 3; c++) d.lightColor[k][c] = g.litColor[k][c];
            d.fogVtx = g.fogVtx;
            d.fogParams[0] = rsp->fog_mul;
            d.fogParams[1] = rsp->fog_offset;
            if (g.m.use_blend_color) { d.fogParams[2] = 0.0f; d.fogParams[3] = (g.fogC[3] * 255.0f + 0.5f) * (1.0f / 256.0f); }
            else { d.fogParams[2] = 1.0f / 256.0f; d.fogParams[3] = 0.5f / 256.0f; }
            d.cull = g.cull;
            // vertice de material: el primero de la racha calculado por el camino anterior
            float material[64];
            {
                float w;
                g.computeVertex(segV[s0][0], material, w);
                g.resetPalette();
            }
            d.materialVertex = material;
            if (gPsResVerify3DS) PsResVerify3DS(g, d, gVtx + segR[s0].vtx0, s1 - s0, nVtx, &segV[s0], &segN[s0], bones, nBones);
            rapi->DrawPsResident3DS(d);
            gPsResStats3DS[1]++;
        };
        for (uint32_t sg = gr.seg0; sg < gr.seg0 + gr.nSeg; sg++) {
            const uint32_t before = rsp->geometry_mode;
            for (uint32_t o = 0; o < segNOps[sg]; o++) gfx->GfxSpGeometryMode(segOps[sg][o * 2], segOps[sg][o * 2 + 1]);
            if (segNIdx[sg] < 3) continue;
            // misma regla que el camino anterior: estado nuevo solo si cambio el modo de geometria
            if (!(g.active && rsp->geometry_mode == g.geom)) {
                // la racha anterior se cierra con su modo (el camino anterior lo lee al juntar)
                const uint32_t after = rsp->geometry_mode;
                rsp->geometry_mode = before;
                drawRun();
                rsp->geometry_mode = after;
                g.submit();
                g.begin();
                runWhy = -1;
                if (!g.active) runWhy = 8;
                else if (g.texgen) runWhy = 5;
                else if (g.lit && !g.gpuLit) runWhy = 6;
                if (runWhy < 0) {
                    int nShade = 0, first = -1;
                    for (int j = 0; j < g.nIn; j++)
                        if (g.inp[j].shadeRgb || (g.alphaIn && g.inp[j].shadeA)) { nShade++; if (first < 0) first = j; }
                    if (nShade > 1) runWhy = 7;
                    runVarying = g.gpuLit ? g.shadeIn : first >= 0 ? first : 0;
                }
            }
            if (!runN) run0 = sg;
            runN++;
        }
        drawRun();
    }
    g.submit();
    g.active = false;
    // el interprete queda con la matriz del ultimo hueso, como si hubiera corrido los G_MTX originales
    if (nBones > 0 && bones[nBones - 1] != 0)
        gfx->GfxSpMatrix(get_attr(MTX_LOAD), reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(bones[nBones - 1])));
    gPsResStats3DS[0]++;
    return true;
}

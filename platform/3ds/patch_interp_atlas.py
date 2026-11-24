#!/usr/bin/env python3
"""Lado del interprete del atlas de texturas (ver patch_atlas_backend.py).

- Al importar una textura en modo clamp se le pide al backend que la ponga en el atlas.
- Cambiar entre dos texturas de la misma pagina ya no vacia el lote.
- Las coordenadas de textura se escriben ya en coordenadas de pagina (con el clamp hecho en el CPU).
- Las corridas estaticas de MK64 quedan apagadas: guardan coordenadas de la textura suelta.

Uso: patch_interp_atlas.py interprete.cpp   (despues de patch_interp_tex/skin/mat)
"""
import sys

p = sys.argv[1]
s = open(p).read()


def rep(old, new, count=1):
    global s
    if s.count(old) < count:
        sys.exit("patch_interp_atlas: no encontre: " + old[:80])
    s = s.replace(old, new, count)


rep("            sPsBoundTex3DS[i & 7] = it->second.texture_id;\n        }",
    "            sPsBoundTex3DS[i & 7] = it->second.texture_id;\n        }\n        sPsBoundFilt3DS[i & 7] = it->second.linear_filter;")
rep("static uint32_t sPsBoundTex3DS[8]",
    'extern "C" { extern bool gPsAtlasNext3DS; extern bool gPsAtlasNextLinear3DS; extern bool gPsAtlasEnabled3DS;\n'
    '             bool PsAtlasXform3DS(uint32_t id, float* xf, int* page); }\n'
    "static bool sPsBoundFilt3DS[8];\n"
    "extern \"C\" { bool gPsAtlasMerge3DS = true; int gPsUvLog3DS = 0; void PsLog3DS(const char*, ...); }\n"
    "static uint32_t sPsBoundTex3DS[8]")

# misma pagina del atlas: sin vaciar
rep("""        if (sPsBoundTex3DS[i & 7] != it->second.texture_id || *n != &*it) {""",
    """        if (sPsBoundTex3DS[i & 7] != it->second.texture_id || *n != &*it) {
            float psXf3DS[4];
            int psPgA3DS = -1, psPgB3DS = -2;
            const bool psSamePage3DS = gPsAtlasMerge3DS && sPsBoundTex3DS[i & 7] != ~0u &&
                PsAtlasXform3DS(sPsBoundTex3DS[i & 7], psXf3DS, &psPgA3DS) &&
                PsAtlasXform3DS(it->second.texture_id, psXf3DS, &psPgB3DS) && psPgA3DS == psPgB3DS &&
                it->second.linear_filter == ((psPgB3DS & 1) != 0) && sPsBoundFilt3DS[i & 7] == ((psPgA3DS & 1) != 0);""")
rep("""            gPsTexDbg3DS[4] += small && !clampB; gPsTexDbg3DS[5] += i != 0;
            }
            Flush();""", """            gPsTexDbg3DS[4] += small && !clampB; gPsTexDbg3DS[5] += i != 0; gPsTexDbg3DS[6] += psSamePage3DS;
            }
            if (!psSamePage3DS) Flush();""")

# pedir atlas al importar
rep("""                imported3DS = true;
                gfx->ImportTexture(i, tile, false);""", """                imported3DS = true;
                {
                    const uint8_t cms3DS = gfx->mRdp->texture_tile[tile].cms, cmt3DS = gfx->mRdp->texture_tile[tile].cmt;
                    gPsAtlasNext3DS = (cms3DS & G_TX_CLAMP) && (cmt3DS & G_TX_CLAMP) && !(cms3DS & G_TX_MIRROR) &&
                                      !(cmt3DS & G_TX_MIRROR);
                    gPsAtlasNextLinear3DS = (gfx->mRdp->other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT;
                    // clamp con mascara menor que el tile: el N64 repite dentro del tile (modelos): suelta
                    {
                        const auto& tt3DS = gfx->mRdp->texture_tile[tile];
                        const uint32_t tw3DS = (uint32_t)((tt3DS.lrs - tt3DS.uls) / 4) + 1, th3DS = (uint32_t)((tt3DS.lrt - tt3DS.ult) / 4) + 1;
                        if ((tt3DS.masks != 0 && (1u << tt3DS.masks) < tw3DS) || (tt3DS.maskt != 0 && (1u << tt3DS.maskt) < th3DS))
                            gPsAtlasNext3DS = false;
                    }
                    // modelos con luz (Pokemon): sus coordenadas salen del rango de la textura y dependen de la repeticion
                    if (gfx->mRsp->geometry_mode & G_LIGHTING) gPsAtlasNext3DS = false;
                    // con dos texturas a la vez el atlas falla (iconos L/R del menu): sueltas
                    if (comb->usedTextures[0] && comb->usedTextures[1]) gPsAtlasNext3DS = false;
                }
                gfx->ImportTexture(i, tile, false);
                gPsAtlasNext3DS = false;""")

# parametros del muestreador: la pagina ya tiene los suyos
rep("""            if (linear_filter != gfx->mRenderingState.mTextures[i]->second.linear_filter ||""",
    """            float psXfS3DS[4]; int psPgS3DS;
            if (PsAtlasXform3DS(gfx->mRenderingState.mTextures[i]->second.texture_id, psXfS3DS, &psPgS3DS) &&
                linear_filter == ((psPgS3DS & 1) != 0) &&
                gfx->mRenderingState.mTextures[i]->second.linear_filter == linear_filter) {
                gfx->mRenderingState.mTextures[i]->second.linear_filter = linear_filter;
                gfx->mRenderingState.mTextures[i]->second.cms = cms;
                gfx->mRenderingState.mTextures[i]->second.cmt = cmt;
            }
            if (linear_filter != gfx->mRenderingState.mTextures[i]->second.linear_filter ||""")

# triangulos normales: coordenadas de pagina
rep("""        clampS3DS[t] = (tex_width2[t] - 0.5f) * inverseWidth3DS[t];
        clampT3DS[t] = (tex_height2[t] - 0.5f) * inverseHeight3DS[t];
    }""", """        clampS3DS[t] = (tex_width2[t] - 0.5f) * inverseWidth3DS[t];
        clampT3DS[t] = (tex_height2[t] - 0.5f) * inverseHeight3DS[t];
    }
    float psAtlasXf3DS[2][4];
    bool psAtlas3DS[2] = { false, false };
    for (int t = 0; t < 2; ++t) {
        int pg;
        if (usedTextures[t] && mRenderingState.mTextures[t] != nullptr)
            psAtlas3DS[t] = PsAtlasXform3DS(mRenderingState.mTextures[t]->second.texture_id, psAtlasXf3DS[t], &pg);
    }""")
rep("""            mBufVbo[mBufVboLen++] = u * inverseWidth3DS[t];
            mBufVbo[mBufVboLen++] = v * inverseHeight3DS[t];

            bool clampS = tm & (1 << 2 * t);
            bool clampT = tm & (1 << 2 * t + 1);

            if (clampS) {
                mBufVbo[mBufVboLen++] = clampS3DS[t];
            }

            if (clampT) {
                mBufVbo[mBufVboLen++] = clampT3DS[t];
            }""", """            bool clampS = tm & (1 << 2 * t);
            bool clampT = tm & (1 << 2 * t + 1);
            if (psAtlas3DS[t]) {
                float uu = u * inverseWidth3DS[t], vv = v * inverseHeight3DS[t];
                if (gPsUvLog3DS > 0 && is_rect) {
                    gPsUvLog3DS--;
                    PsLog3DS("[uv] x %.3f y %.3f t %d u1 %d a1 %d fl %d id %u tw %u th %u tw2 %u th2 %u tm %u u %.4f v %.4f cs %.4f ct %.4f xf %.5f %.5f %.5f %.5f\\n",
                           mRsp->loaded_vertices[i == 0 ? vtx1_idx : i == 1 ? vtx2_idx : vtx3_idx].x /
                               mRsp->loaded_vertices[i == 0 ? vtx1_idx : i == 1 ? vtx2_idx : vtx3_idx].w,
                           mRsp->loaded_vertices[i == 0 ? vtx1_idx : i == 1 ? vtx2_idx : vtx3_idx].y /
                               mRsp->loaded_vertices[i == 0 ? vtx1_idx : i == 1 ? vtx2_idx : vtx3_idx].w,
                           t, (int)usedTextures[1], (int)psAtlas3DS[1], (int)((mRdp->other_mode_h >> G_MDSFT_TEXTFILT) & 3),
                           (unsigned)mRenderingState.mTextures[t]->second.texture_id, tex_width[t], tex_height[t], tex_width2[t],
                           tex_height2[t], tm, uu, vv, clampS3DS[t], clampT3DS[t], psAtlasXf3DS[t][0], psAtlasXf3DS[t][1],
                           psAtlasXf3DS[t][2], psAtlasXf3DS[t][3]);
                }
                if (clampS && uu > clampS3DS[t]) uu = clampS3DS[t];
                if (clampT && vv > clampT3DS[t]) vv = clampT3DS[t];
                mBufVbo[mBufVboLen++] = uu * psAtlasXf3DS[t][0] + psAtlasXf3DS[t][1];
                mBufVbo[mBufVboLen++] = vv * psAtlasXf3DS[t][2] + psAtlasXf3DS[t][3];
                if (clampS) mBufVbo[mBufVboLen++] = 0x1p30f;
                if (clampT) mBufVbo[mBufVboLen++] = 0x1p30f;
            } else {
            if (gPsUvLog3DS > 0 && !is_rect && usedTextures[0] && usedTextures[1]) {
                gPsUvLog3DS--;
                PsLog3DS("[uv2] t %d tile %u s %d t %d u %.3f v %.3f tw %u th %u tw2 %u th2 %u tm %u id %u\\n", t, (unsigned)uv_tile, (int)v_arr[i]->u, (int)v_arr[i]->v,
                         u, v, tex_width[t], tex_height[t], tex_width2[t], tex_height2[t], tm, (unsigned)mRenderingState.mTextures[t]->second.texture_id);
            }
            mBufVbo[mBufVboLen++] = u * inverseWidth3DS[t];
            mBufVbo[mBufVboLen++] = v * inverseHeight3DS[t];

            if (clampS) {
                mBufVbo[mBufVboLen++] = clampS3DS[t];
            }

            if (clampT) {
                mBufVbo[mBufVboLen++] = clampT3DS[t];
            }
            }""")

# skin: el plan por lote de patch_interp_skin.py ya aplica el atlas (coordenadas de pagina)
rep("""    bool texgen = false, texgenLinear = false;""", """    bool texgen = false, texgenLinear = false;
    bool atlas[2] = { false, false };
    float atlasXf[2][4];""")
rep("""        pointFilter = (rdp->other_mode_h & (3U << G_MDSFT_TEXTFILT)) == G_TF_POINT;""",
    """        pointFilter = (rdp->other_mode_h & (3U << G_MDSFT_TEXTFILT)) == G_TF_POINT;
        for (int t = 0; t < 2; ++t) {
            int pg;
            atlas[t] = m.usedTextures[t] && gfx->mRenderingState.mTextures[t] != nullptr &&
                       PsAtlasXform3DS(gfx->mRenderingState.mTextures[t]->second.texture_id, atlasXf[t], &pg);
        }""")

# corridas estaticas apagadas con el atlas
rep("    sStaticRuns3DS.SetEnabled(Mk64Settings3DSGetStaticMeshes == nullptr || Mk64Settings3DSGetStaticMeshes());",
    "    sStaticRuns3DS.SetEnabled(!gPsAtlasEnabled3DS && (Mk64Settings3DSGetStaticMeshes == nullptr || Mk64Settings3DSGetStaticMeshes()));")

# Rectangulos con textura en modo clamp mucho mas grandes que su textura (los paneles de las ventanas del laboratorio:
# una esquina de 8x8 estirada con clamp para rellenar todo el panel). El clamp por vertice estiraba la textura en todo
# el rectangulo; se parte en franjas: la parte que cae dentro de la textura se dibuja normal y lo que sobra repite el
# texel del borde, como en el N64.
rep("""    mRdp->first_tile_index = tile;

    GfxDrawRectangle(ulx, uly, lrx, lry);
    if (saved_tile != tile) {""", """    mRdp->first_tile_index = tile;

    {
        const auto& tl3DS = mRdp->texture_tile[tile];
        const int tw3DS = (int)GetTileSizeFromCoordinates(tl3DS.uls, tl3DS.lrs), th3DS = (int)GetTileSizeFromCoordinates(tl3DS.ult, tl3DS.lrt);
        const float s0 = uls, t0 = ult;
        const float sMin = tl3DS.uls * 8.0f, sMax = sMin + tw3DS * 32.0f, tMin = tl3DS.ult * 8.0f, tMax = tMin + th3DS * 32.0f;
        const bool splitS = !flip && dsdx > 0 && tl3DS.shifts == 0 && tw3DS > 0 && (tl3DS.cms & G_TX_CLAMP) && !(tl3DS.cms & G_TX_MIRROR) &&
                            (lrs > sMax + 64.0f || s0 < sMin - 64.0f);
        const bool splitT = !flip && dtdy > 0 && tl3DS.shiftt == 0 && th3DS > 0 && (tl3DS.cmt & G_TX_CLAMP) && !(tl3DS.cmt & G_TX_MIRROR) &&
                            (lrt > tMax + 64.0f || t0 < tMin - 64.0f);
        if (splitS || splitT) {
            struct Seg3DS { int32_t a, b; float u0, u1; };
            Seg3DS xs[3], ys[3];
            int nx = 0, ny = 0;
            auto cut = [](Seg3DS* out, int& n, bool split, int32_t p0, int32_t p1, float c0, float c1, int16_t d, float lo, float hi) {
                if (!split) { out[n++] = { p0, p1, c0, c1 }; return; }
                // posicion (10.2) donde la coordenada vale c: p0 + (c - c0) * 128 / d
                int32_t pa = c0 < lo ? p0 + (int32_t)((lo - c0) * 128.0f / d) : p0;
                int32_t pb = c1 > hi ? p0 + (int32_t)((hi - c0) * 128.0f / d) : p1;
                if (pa > p1) pa = p1;
                if (pb > p1) pb = p1;
                if (pb < pa) pb = pa;
                if (pa > p0) out[n++] = { p0, pa, lo + 16.0f, lo + 16.0f };
                if (pb > pa) out[n++] = { pa, pb, c0 < lo ? lo : c0, c1 > hi ? hi : c1 };
                if (p1 > pb) out[n++] = { pb, p1, hi - 16.0f, hi - 16.0f };
            };
            cut(xs, nx, splitS, ulx, lrx, s0, lrs, dsdx, sMin, sMax);
            cut(ys, ny, splitT, uly, lry, t0, lrt, dtdy, tMin, tMax);
            for (int iy = 0; iy < ny; iy++) {
                for (int ix = 0; ix < nx; ix++) {
                    ul->u = xs[ix].u0; ul->v = ys[iy].u0;
                    ll->u = xs[ix].u0; ll->v = ys[iy].u1;
                    lr->u = xs[ix].u1; lr->v = ys[iy].u1;
                    ur->u = xs[ix].u1; ur->v = ys[iy].u0;
                    GfxDrawRectangle(xs[ix].a, ys[iy].a, xs[ix].b, ys[iy].b);
                }
            }
        } else {
            GfxDrawRectangle(ulx, uly, lrx, lry);
        }
    }
    if (saved_tile != tile) {""")

open(p, "w").write(s)
print("patch_interp_atlas: ok")

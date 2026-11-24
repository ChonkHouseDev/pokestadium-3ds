// F2: dibujo residente de un segmento de un modelo con huesos (se incluye al final de gfx_citro3d.cpp de MK64;
// ver patch_citro3d.py). Mismo programa de vertices que el resto (rama residentEnabled): no hay cambio de
// programa, solo de formato de vertices, y el material se arma con el mismo ApplyCombinerState.
#include "ps_resident.h"

// dibujos, triangulos, rechazos (sin programa o estado)
extern "C" uint64_t gPsResBackend3DS[4];
uint64_t gPsResBackend3DS[4];
namespace Fast {

void GfxRenderingAPICitro3D::DrawPsResident3DS(const PsResidentDraw3DS& d) {
    ShaderProgram* program = mImpl->currentProgram;
    if (!mImpl->frameActive || program == nullptr || program->invisible || d.vertices == nullptr || d.indices == nullptr ||
        d.indexCount < 3 || d.materialVertex == nullptr || program->strideFloats == 0 ||
        program->strideFloats > kMaxVertexStrideFloats || mImpl->psResidentUniform < 0 || mImpl->psSkinBonesUniform < 0 ||
        d.nBones <= 0 || d.nBones > kPsResMaxBones3DS) {
        gPsResBackend3DS[2]++;
        return;
    }
    if (!DrawTargetVisible()) return;
    RequireGpuCommandRoom(mk64_3ds::kGpuCommandTailWords + mk64_3ds::kGpuCommandDrawWords);

    // Material: las constantes, los recortes, la niebla y el gris salen del vertice de material en el formato del
    // interprete, igual que DrawTriangles los saca del primer vertice del lote
    const float* material = d.materialVertex;
    std::array<std::array<float, 4>, 7> constants = {};
    for (uint8_t input = 0; input < program->numInputs; ++input) {
        const uint8_t inputOffset = program->inputOffsets[input];
        for (int component = 0; component < 4; ++component)
            constants[input][component] = component == 3 && !program->alpha ? 1.0f : material[inputOffset + component];
    }
    const int varyingInput = program->numInputs == 1 ? 0 : (d.varyingInput < program->numInputs ? d.varyingInput : 0);
    const std::array<float, 4> uvScale = SelectedTextureScale(program, 1.0f);
    std::array<float, 4> uvClamp = {0x1p30f, 0x1p30f, 0x1p30f, 0x1p30f};
    for (int texture = 0; texture < 2; ++texture) {
        if (!program->usedTextures[texture]) continue;
        uint8_t offset = program->textureOffsets[texture] + 2;
        for (int axis = 0; axis < 2; ++axis)
            if (program->clamp[texture][axis]) uvClamp[texture * 2 + axis] = material[offset++];
    }
    if (!mImpl->textureTransformValid || mImpl->textureScaleState != uvScale || mImpl->textureClampState != uvClamp) {
        C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->textureScaleUniform, uvScale[0], uvScale[1], uvScale[2], uvScale[3]);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->textureClampUniform, uvClamp[0], uvClamp[1], uvClamp[2], uvClamp[3]);
        mImpl->textureScaleState = uvScale;
        mImpl->textureClampState = uvClamp;
        mImpl->textureTransformValid = true;
    }
    std::array<float, 4> grayscaleColor = { 1.0f, 1.0f, 1.0f, 0.0f };
    if (program->grayscale) std::copy_n(material + program->grayscaleOffset, grayscaleColor.size(), grayscaleColor.begin());
    const std::array<float, 4> fogColor = program->fog
        ? std::array<float, 4>{material[program->fogOffset], material[program->fogOffset + 1],
                              material[program->fogOffset + 2], material[program->fogOffset + 3]}
        : std::array<float, 4>{};
    // como los lotes de huesos de siempre: sin niebla de distancia (su w no pasa por el CPU)
    ApplyCombinerState(program, varyingInput, constants, grayscaleColor, false, fogColor);

    // Programa de vertices: rama de huesos + rama residente
    auto* rows = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, mImpl->psSkinBonesUniform, d.nBones * 6);
    for (int b = 0; b < d.nBones; ++b)
        for (int r = 0; r < 6; ++r) {
            rows[b * 6 + r].x = d.bones[b][r][0]; rows[b * 6 + r].y = d.bones[b][r][1];
            rows[b * 6 + r].z = d.bones[b][r][2]; rows[b * 6 + r].w = d.bones[b][r][3];
        }
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->nativeGeometryUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->nativeLightingUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->staticMeshUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinUniform, true);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psResidentUniform, true);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinLitUniform, d.lit);
    if (d.lit) {
        auto* lc = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, mImpl->psSkinLightColorUniform, 3);
        for (int r = 0; r < 3; ++r) { lc[r].x = d.lightColor[r][0]; lc[r].y = d.lightColor[r][1]; lc[r].z = d.lightColor[r][2]; lc[r].w = 0.0f; }
    }
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinFogUniform, d.fogVtx);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinFogAlphaUniform, d.fogVtx && d.fogAlpha);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinFogParamsUniform, d.fogParams[0], d.fogParams[1], d.fogParams[2], d.fogParams[3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->staticUvUniform, d.uvK[0], d.uvK[1], d.uvK[2], d.uvK[3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->staticUvUniform + 1, d.uvO[0], d.uvO[1], d.uvO[2], d.uvO[3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->nativeMatrixUniform, d.uvC[0], d.uvC[1], d.uvC[2], d.uvC[3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->nativeMatrixUniform + 1, d.uvM[0], d.uvM[1], d.uvM[2], d.uvM[3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->nativeMatrixUniform + 2, d.uvA[0], d.uvA[1], d.uvA[2], d.uvA[3]);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->staticConstantColorUniform, true);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->staticColorUniform, d.color[0], d.color[1], d.color[2], d.color[3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->staticColorMaskUniform, d.colorMask[0], d.colorMask[1], d.colorMask[2], d.colorMask[3]);
    C3D_CullFace(d.cull == 1 ? GPU_CULL_FRONT_CCW : d.cull == 2 ? GPU_CULL_BACK_CCW : GPU_CULL_NONE);

    // Formato residente: short4 posicion+fila, short2 U,V, ubyte4 color/normal (registros v0, v1, v3)
    C3D_AttrInfo saved = *C3D_GetAttrInfo();
    C3D_AttrInfo attributes;
    AttrInfo_Init(&attributes);
    AttrInfo_AddLoader(&attributes, 0, GPU_SHORT, 4);
    AttrInfo_AddLoader(&attributes, 1, GPU_SHORT, 2);
    AttrInfo_AddLoader(&attributes, 3, GPU_UNSIGNED_BYTE, 4);
    C3D_SetAttrInfo(&attributes);
    C3D_BufInfo* bufferInfo = C3D_GetBufInfo();
    BufInfo_Init(bufferInfo);
    BufInfo_Add(bufferInfo, d.vertices, sizeof(PsResidentVertex3DS), 3, 0x210);
    C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(d.indexCount), C3D_UNSIGNED_SHORT, d.indices);
    C3D_SetAttrInfo(&saved);

    // todo lo que se toco queda para que el siguiente dibujo lo vuelva a poner
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psResidentUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinLitUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinFogUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinFogAlphaUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->staticConstantColorUniform, false);
    mImpl->staticVertexStateValid = false;
    mImpl->nativeVertexStateDirty = true;
    mImpl->packedBufferBound = false;
    ++mImpl->drawCallCount;
    mImpl->triangleCount += d.indexCount / 3;
    gPsResBackend3DS[0]++;
    gPsResBackend3DS[1] += d.indexCount / 3;
}

// ---------------- Capa del HUD (ver ps_hud_interp.inc) ----------------
// Destino aparte con la misma forma que la pantalla de arriba (240x400 fisicos dentro de una textura 256x512):
// mientras se captura, el backend lo trata como la pantalla (mismos viewport, recorte y proyeccion), asi cada
// pixel cae en el mismo lugar. El alfa de la capa guarda cuanto se ve de lo que hay detras (1 = todo): empieza
// en 1, un dibujo opaco lo deja en 0 y uno mezclado lo multiplica por (1 - alfa) (ver SetUseAlpha). Al componer:
// pantalla = capa.rgb + pantalla * capa.a, que da lo mismo que haber dibujado directo sobre la pantalla.
static constexpr int kPsLayers3DS = 2;
static C3D_Tex sPsLayerTexs3DS[kPsLayers3DS];
static C3D_RenderTarget* sPsLayerTargets3DS[kPsLayers3DS] = {};
static bool sPsLayerFaileds3DS[kPsLayers3DS] = {};
extern "C" uint32_t gPsHudStats3DS[8];
#define sPsLayerCapturing3DS gPsLayerCapturing3DS
static C3D_RenderTarget *sPsLayerSavedTop3DS = nullptr, *sPsLayerSavedGame3DS = nullptr;
static PackedVertex* sPsLayerVerts3DS = nullptr;

bool GfxRenderingAPICitro3D::PsLayerBegin3DS(int layer) {
    if (layer < 0 || layer >= kPsLayers3DS) return false;
    C3D_Tex& sPsLayerTex3DS = sPsLayerTexs3DS[layer];
    C3D_RenderTarget*& sPsLayerTarget3DS = sPsLayerTargets3DS[layer];
    bool& sPsLayerFailed3DS = sPsLayerFaileds3DS[layer];
    if (!mImpl->frameActive || sPsLayerCapturing3DS || sPsLayerFailed3DS || mImpl->gameTarget == nullptr ||
        mImpl->gameTarget != mImpl->topTarget || mImpl->activeTarget != mImpl->gameTarget || mImpl->outputWidth != kTopLogicalWidth)
        return false;
    if (sPsLayerTarget3DS == nullptr) {
        // solo si sobra memoria de video y lineal: si no, el HUD se sigue dibujando como siempre
        gPsHudStats3DS[6] = vramSpaceFree() / 1024;
        if (vramSpaceFree() < 3u * 512u * 1024u || linearSpaceFree() < 5u * 1024u * 1024u ||
            !C3D_TexInitVRAM(&sPsLayerTex3DS, 256, 512, GPU_RGBA8)) { sPsLayerFailed3DS = true; return false; }
        sPsLayerTarget3DS = C3D_RenderTargetCreateFromTex(&sPsLayerTex3DS, GPU_TEXFACE_2D, 0, C3D_DEPTHTYPE(GPU_RB_DEPTH24_STENCIL8));
        if (sPsLayerVerts3DS == nullptr) sPsLayerVerts3DS = static_cast<PackedVertex*>(linearAlloc(6 * sizeof(PackedVertex)));
        if (sPsLayerTarget3DS == nullptr || sPsLayerVerts3DS == nullptr) {
            if (sPsLayerTarget3DS != nullptr) C3D_RenderTargetDelete(sPsLayerTarget3DS);
            sPsLayerTarget3DS = nullptr;
            C3D_TexDelete(&sPsLayerTex3DS);
            sPsLayerFailed3DS = true;
            return false;
        }
        C3D_TexSetFilter(&sPsLayerTex3DS, GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&sPsLayerTex3DS, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        // identidad: (x, y) de recorte directo a la pantalla fisica; u, v = pixel / tamano de la textura
        static const float kPos[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };
        for (int i = 0; i < 6; ++i) {
            PackedVertex& v = sPsLayerVerts3DS[i];
            v.position[0] = kPos[i][0]; v.position[1] = kPos[i][1]; v.position[2] = 0.0f; v.position[3] = 1.0f;
            v.texcoord0[0] = (kPos[i][0] * 0.5f + 0.5f) * (240.0f / 256.0f);
            v.texcoord0[1] = (kPos[i][1] * 0.5f + 0.5f) * (400.0f / 512.0f);
            v.texcoord1[0] = v.texcoord1[1] = 0.0f;
            v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
            v.clipDepth = -0.5f;
        }
    }
    FlushPackedVertices();
    C3D_FrameSplit(GX_CMDLIST_FLUSH);
    C3D_FrameDrawOn(sPsLayerTarget3DS);
    C3D_RenderTargetClear(sPsLayerTarget3DS, C3D_CLEAR_ALL, 0x000000FF, 0);
    sPsLayerSavedTop3DS = mImpl->topTarget;
    sPsLayerSavedGame3DS = mImpl->gameTarget;
    mImpl->topTarget = mImpl->gameTarget = mImpl->activeTarget = sPsLayerTarget3DS;
    sPsLayerCapturing3DS = true;
    SetUseAlpha(mImpl->useAlpha);
    UploadProjectionForActiveTarget();
    return true;
}

void GfxRenderingAPICitro3D::PsLayerEnd3DS() {
    if (!sPsLayerCapturing3DS) return;
    FlushPackedVertices();
    C3D_FrameSplit(GX_CMDLIST_FLUSH);
    mImpl->topTarget = sPsLayerSavedTop3DS;
    mImpl->gameTarget = sPsLayerSavedGame3DS;
    sPsLayerCapturing3DS = false;
    C3D_FrameDrawOn(mImpl->gameTarget);
    mImpl->activeTarget = mImpl->gameTarget;
    SetUseAlpha(mImpl->useAlpha);
    UploadProjectionForActiveTarget();
}

void GfxRenderingAPICitro3D::PsLayerComposite3DS(int layer) {
    if (layer < 0 || layer >= kPsLayers3DS) return;
    C3D_Tex& sPsLayerTex3DS = sPsLayerTexs3DS[layer];
    C3D_RenderTarget* sPsLayerTarget3DS = sPsLayerTargets3DS[layer];
    if (!mImpl->frameActive || sPsLayerTarget3DS == nullptr || sPsLayerCapturing3DS || mImpl->activeTarget != mImpl->topTarget) return;
    FlushPackedVertices();
    RequireGpuCommandRoom(mk64_3ds::kGpuCommandTailWords + mk64_3ds::kGpuCommandDrawWords);
    C3D_Mtx identity;
    Mtx_Identity(&identity);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mImpl->projectionUniform, &identity);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->nativeGeometryUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->nativeLightingUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->staticMeshUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->staticConstantColorUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->originalFogUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinUniform, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psResidentUniform, false);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->textureScaleUniform, 1.0f, 1.0f, 1.0f, 1.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->textureClampUniform, 0x1p30f, 0x1p30f, 0x1p30f, 0x1p30f);
    C3D_SetViewport(0, 0, 240, 400);
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthMap(true, -1.0f, 0.0f);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_SRC_ALPHA, GPU_ZERO, GPU_ONE);
    C3D_TexBind(0, &sPsLayerTex3DS);
    C3D_TexBind(1, nullptr); C3D_TexBind(2, nullptr); C3D_ProcTexBind(0, nullptr);
    C3D_TexEnv* environment = C3D_GetTexEnv(0);
    C3D_TexEnvInit(environment);
    C3D_TexEnvSrc(environment, C3D_Both, GPU_TEXTURE0, GPU_TEXTURE0, GPU_TEXTURE0);
    C3D_TexEnvFunc(environment, C3D_Both, GPU_REPLACE);
    for (int stage = 1; stage < 6; ++stage) C3D_TexEnvInit(C3D_GetTexEnv(stage));
    C3D_AttrInfo saved = *C3D_GetAttrInfo();
    C3D_AttrInfo attributes;
    AttrInfo_Init(&attributes);
    AttrInfo_AddLoader(&attributes, 0, GPU_FLOAT, 4);
    AttrInfo_AddLoader(&attributes, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(&attributes, 2, GPU_FLOAT, 2);
    AttrInfo_AddLoader(&attributes, 3, GPU_UNSIGNED_BYTE, 4);
    AttrInfo_AddLoader(&attributes, 4, GPU_FLOAT, 1);
    C3D_SetAttrInfo(&attributes);
    C3D_BufInfo* bufferInfo = C3D_GetBufInfo();
    BufInfo_Init(bufferInfo);
    BufInfo_Add(bufferInfo, sPsLayerVerts3DS, sizeof(PackedVertex), 5, 0x43210);
    C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
    C3D_SetAttrInfo(&saved);
    // todo lo que se toco lo vuelve a poner el siguiente dibujo (el interprete reenvia viewport, recorte y z)
    SetUseAlpha(mImpl->useAlpha);
    UploadProjectionForActiveTarget();
    mImpl->tevStateValid = false;
    mImpl->textureTransformValid = false;
    mImpl->staticVertexStateValid = false;
    mImpl->nativeVertexStateDirty = true;
    mImpl->packedBufferBound = false;
    ++mImpl->drawCallCount;
    mImpl->triangleCount += 2;
}
} // namespace Fast

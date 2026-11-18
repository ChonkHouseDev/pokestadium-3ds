#!/usr/bin/env python3
"""Comando G_PS_SKIN (0x41) en el interprete: dibuja un lote de un modelo con huesos armado por gfx_convert.

Formato del lote (palabras nativas): 'PSSK', nBones, nSegs, bones[nBones] (apuntadores a matrices fijas
convertidas), y por segmento: nOps, nVtx, ops[2*nOps] (clear, set de G_GEOMETRYMODE), vertices[nVtx]
(F3DVtx nativo de 16 bytes + hueso + 7 de relleno = 24; tres por triangulo).

Modo CPU (PS_SKIN_GPU no definido): cada vertice se carga con la matriz de su hueso por el camino normal
del interprete. Sirve para verificar que el agrupado da la misma imagen.

Uso: patch_interp_skin.py interprete.cpp
"""
import sys

p = sys.argv[1]
s = open(p).read()


def rep(old, new):
    global s
    if old not in s:
        sys.exit("patch_interp_skin: no encontre: " + old[:80])
    s = s.replace(old, new, 1)


handler = r'''
// ---------------- G_PS_SKIN: lotes de modelos con huesos (Stadium) ----------------
struct PsSkinVtx3DS { F3DVtx v; uint8_t bone; uint8_t pad[3]; };
static_assert(sizeof(PsSkinVtx3DS) == 24, "PsSkinVtx3DS");
extern "C" uint64_t gPsSkinStats3DS[10]; // lotes, triangulos, segmentos, cambios de matriz, rechazos (texgen, lod, zrango, niebla, luces), gpu
uint64_t gPsSkinStats3DS[10];


// Camino GPU: el material lo pone la misma funcion que usan los triangulos normales; los vertices van en
// coordenadas de objeto con el hueso en w y el shader los transforma con la tabla de matrices.
extern "C" { extern bool gPsSkinActive3DS; extern float gPsSkinRows3DS[66][4]; extern int gPsSkinRowCount3DS; extern int gPsSkinCull3DS;
              extern bool gPsSkinLit3DS; extern float gPsSkinLightColor3DS[3][4]; extern int gPsSkinVarying3DS; }
// Por hueso 6 filas en el GPU: 4 de MV*P y la direccion de las 2 luces en el espacio del hueso; caben 11 huesos por envio
static constexpr int kPsSkinRows3DS = 6, kPsSkinMaxSlots3DS = 11;
static bool gPsSkinGpuLit3DS = true; // luz en el GPU (luzcpu.txt la regresa al CPU)
// Mallas retenidas (ver patch_citro3d.py): los vertices empacados de cada envio de un lote se guardan la primera
// vez; en los cuadros siguientes solo se calculan las matrices de los huesos y se dibuja del buffer guardado.
extern "C" { extern void* gPsRetainCaptureDst3DS; extern size_t gPsRetainCaptureMax3DS; extern size_t gPsRetainCaptured3DS;
             extern const void* gPsRetainBuf3DS; extern size_t gPsRetainCount3DS;
             extern bool gPsRetainNoDraw3DS; extern const uint16_t* gPsRetainIdx3DS; extern size_t gPsRetainIdxCount3DS;
             extern int gPsRetainVaryingOut3DS, gPsRetainVaryingIn3DS; extern int gPsSkinFog3DS; extern float gPsSkinFogParams3DS[2];
             void* PsRetainAlloc3DS(size_t bytes); void PsRetainFree3DS(void* p); }
static bool gPsRetain3DS = true; // sinretener.txt lo apaga
static int gPsRetDbg3DS = 40;
static int gPsRetainMinTris3DS = 16;
static bool gPsRetainFogGpu3DS = true; // niebla por vertice en el GPU para las mallas retenidas (nieblacpu.txt la apaga)
extern "C" void PsSkinSetRetainFog3DS(bool on) { gPsRetainFogGpu3DS = on; }
extern "C" void PsSkinSetRetainMin3DS(int n) { gPsRetainMinTris3DS = n; }
extern "C" void PsSkinSetRetain3DS(bool on) { gPsRetain3DS = on; }
extern "C" uint64_t gPsRetainStats3DS[6];
extern "C" uint64_t gPsRetainWhy3DS[8]; // por que no se pudo capturar: niebla, texgen, luz CPU, camino CPU, detras del ojo, cerca del ojo, copia, memoria
uint64_t gPsRetainWhy3DS[8]; // envios dibujados retenidos, capturados, lotes retenidos, fallas de captura, cerca del ojo, bytes
uint64_t gPsRetainStats3DS[6];
// lotes de piel que NO se dibujaron retenidos, por que: 0 chico (pocos tri), 1 apagado/sin GPU, 2 sin lugar,
// 3 captura fallo ahora, 4 esperando tras falla, 5 la repeticion se rindio, 6 capturado este cuadro
extern "C" uint64_t gPsRetainSkip3DS[8];
uint64_t gPsRetainSkip3DS[8];
static uint32_t gPsRetainFrame3DS = 0;
extern "C" void PsSkinSetGpuLit3DS(bool on) { gPsSkinGpuLit3DS = on; }
static bool gPsSkinGpuEnabled3DS = true;
extern "C" void PsSkinSetGpu3DS(bool on) { gPsSkinGpuEnabled3DS = on; }
static bool gPsSkinMerge3DS = true;
extern "C" void PsSkinSetMerge3DS(bool on) { gPsSkinMerge3DS = on; }
static int gPsSkinLo3DS = 0, gPsSkinHi3DS = 1 << 30, gPsSkinBlock3DS = 0;
extern "C" void PsSkinSetRange3DS(int lo, int hi) { gPsSkinLo3DS = lo; gPsSkinHi3DS = hi; }
static void PsRetainEvict3DS();
extern "C" void PsSkinFrame3DS() { gPsSkinBlock3DS = 0; gPsRetainFrame3DS++; if ((gPsRetainFrame3DS & 63) == 0) PsRetainEvict3DS(); }

extern "C" { int gPsSkinFogLog3DS = 0; int gPsSkinKindLog3DS = 0; void PsLog3DS(const char*, ...); }
struct PsSkinGpu3DS {
    Interpreter* gfx = nullptr;
    const uint32_t* bones = nullptr;
    uint32_t nBones = 0;
    bool active = false;
    uint32_t geom = 0;
    TriangleMaterial3DS m;
    int cull = 0, nLights = 0;
    bool lit = false, pointFilter = false, invertY = false;
    float inverseWidth[2] = {}, inverseHeight[2] = {}, clampS[2] = {}, clampT[2] = {};
    int slotOf[256];
    bool slotOfUsed = true; // la tabla se limpia cientos de veces por cuadro: solo si alguien la toco
    void clearSlotOf() { if (!slotOfUsed) return; slotOfUsed = false; for (int& x : slotOf) x = -1; }
    int slots = 0;
    float light[256][7][3];
    float look[256][2][3];
    bool texgen = false, texgenLinear = false;
    struct UvPlan { bool on, writeS, writeT, cpuClampS, cpuClampT; float ku, ou, kv, ov, mu, au, mv, av, clampS, clampT, floatS, floatT; };
    struct InPlan { bool shadeRgb, shadeA; float c[4]; };
    UvPlan uv[2];
    InPlan inp[8];
    int nIn = 0;
    bool alphaIn = false, fogVtx = false;
    float fogC[4], grayC[4];
    uint32_t lightValid[8] = {};

    int pendingCull = 0;
    bool gpuLit = false, pendingLit = false, prevGpuLit = false, sigChanged = false;
    int shadeIn = -1;
    float litColor[3][3] = {};
    uint8_t constSig[40] = {};
    float sentinelRows[4][4];
    int sentinelSlot = -1;
    // por hueso del envio: de que hueso del lote es y la caja de sus vertices (para saber si queda cerca del ojo)
    int slotBone[kPsSkinMaxSlots3DS + 2];
    float slotMin[kPsSkinMaxSlots3DS + 2][3], slotMax[kPsSkinMaxSlots3DS + 2][3];
    // captura de mallas retenidas
    // pos: x, y, z, lugar del hueso de cada vertice del buffer (3 por triangulo); src: segmento y sus 3 indices
    struct RetDraw { uint32_t seg; uint64_t key; int nSlots; int slotBone[kPsSkinMaxSlots3DS]; float sph[kPsSkinMaxSlots3DS][4];
                     PsSkinVtx3DS tmpl[3]; void* buf; size_t nVtx; std::vector<float> pos; std::vector<uint16_t> src; int varying; };
    std::vector<float> batchPos;
    std::vector<uint16_t> batchSrc;
    bool capturing = false, captureFail = false;
    uint32_t curSeg = 0, batchSeg = 0;
    uint64_t batchKey = 0;
    PsSkinVtx3DS batchTmpl[3];
    std::vector<RetDraw> capDraws;
    uint64_t matKey() const {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](const void* d, size_t n) { const uint8_t* c = static_cast<const uint8_t*>(d); for (size_t i = 0; i < n; i++) { h ^= c[i]; h *= 1099511628211ull; } };
        mix(&m.comb, sizeof m.comb); mix(&m.numInputs, sizeof m.numInputs); mix(m.usedTextures, sizeof m.usedTextures);
        for (int t = 0; t < 2; t++) if (m.usedTextures[t] && gfx->mRenderingState.mTextures[t] != nullptr) mix(&gfx->mRenderingState.mTextures[t]->second.texture_id, 4);
        mix(uv, sizeof uv);
        for (int j = 0; j < nIn && j < 8; j++) { mix(&inp[j].shadeRgb, 1); mix(&inp[j].shadeA, 1); }
        const uint8_t f[12] = { (uint8_t)alphaIn, (uint8_t)fogVtx, (uint8_t)m.use_fog, (uint8_t)m.use_grayscale, (uint8_t)gpuLit, (uint8_t)shadeIn,
                                (uint8_t)cull, (uint8_t)lit, (uint8_t)texgen, (uint8_t)invertY, (uint8_t)nIn, (uint8_t)m.use_blend_color };
        mix(f, sizeof f);
        return h;
    }
    // un envio retenible: sin niebla por vertice ni texgen, y la luz (si hay) en el GPU
    bool retainableState() const { return (!fogVtx || gPsRetainFogGpu3DS) && !texgen && (!lit || gpuLit); }
    // el lote pendiente se dibuja en el siguiente Flush() del interprete (cambio de estado o triangulo normal)
    void submit() {
        if (sPsSkinPending3DS && gfx != nullptr) gfx->Flush();
        // sin triangulos pendientes Flush no pasa por drawPending: la tabla de huesos se vacia aqui (si no,
        // los huesos de triangulos descartados la llenaban y se escribia fuera de gPsSkinRows3DS, encima del
        // contexto de citro3d: C3D_GetTexEnv regresaba NULL y truena en ApplyCombinerState)
        else if (!sPsSkinPending3DS) { slots = 0; sentinelSlot = -1; clearSlotOf(); palEpoch++; }
    }
    // lo llama Interpreter::Flush antes de dibujar
    void drawPending() {
        sPsSkinPending3DS = false;
        if (gfx == nullptr || gfx->mBufVboNumTris == 0) { slots = 0; sentinelSlot = -1; clearSlotOf(); palEpoch++; return; }
        void* capBuf = nullptr;
        const size_t capVtx = size_t(gfx->mBufVboNumTris) * 3;
        const bool noDraw = capturing && gPsRetainBuf3DS == nullptr;
        if (capturing && !captureFail && gPsRetainBuf3DS == nullptr) {
            capBuf = PsRetainAlloc3DS(capVtx * 40);
            if (!capBuf) { captureFail = true; gPsRetainWhy3DS[7]++; }
            gPsRetainCaptureDst3DS = capBuf; gPsRetainCaptureMax3DS = capVtx; gPsRetainCaptured3DS = 0; gPsRetainVaryingOut3DS = -2;
        }
        gPsSkinActive3DS = true;
        gPsSkinRowCount3DS = slots * kPsSkinRows3DS;
        gPsSkinCull3DS = pendingCull;
        gPsSkinLit3DS = pendingLit;
        gPsSkinVarying3DS = pendingLit ? shadeIn : -1;
        if (pendingLit)
            for (int k = 0; k < 3; k++) { gPsSkinLightColor3DS[k][0] = litColor[k][0]; gPsSkinLightColor3DS[k][1] = litColor[k][1]; gPsSkinLightColor3DS[k][2] = litColor[k][2]; }
        gPsRetainNoDraw3DS = noDraw;
        gfx->Flush();
        gPsRetainNoDraw3DS = false;
        gPsSkinActive3DS = false;
        gPsSkinLit3DS = false;
        if (capBuf) {
            gPsRetainCaptureDst3DS = nullptr;
            if (gPsRetainCaptured3DS != capVtx || slots > kPsSkinMaxSlots3DS) { PsRetainFree3DS(capBuf); captureFail = true; gPsRetainWhy3DS[6]++; }
            else {
                RetDraw d{};
                d.seg = batchSeg; d.key = batchKey; d.nSlots = slots; d.buf = capBuf; d.nVtx = capVtx; d.varying = gPsRetainVaryingOut3DS;
                if (d.varying < -1) { PsRetainFree3DS(capBuf); captureFail = true; gPsRetainWhy3DS[6]++; capBuf = nullptr; }
                std::memcpy(d.tmpl, batchTmpl, sizeof d.tmpl);
                d.pos = batchPos;
                d.src = batchSrc;
                if (capBuf && (d.pos.size() != capVtx * 4 || d.src.size() != capVtx / 3 * 4)) { PsRetainFree3DS(capBuf); captureFail = true; gPsRetainWhy3DS[6]++; capBuf = nullptr; }
                for (int k = 0; capBuf && k < slots; k++) {
                    d.slotBone[k] = slotBone[k];
                    float rr = 0.0f;
                    for (int a = 0; a < 3; a++) {
                        d.sph[k][a] = 0.5f * (slotMin[k][a] + slotMax[k][a]);
                        const float e = 0.5f * (slotMax[k][a] - slotMin[k][a]);
                        rr += e * e;
                    }
                    d.sph[k][3] = sqrtf(rr);
                }
                if (capBuf) {
                    capDraws.push_back(std::move(d));
                    gPsRetainStats3DS[1]++;
                    gPsRetainStats3DS[5] += capVtx * 40;
                }
            }
        }
        batchPos.clear();
        batchSrc.clear();
        slots = 0;
        sentinelSlot = -1;
        clearSlotOf();
        palEpoch++;
    }
    void resetPalette() { clearSlotOf(); if (!sPsSkinPending3DS) { slots = 0; sentinelSlot = -1; } palEpoch++; }

    // Pone el material del segmento actual; false si este estado lo tiene que dibujar el CPU
    bool begin() {
        RSP* rsp = gfx->mRsp;
        RDP* rdp = gfx->mRdp;
        active = false;
        if (!gPsSkinGpuEnabled3DS) return false;
        if (rsp->geometry_mode & G_LIGHTING_POSITIONAL) { gPsSkinStats3DS[4]++; return false; }
        struct GfxClipParameters clip = gfx->mRapi->GetClipParameters();
        if (clip.z_is_from_0_to_1) { gPsSkinStats3DS[6]++; return false; }
        if (sPsSkinPending3DS && invertY != clip.invertY) submit();
        // el backend solo tiene una entrada variable por vertice: colores constantes distintos (prim/env de cada
        // globo, por ejemplo) no pueden ir en el mismo envio
        {
            uint8_t sig[40] = {};
            std::memcpy(sig, &rdp->prim_color, 4); std::memcpy(sig + 4, &rdp->env_color, 4);
            std::memcpy(sig + 8, &rdp->fog_color, 4); std::memcpy(sig + 12, &rdp->blend_color, 4);
            std::memcpy(sig + 16, &rdp->grayscale_color, 4);
            sig[20] = rdp->prim_lod_fraction; sig[21] = rdp->convert_k[4]; sig[22] = rdp->convert_k[5]; sig[23] = 0;
            // con la luz en el GPU, la luz (direccion y colores) es del envio entero
            if (rsp->geometry_mode & G_LIGHTING) {
                const int nl = rsp->current_num_lights - 1;
                sig[23] = 1;
                if (nl >= 0 && nl <= 7) {
                    std::memcpy(sig + 24, rsp->current_lights[nl].l.col, 3);
                    if (nl >= 1) { std::memcpy(sig + 27, rsp->current_lights[0].l.col, 3); std::memcpy(sig + 30, rsp->current_lights[0].l.dir, 3); }
                    if (nl >= 2) { std::memcpy(sig + 34, rsp->current_lights[1].l.col, 3); std::memcpy(sig + 37, rsp->current_lights[1].l.dir, 3); }
                    sig[33] = (uint8_t)nl;
                }
            }
            sigChanged = std::memcmp(sig, constSig, sizeof sig) != 0;
            if (sPsSkinPending3DS && sigChanged) submit();
            std::memcpy(constSig, sig, sizeof sig);
        }
        invertY = clip.invertY;
        auto* nr = static_cast<GfxRenderingAPICitro3D*>(gfx->mRapi);
        // Triangulos del CPU que siguen sin dibujar se armaron para el estado nativo de ahora (posiciones de objeto
        // y su matriz en el GPU): hay que dibujarlos antes de apagarlo. Sin esto, una malla del CPU seguida de un
        // lote de huesos salia con sus coordenadas de objeto como si fueran de pantalla (el anillo de Earthquake
        // cubria media pantalla).
        if ((!nr->NativeGeometryMatches(nullptr) || !nr->NativeLightingMatches(nullptr)) && !sPsSkinPending3DS && gfx->mBufVboLen > 0)
            gfx->Flush();
        if (!nr->NativeGeometryMatches(nullptr)) nr->SetNativeGeometry(nullptr);
        if (!nr->NativeLightingMatches(nullptr)) nr->SetNativeLighting(nullptr);
        m = Mk64ResolveTriangleMaterial3DS(gfx, false);
        if (m.comb == nullptr) { gPsSkinStats3DS[7]++; return false; }
        // la fraccion LOD con G_TL_LOD usa la distancia del vertice (truco de LUS): eso solo lo hace el CPU
        if (rdp->other_mode_l & G_TL_LOD) {
            for (int j = 0; j < m.numInputs; j++)
                for (int kk = 0; kk < 2; kk++)
                    if (m.comb->shader_input_mapping[kk][j] == G_CCMUX_LOD_FRACTION) { gPsSkinStats3DS[5]++; return false; }
        }
        const uint32_t cullBoth = get_attr(CULL_BOTH), cullMode = rsp->geometry_mode & cullBoth;
        cull = cullMode == get_attr(CULL_FRONT) ? 1 : cullMode == get_attr(CULL_BACK) ? 2 : cullMode == cullBoth ? 3 : 0;
        if (rsp->extra_geometry_mode & G_EX_INVERT_CULLING) cull = cull == 1 ? 2 : cull == 2 ? 1 : cull;
        if (sPsSkinPending3DS && cull != pendingCull) submit();
        lit = (rsp->geometry_mode & G_LIGHTING) != 0;
        texgen = lit && (rsp->geometry_mode & G_TEXTURE_GEN) != 0;
        texgenLinear = (rsp->geometry_mode & G_TEXTURE_GEN_LINEAR) != 0;
        nLights = rsp->current_num_lights - 1;
        if (lit && (nLights < 0 || nLights > 7)) { gPsSkinStats3DS[8]++; return false; }
        for (int t = 0; t < 2; ++t) {
            inverseWidth[t] = inverseHeight[t] = clampS[t] = clampT[t] = 0.0f;
            if (!m.usedTextures[t]) continue;
            inverseWidth[t] = 1.0f / m.tex_width[t];
            inverseHeight[t] = 1.0f / m.tex_height[t];
            clampS[t] = (m.tex_width2[t] - 0.5f) * inverseWidth[t];
            clampT[t] = (m.tex_height2[t] - 0.5f) * inverseHeight[t];
        }
        pointFilter = (rdp->other_mode_h & (3U << G_MDSFT_TEXTFILT)) == G_TF_POINT;
        // plan por lote: todo lo que no cambia por vertice se calcula una vez
        for (int t = 0; t < 2; ++t) {
            UvPlan& q = uv[t];
            q = UvPlan{};
            q.on = m.usedTextures[t];
            if (!q.on) continue;
            const uint32_t tile = m.effective_tile[t];
            const int shifts = rdp->texture_tile[tile].shifts, shiftt = rdp->texture_tile[tile].shiftt;
            float fs = 1.0f / 32.0f, ft = 1.0f / 32.0f;
            if (shifts != 0) fs *= shifts <= 10 ? 1.0f / (1 << shifts) : (float)(1 << (16 - shifts));
            if (shiftt != 0) ft *= shiftt <= 10 ? 1.0f / (1 << shiftt) : (float)(1 << (16 - shiftt));
            const float half = pointFilter ? 0.0f : 0.5f;
            q.ku = fs * inverseWidth[t];
            q.ou = (half - rdp->texture_tile[tile].uls / 4.0f) * inverseWidth[t];
            q.kv = ft * inverseHeight[t];
            q.ov = (half - rdp->texture_tile[tile].ult / 4.0f) * inverseHeight[t];
            q.writeS = (m.tm & (1 << 2 * t)) != 0;
            q.writeT = (m.tm & (1 << (2 * t + 1))) != 0;
            q.clampS = clampS[t]; q.clampT = clampT[t];
            if (atlas[t]) {
                q.cpuClampS = q.writeS; q.cpuClampT = q.writeT;
                q.mu = atlasXf[t][0]; q.au = atlasXf[t][1]; q.mv = atlasXf[t][2]; q.av = atlasXf[t][3];
                q.floatS = q.floatT = 0x1p30f;
            } else {
                q.mu = q.mv = 1.0f; q.au = q.av = 0.0f;
                q.floatS = clampS[t]; q.floatT = clampT[t];
            }
        }
        fogVtx = (rsp->geometry_mode & G_FOG) != 0;
        if (m.use_blend_color) {
            fogC[0] = rdp->blend_color.r / 255.0f; fogC[1] = rdp->blend_color.g / 255.0f; fogC[2] = rdp->blend_color.b / 255.0f;
            fogC[3] = rdp->fog_color.a / 255.0f;
        } else {
            fogC[0] = rdp->fog_color.r / 255.0f; fogC[1] = rdp->fog_color.g / 255.0f; fogC[2] = rdp->fog_color.b / 255.0f;
            fogC[3] = 0.0f;
        }
        grayC[0] = rdp->grayscale_color.r / 255.0f; grayC[1] = rdp->grayscale_color.g / 255.0f;
        grayC[2] = rdp->grayscale_color.b / 255.0f; grayC[3] = rdp->grayscale_color.a / 255.0f;
        nIn = m.numInputs;
        alphaIn = m.use_alpha;
        for (int j = 0; j < m.numInputs && j < 8; j++) {
            InPlan& ip = inp[j];
            ip = InPlan{};
            for (int kk = 0; kk < 2; kk++) {
                RGBA tmp{};
                const RGBA* color = &tmp;
                bool shade = false;
                switch (m.comb->shader_input_mapping[kk][j]) {
                    case G_CCMUX_PRIMITIVE: color = &rdp->prim_color; break;
                    case G_CCMUX_SHADE: shade = true; break;
                    case G_CCMUX_ENVIRONMENT: color = &rdp->env_color; break;
                    case G_CCMUX_PRIMITIVE_ALPHA: tmp.r = tmp.g = tmp.b = rdp->prim_color.a; break;
                    case G_CCMUX_ENV_ALPHA: tmp.r = tmp.g = tmp.b = rdp->env_color.a; break;
                    case G_CCMUX_PRIM_LOD_FRAC: tmp.r = tmp.g = tmp.b = rdp->prim_lod_fraction; break;
                    case G_CCMUX_LOD_FRACTION: tmp.r = tmp.g = tmp.b = tmp.a = 255; break;
                    case G_CCMUX_KEY_CENTER: color = &rdp->key_center; break;
                    case G_CCMUX_KEY_SCALE: color = &rdp->key_scale; break;
                    case G_CCMUX_CONVERT_K4: tmp.r = tmp.g = tmp.b = rdp->convert_k[4]; break;
                    case G_CCMUX_CONVERT_K5: tmp.r = tmp.g = tmp.b = rdp->convert_k[5]; break;
                    case G_ACMUX_PRIM_LOD_FRAC: tmp.a = rdp->prim_lod_fraction; break;
                    default: break;
                }
                if (kk == 0) {
                    ip.shadeRgb = shade;
                    if (!shade) { ip.c[0] = color->r / 255.0f; ip.c[1] = color->g / 255.0f; ip.c[2] = color->b / 255.0f; }
                } else {
                    ip.shadeA = shade;
                    if (!shade) ip.c[3] = color->a / 255.0f;
                }
            }
        }
        for (auto& w : lightValid) w = 0; // las luces pueden haber cambiado
        // luz en el GPU: una luz direccional + ambiente, sin texgen, y la sombra solo en el RGB de una entrada
        {
            int nShade = 0, nShadeA = 0;
            shadeIn = -1;
            for (int j = 0; j < nIn; j++) { if (inp[j].shadeRgb) { nShade++; if (shadeIn < 0) shadeIn = j; } if (inp[j].shadeA) nShadeA++; }
            gpuLit = gPsSkinGpuLit3DS && lit && (nLights == 1 || nLights == 2) && !texgen && nShade == 1 && nShadeA == 0;
            if (gpuLit) {
                for (int k = 0; k < 3; k++) litColor[k][0] = litColor[k][1] = litColor[k][2] = 0.0f;
                for (int i = 0; i < nLights; i++)
                    for (int c = 0; c < 3; c++) litColor[i][c] = rsp->current_lights[i].l.col[c];
                for (int c = 0; c < 3; c++) litColor[2][c] = rsp->current_lights[nLights].l.col[c];
            }
            if (sPsSkinPending3DS && gpuLit != pendingLit) submit();
            // la quinta fila de cada hueso depende de la luz: si cambio, los huesos ya puestos no sirven
            if (gpuLit != prevGpuLit || (gpuLit && sigChanged)) resetPalette();
            prevGpuLit = gpuLit;
        }
        geom = rsp->geometry_mode;
        active = true;
        gPsSkinStats3DS[9]++;
        if (gPsSkinKindLog3DS > 0) {
            gPsSkinKindLog3DS--;
            int shadeIdx = -1, shadeA = -1;
            for (int j = 0; j < nIn; j++) { if (inp[j].shadeRgb && shadeIdx < 0) shadeIdx = j; if (inp[j].shadeA && shadeA < 0) shadeA = j; }
            PsLog3DS("[sk] gpuluz %d lit %d luces %d texgen %d niebla %d/%d ent %d sombra %d/%d alfa %d gris %d tex %d%d cc %08lx%08lx prim %02x%02x%02x%02x\n", (int)gpuLit, (int)lit, nLights, (int)texgen,
                     (int)fogVtx, (int)m.use_fog, nIn, shadeIdx, shadeA, (int)alphaIn, (int)m.use_grayscale, (int)m.usedTextures[0], (int)m.usedTextures[1],
                     (unsigned long)(rdp->combine_mode >> 32), (unsigned long)rdp->combine_mode, rdp->prim_color.r, rdp->prim_color.g, rdp->prim_color.b, rdp->prim_color.a);
        }
        return true;
    }

    const float (*lightFor(int b))[3] {
        RSP* rsp = gfx->mRsp;
        if (!(lightValid[b >> 5] & (1u << (b & 31)))) {
            float mv[4][4];
            boneMatrix(b, mv);
            for (int i = 0; i < nLights; ++i) {
                const float dir[3] = { rsp->current_lights[i].l.dir[0] / 127.0f, rsp->current_lights[i].l.dir[1] / 127.0f,
                                       rsp->current_lights[i].l.dir[2] / 127.0f };
                Interpreter::TransposedMatrixMul(light[b][i], dir, mv);
                Interpreter::NormalizeVector(light[b][i]);
            }
            for (int i = 0; i < 2; ++i) {
                const float dir[3] = { rsp->lookat[i].dir[0] / 127.0f, rsp->lookat[i].dir[1] / 127.0f, rsp->lookat[i].dir[2] / 127.0f };
                Interpreter::TransposedMatrixMul(look[b][i], dir, mv);
                Interpreter::NormalizeVector(look[b][i]);
            }
            lightValid[b >> 5] |= 1u << (b & 31);
        }
        return light[b];
    }
    void boneMatrix(int b, float mv[4][4]) {
        if (bones[b] == 0) {
            std::memcpy(mv, gfx->mRsp->modelview_matrix_stack[gfx->mRsp->modelview_matrix_stack_size - 1], sizeof(float) * 16);
            return;
        }
        ::mk64_3ds::LoadFast3DFixedMatrix(mv, reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(bones[b])));
    }
    void boneRows(int b, float rows[4][4]) {
        float mv[4][4], mp[4][4];
        boneMatrix(b, mv);
        Interpreter::MatrixMul(mp, mv, gfx->mRsp->P_matrix);
        for (int r = 0; r < 4; ++r) {
            const float sign = (r == 1 && invertY) ? -1.0f : 1.0f;
            for (int c = 0; c < 4; ++c) rows[r][c] = mp[c][r] * sign;
        }
    }
    // pone el hueso b en el lugar sl de la tabla del GPU
    void setSlot(int b, int sl, const float rows[4][4]) {
        std::memcpy(gPsSkinRows3DS[sl * kPsSkinRows3DS], rows, sizeof(float) * 16);
        float* L = gPsSkinRows3DS[sl * kPsSkinRows3DS + 4];
        for (int k = 0; k < 8; k++) L[k] = 0.0f;
        if (gpuLit) { const float (*lb)[3] = lightFor(b); for (int i = 0; i < nLights && i < 2; i++) { L[i * 4] = lb[i][0]; L[i * 4 + 1] = lb[i][1]; L[i * 4 + 2] = lb[i][2]; } }
        slotBone[sl] = b;
        for (int a = 0; a < 3; a++) { slotMin[sl][a] = 1e30f; slotMax[sl][a] = -1e30f; }
        slotOfUsed = true; slotOf[b] = sl;
    }
    void addBone(int b) {
        float rows[4][4];
        boneRows(b, rows);
        if (bones[b] == 0 && sentinelSlot >= 0 && std::memcmp(rows, sentinelRows, sizeof(rows)) == 0) {
            slotOfUsed = true; slotOf[b] = sentinelSlot;
            return;
        }
        if (slots >= kPsSkinMaxSlots3DS) submit(); // respaldo: nunca escribir fuera de la tabla
        if (slots >= kPsSkinMaxSlots3DS) { gPsRetainWhy3DS[6]++; slotOfUsed = true; slotOf[b] = 0; return; }
        const int sl = slots++;
        setSlot(b, sl, rows);
        if (bones[b] == 0) { std::memcpy(sentinelRows, rows, sizeof(rows)); sentinelSlot = sl; }
    }

    static constexpr float kNearW = 1.0f;
    int cpuBone = -1; // matriz cargada en el interprete por el camino CPU
    void cpuTri(const PsSkinVtx3DS* tv) {
        submit();
        const uint8_t mvLoad = get_attr(MTX_LOAD);
        for (int k = 0; k < 3; ++k) {
            const PsSkinVtx3DS& sv = tv[k];
            const int b = sv.bone < nBones ? sv.bone : 0;
            if (bones[b] != 0 && cpuBone != b) {
                gfx->GfxSpMatrix(mvLoad, reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(bones[b])));
                cpuBone = b;
            }
            gfx->GfxSpVertex(1, k, &sv.v);
        }
        gfx->GfxSpTri1(0, 1, 2, false);
        gfx->Flush(); // no mezclar este triangulo con el lote de skin que sigue
    }
    // un vertice ya calculado (coordenadas, color, niebla) se reusa en todos sus triangulos del segmento
    std::vector<float> vcOut;
    std::vector<uint32_t> vcEpoch;
    std::vector<float> vcW;
    uint32_t palEpoch = 1;
    size_t computeVertex(const PsSkinVtx3DS& sv, float* out, float& wOut) {
        RSP* rsp = gfx->mRsp;
        const int b = sv.bone < nBones ? sv.bone : 0;
        if (slotOf[b] < 0) addBone(b);
        {
            const float* R3 = gPsSkinRows3DS[slotOf[b] * kPsSkinRows3DS + 3];
            wOut = R3[0] * sv.v.v.ob[0] + R3[1] * sv.v.v.ob[1] + R3[2] * sv.v.v.ob[2] + R3[3];
        }
        {
            const int sl = slotOf[b];
            for (int a = 0; a < 3; a++) {
                const float c = sv.v.v.ob[a];
                if (c < slotMin[sl][a]) slotMin[sl][a] = c;
                if (c > slotMax[sl][a]) slotMax[sl][a] = c;
            }
        }
        size_t n = 0;
        out[n++] = sv.v.v.ob[0];
        out[n++] = sv.v.v.ob[1];
        out[n++] = sv.v.v.ob[2];
        out[n++] = (float)(slotOf[b] * kPsSkinRows3DS);
        int32_t U = (short)(sv.v.v.tc[0] * rsp->texture_scaling_factor.s >> 16);
        int32_t V = (short)(sv.v.v.tc[1] * rsp->texture_scaling_factor.t >> 16);
        if (texgen) {
            lightFor(b);
            const float (*Lk)[3] = look[b];
            const signed char* nrm = sv.v.n.n;
            float dx = (nrm[0] * Lk[0][0] + nrm[1] * Lk[0][1] + nrm[2] * Lk[0][2]) * (1.0f / 127.0f);
            float dy = (nrm[0] * Lk[1][0] + nrm[1] * Lk[1][1] + nrm[2] * Lk[1][2]) * (1.0f / 127.0f);
            dx = dx < -1.0f ? -1.0f : dx > 1.0f ? 1.0f : dx;
            dy = dy < -1.0f ? -1.0f : dy > 1.0f ? 1.0f : dy;
            if (texgenLinear) { dx = acosf(-dx) * 0.159155f; dy = acosf(-dy) * 0.159155f; }
            else { dx = (dx + 1.0f) * 0.25f; dy = (dy + 1.0f) * 0.25f; }
            U = (int32_t)(dx * rsp->texture_scaling_factor.s);
            V = (int32_t)(dy * rsp->texture_scaling_factor.t);
        }
        for (int tt = 0; tt < 2; tt++) {
            const UvPlan& q = uv[tt];
            if (!q.on) continue;
            float un = U * q.ku + q.ou, vn = V * q.kv + q.ov;
            if (q.cpuClampS && un > q.clampS) un = q.clampS;
            if (q.cpuClampT && vn > q.clampT) vn = q.clampT;
            out[n++] = un * q.mu + q.au;
            out[n++] = vn * q.mv + q.av;
            if (q.writeS) out[n++] = q.floatS;
            if (q.writeT) out[n++] = q.floatT;
        }
        // niebla del N64: factor por vertice con z/w de la matriz del hueso (como GfxSpVertex)
        float fogZ = sv.v.v.cn[3];
        if (fogVtx) {
            const float* R2 = gPsSkinRows3DS[slotOf[b] * kPsSkinRows3DS + 2];
            const float* R3 = gPsSkinRows3DS[slotOf[b] * kPsSkinRows3DS + 3];
            const float x = sv.v.v.ob[0], y = sv.v.v.ob[1], zz = sv.v.v.ob[2];
            const float cz = R2[0] * x + R2[1] * y + R2[2] * zz + R2[3];
            float cw = R3[0] * x + R3[1] * y + R3[2] * zz + R3[3];
            if (fabsf(cw) < 0.001f) cw = 0.001f;
            float winv = 1.0f / cw;
            if (winv < 0.0f) winv = 32767.0f;
            fogZ = cz * winv * rsp->fog_mul + rsp->fog_offset;
            fogZ = fogZ < 0.0f ? 0.0f : fogZ > 255.0f ? 255.0f : fogZ;
        }
        if (m.use_fog) {
            out[n++] = fogC[0]; out[n++] = fogC[1]; out[n++] = fogC[2];
            out[n++] = m.use_blend_color ? fogC[3] : fogZ * (1.0f / 255.0f);
        }
        if (m.use_grayscale) { out[n++] = grayC[0]; out[n++] = grayC[1]; out[n++] = grayC[2]; out[n++] = grayC[3]; }
        float sr, sg, sb;
        if (gpuLit) {
            // el GPU calcula la luz: aqui va la normal + 128 (byte exacto al empacar)
            const signed char* nrm = sv.v.n.n;
            sr = (nrm[0] + 128) * (1.0f / 255.0f); sg = (nrm[1] + 128) * (1.0f / 255.0f); sb = (nrm[2] + 128) * (1.0f / 255.0f);
        } else if (lit) {
            const float (*L)[3] = lightFor(b);
            const signed char* nrm = sv.v.n.n;
            int r = rsp->current_lights[nLights].l.col[0], g = rsp->current_lights[nLights].l.col[1],
                bl = rsp->current_lights[nLights].l.col[2];
            for (int i = 0; i < nLights; ++i) {
                float in = (nrm[0] * L[i][0] + nrm[1] * L[i][1] + nrm[2] * L[i][2]) * (1.0f / 127.0f);
                if (in > 0.0f) {
                    r += in * rsp->current_lights[i].l.col[0];
                    g += in * rsp->current_lights[i].l.col[1];
                    bl += in * rsp->current_lights[i].l.col[2];
                }
            }
            sr = (r > 255 ? 255 : r) * (1.0f / 255.0f); sg = (g > 255 ? 255 : g) * (1.0f / 255.0f);
            sb = (bl > 255 ? 255 : bl) * (1.0f / 255.0f);
        } else {
            sr = sv.v.v.cn[0] * (1.0f / 255.0f); sg = sv.v.v.cn[1] * (1.0f / 255.0f); sb = sv.v.v.cn[2] * (1.0f / 255.0f);
        }
        const float sa = (fogVtx ? (float)(uint8_t)fogZ : (float)sv.v.v.cn[3]) * (1.0f / 255.0f);
        for (int j = 0; j < nIn; j++) {
            const InPlan& ip = inp[j];
            if (ip.shadeRgb) { out[n++] = sr; out[n++] = sg; out[n++] = sb; }
            else { out[n++] = ip.c[0]; out[n++] = ip.c[1]; out[n++] = ip.c[2]; }
            if (alphaIn) out[n++] = ip.shadeA ? sa : ip.c[3];
        }
        return n;
    }
    void append(const PsSkinVtx3DS* v, uint32_t nVtx, const uint16_t* ix, uint32_t nIdx) {
        if (cull == 3) return; // ambas caras: el N64 no dibuja nada
        constexpr size_t kStride = 64;
        if (vcEpoch.size() < nVtx) { vcEpoch.resize(nVtx); vcW.resize(nVtx); vcOut.resize(size_t(nVtx) * kStride); }
        for (uint32_t k = 0; k < nVtx; k++) vcEpoch[k] = 0;
        for (uint32_t t = 0; t + 2 < nIdx; t += 3) {
            const uint16_t i0 = ix[t], i1 = ix[t + 1], i2 = ix[t + 2];
            if (i0 >= nVtx || i1 >= nVtx || i2 >= nVtx) continue;
            int need = 0;
            for (uint16_t u : { i0, i1, i2 }) {
                const int b = v[u].bone < nBones ? v[u].bone : 0;
                if (slotOf[b] < 0) need++;
            }
            if (slots + need > kPsSkinMaxSlots3DS || gfx->mBufVboNumTris + 1 >= MAX_TRI_BUFFER) submit();
            size_t n = 0;
            for (uint16_t u : { i0, i1, i2 })
                if (vcEpoch[u] != palEpoch) { n = computeVertex(v[u], &vcOut[size_t(u) * kStride], vcW[u]); vcStride = n; vcEpoch[u] = palEpoch; }
            n = vcStride;
            // cerca del ojo (w ~ 0) el GPU no recorta bien: ese triangulo lo arma el CPU con su recorte
            const float w0 = vcW[i0], w1 = vcW[i1], w2 = vcW[i2];
            // capturando se guarda todo: lo cerca/detras del ojo se decide en cada cuadro al dibujar
            if (!capturing && w0 < 0.0f && w1 < 0.0f && w2 < 0.0f) continue; // detras del ojo
            if (!capturing && (w0 < kNearW || w1 < kNearW || w2 < kNearW)) {
                const PsSkinVtx3DS tv[3] = { v[i0], v[i1], v[i2] };
                cpuTri(tv);
                gPsSkinStats3DS[3]++;
                continue;
            }
            if (gfx->mBufVboNumTris == 0) {
                batchSeg = curSeg; batchKey = matKey();
                batchTmpl[0] = v[i0]; batchTmpl[1] = v[i1]; batchTmpl[2] = v[i2];
                batchPos.clear(); batchSrc.clear();
                if (capturing && !retainableState()) {
                    captureFail = true;
                    gPsRetainWhy3DS[fogVtx ? 0 : texgen ? 1 : 2]++;
                }
            }
            if (capturing) {
                for (uint16_t u : { i0, i1, i2 }) {
                    const int bb = v[u].bone < nBones ? v[u].bone : 0;
                    batchPos.push_back(v[u].v.v.ob[0]); batchPos.push_back(v[u].v.v.ob[1]); batchPos.push_back(v[u].v.v.ob[2]);
                    batchPos.push_back((float)slotOf[bb]);
                }
                batchSrc.push_back((uint16_t)curSeg); batchSrc.push_back(i0); batchSrc.push_back(i1); batchSrc.push_back(i2);
            }
            float* out = &gfx->mBufVbo[gfx->mBufVboLen];
            std::memcpy(out, &vcOut[size_t(i0) * kStride], n * sizeof(float));
            std::memcpy(out + n, &vcOut[size_t(i1) * kStride], n * sizeof(float));
            std::memcpy(out + 2 * n, &vcOut[size_t(i2) * kStride], n * sizeof(float));
            gfx->mBufVboLen += 3 * n;
            gfx->mBufVboNumTris++;
            sPsSkinPending3DS = true;
            pendingCull = cull;
            pendingLit = gpuLit;
            gPsSkinStats3DS[1]++;
        }
    }
    size_t vcStride = 0;
};
static PsSkinGpu3DS sPsSkinGpu3DS;
static void PsSkinDrawPending3DS() { sPsSkinGpu3DS.drawPending(); }

// ---- tabla de lotes retenidos (llave: contenido del lote sin los apuntadores de los huesos) ----
struct PsRetEntry3DS { bool ok = false; uint32_t lastUse = 0, failFrame = 0; std::vector<PsSkinGpu3DS::RetDraw> draws; };
static std::unordered_map<uint64_t, PsRetEntry3DS> sPsRet3DS;
static size_t sPsRetBytes3DS = 0;
static constexpr size_t kPsRetMaxBytes3DS = 3u << 20; // tope de mallas retenidas (la memoria lineal es poca)
static void PsRetFree3DS(PsRetEntry3DS& e) {
    for (auto& d : e.draws) { PsRetainFree3DS(d.buf); sPsRetBytes3DS -= d.nVtx * 40; gPsRetainStats3DS[5] -= d.nVtx * 40; }
    e.draws.clear();
    e.ok = false;
}
static void PsRetainEvict3DS() {
    for (auto it = sPsRet3DS.begin(); it != sPsRet3DS.end();) {
        if (gPsRetainFrame3DS - it->second.lastUse > 240) { PsRetFree3DS(it->second); it = sPsRet3DS.erase(it); }
        else ++it;
    }
}
static uint64_t PsRetHash3DS(const uint32_t* segStart, const uint32_t* end, uint32_t nBones) {
    uint64_t h = 0x9E3779B97F4A7C15ull ^ nBones;
    for (const uint32_t* q = segStart; q < end; q++) { h ^= *q; h *= 0x100000001B3ull; h ^= h >> 29; }
    return h;
}
// Indices por cuadro para los dibujos parciales (dos mitades: la del cuadro anterior la puede estar leyendo el GPU)
static uint16_t* sPsRetIdx3DS = nullptr;
static constexpr size_t kPsRetIdxHalf3DS = 48 * 1024;
static size_t sPsRetIdxUsed3DS = 0;
static uint32_t sPsRetIdxFrame3DS = ~0u;
static uint16_t* PsRetIdxAlloc3DS(size_t n) {
    if (!sPsRetIdx3DS) { sPsRetIdx3DS = static_cast<uint16_t*>(PsRetainAlloc3DS(kPsRetIdxHalf3DS * 2 * sizeof(uint16_t))); if (!sPsRetIdx3DS) return nullptr; }
    if (sPsRetIdxFrame3DS != gPsRetainFrame3DS) { sPsRetIdxFrame3DS = gPsRetainFrame3DS; sPsRetIdxUsed3DS = 0; }
    if (sPsRetIdxUsed3DS + n > kPsRetIdxHalf3DS) return nullptr;
    uint16_t* r = sPsRetIdx3DS + (gPsRetainFrame3DS & 1) * kPsRetIdxHalf3DS + sPsRetIdxUsed3DS;
    sPsRetIdxUsed3DS += (n + 7) & ~size_t(7);
    return r;
}
// Dibuja un lote ya retenido. Primero valida todo (estado, llaves) y decide que triangulos quedan cerca del ojo,
// sin dibujar nada; si algo no cuadra regresa false y el lote se dibuja por el camino normal.
// Los triangulos cerca del ojo van por el camino CPU (que los recorta); los de atras no se dibujan.
static bool PsRetReplay3DS(Interpreter* gfx, PsRetEntry3DS& e, const uint32_t* segStart, uint32_t nSegs) {
    auto& g = sPsSkinGpu3DS;
    RSP* rsp = gfx->mRsp;
    const uint32_t geom0 = rsp->geometry_mode;
    if (gfx->mBufVboNumTris) gfx->Flush(); // el triangulo de muestra va al inicio del buffer
    static std::vector<float> rowsCache; // por hueso de cada envio: 16 de MV*P + 4 de la luz
    static std::vector<const PsSkinVtx3DS*> segV;
    static std::vector<uint32_t> segN;
    static std::vector<uint32_t> cpuList;
    // dibujo parcial en el orden original: rachas de triangulos del GPU y triangulos del CPU intercalados
    // (los calcos sobre una superficie dependen del orden). op >= 0: triangulo CPU; op < 0: racha GPU -(n) indices
    struct Plan { bool partial; uint16_t* idx; size_t nIdx; size_t op0, nOp; };
    static std::vector<Plan> plans;
    static std::vector<int32_t> ops;
    {
        size_t need = 0;
        for (auto& d : e.draws) need += size_t(d.nSlots) * kPsSkinRows3DS * 4;
        if (rowsCache.size() < need) rowsCache.resize(need);
        segV.resize(nSegs); segN.resize(nSegs);
        plans.resize(e.draws.size());
        cpuList.clear();
        ops.clear();
        const uint32_t* q = segStart;
        for (uint32_t sg = 0; sg < nSegs; sg++) {
            const uint32_t nOps = q[0], nVtx = q[1], nIdx = q[2];
            q += 3 + nOps * 2;
            segV[sg] = reinterpret_cast<const PsSkinVtx3DS*>(q);
            segN[sg] = nVtx;
            q += nVtx * sizeof(PsSkinVtx3DS) / 4 + (nIdx * 2 + 3) / 4;
        }
    }
    g.cpuBone = -1;
    for (int pass = 0; pass < 2; pass++) {
        rsp->geometry_mode = geom0;
        g.active = false;
        size_t rc = 0;
        const uint32_t* p = segStart;
        size_t di = 0;
        for (uint32_t sg = 0; sg < nSegs; sg++) {
            const uint32_t nOps = p[0], nVtx = p[1], nIdx = p[2];
            p += 3;
            for (uint32_t o = 0; o < nOps; o++) gfx->GfxSpGeometryMode(p[o * 2], p[o * 2 + 1]);
            p += nOps * 2;
            p += nVtx * sizeof(PsSkinVtx3DS) / 4 + (nIdx * 2 + 3) / 4;
            while (di < e.draws.size() && e.draws[di].seg == sg) {
                auto& d = e.draws[di];
                Plan& pl = plans[di];
                if (!(g.active && rsp->geometry_mode == g.geom)) g.begin();
                if (!g.active) { rsp->geometry_mode = geom0; return false; }
                if (pass == 0) {
                    if (g.matKey() != d.key || !g.retainableState()) { rsp->geometry_mode = geom0; return false; }
                    bool safe[kPsSkinMaxSlots3DS];
                    bool allSafe = true;
                    float* base = &rowsCache[rc];
                    for (int k = 0; k < d.nSlots; k++) {
                        float (*rows)[4] = reinterpret_cast<float (*)[4]>(&rowsCache[rc]);
                        float* L = &rowsCache[rc + 16];
                        rc += kPsSkinRows3DS * 4;
                        g.boneRows(d.slotBone[k], rows);
                        for (int q = 0; q < 8; q++) L[q] = 0.0f;
                        if (g.gpuLit) { const float (*lb)[3] = g.lightFor(d.slotBone[k]); for (int i = 0; i < g.nLights && i < 2; i++) { L[i * 4] = lb[i][0]; L[i * 4 + 1] = lb[i][1]; L[i * 4 + 2] = lb[i][2]; } }
                        // fila w de MV*P: w minimo dentro de la esfera de los vertices de ese hueso
                        const float* R3 = rows[3];
                        const float wc = R3[0] * d.sph[k][0] + R3[1] * d.sph[k][1] + R3[2] * d.sph[k][2] + R3[3];
                        const float len = sqrtf(R3[0] * R3[0] + R3[1] * R3[1] + R3[2] * R3[2]);
                        safe[k] = wc - len * d.sph[k][3] >= PsSkinGpu3DS::kNearW;
                        allSafe &= safe[k];
                    }
                    pl.partial = !allSafe;
                    pl.idx = nullptr; pl.nIdx = 0; pl.op0 = ops.size(); pl.nOp = 0;
                    if (!allSafe) {
                        gPsRetainStats3DS[4]++;
                        const size_t nTri = d.nVtx / 3;
                        pl.idx = PsRetIdxAlloc3DS(d.nVtx);
                        if (!pl.idx) { rsp->geometry_mode = geom0; return false; }
                        size_t runStart = 0;
                        auto closeRun = [&]() { if (pl.nIdx > runStart) { ops.push_back(-(int32_t)(pl.nIdx - runStart)); pl.nOp++; } runStart = pl.nIdx; };
                        for (size_t t = 0; t < nTri; t++) {
                            float w[3];
                            for (int c = 0; c < 3; c++) {
                                const float* ps = &d.pos[(t * 3 + c) * 4];
                                const int sl = (int)ps[3];
                                if (sl < 0 || sl >= d.nSlots) { rsp->geometry_mode = geom0; return false; }
                                if (safe[sl]) { w[c] = 1e30f; continue; }
                                const float* R3 = base + sl * kPsSkinRows3DS * 4 + 12;
                                w[c] = R3[0] * ps[0] + R3[1] * ps[1] + R3[2] * ps[2] + R3[3];
                            }
                            if (w[0] < 0.0f && w[1] < 0.0f && w[2] < 0.0f) continue; // detras del ojo
                            if (w[0] < PsSkinGpu3DS::kNearW || w[1] < PsSkinGpu3DS::kNearW || w[2] < PsSkinGpu3DS::kNearW) {
                                const uint16_t* sr = &d.src[t * 4];
                                if (sr[0] >= nSegs || sr[1] >= segN[sr[0]] || sr[2] >= segN[sr[0]] || sr[3] >= segN[sr[0]]) { rsp->geometry_mode = geom0; return false; }
                                closeRun();
                                ops.push_back((int32_t)t);
                                pl.nOp++;
                                continue;
                            }
                            pl.idx[pl.nIdx++] = (uint16_t)(t * 3); pl.idx[pl.nIdx++] = (uint16_t)(t * 3 + 1); pl.idx[pl.nIdx++] = (uint16_t)(t * 3 + 2);
                        }
                        closeRun();
                    }
                } else {
                    // huesos en el mismo orden que en la captura
                    const size_t rcDraw = rc;
                    rc += size_t(d.nSlots) * kPsSkinRows3DS * 4;
                    auto drawGpu = [&](const uint16_t* idx, size_t nIdx) {
                        // las filas de los huesos (las pisa el dibujo anterior al cerrar) y el triangulo de muestra
                        g.slotOfUsed = true;
                        for (int& x : g.slotOf) x = -1;
                        g.slots = d.nSlots; g.sentinelSlot = -1;
                        for (int k = 0; k < d.nSlots; k++) {
                            std::memcpy(gPsSkinRows3DS[k * kPsSkinRows3DS], &rowsCache[rcDraw + size_t(k) * kPsSkinRows3DS * 4], sizeof(float) * kPsSkinRows3DS * 4);
                            g.slotBone[k] = d.slotBone[k];
                            g.slotOf[d.slotBone[k]] = k;
                        }
                        for (const auto& tv : d.tmpl) { const int tb = tv.bone < g.nBones ? tv.bone : 0; if (g.slotOf[tb] < 0) g.slotOf[tb] = 0; }
                        float w;
                        size_t n = 0;
                        float* out = &gfx->mBufVbo[0];
                        for (int t = 0; t < 3; t++) n = g.computeVertex(d.tmpl[t], out + t * n, w);
                        gfx->mBufVboLen = 3 * n;
                        gfx->mBufVboNumTris = 1;
                        sPsSkinPending3DS = true;
                        g.pendingCull = g.cull;
                        g.pendingLit = g.gpuLit;
                        gPsRetainBuf3DS = d.buf;
                        gPsRetainCount3DS = d.nVtx;
                        gPsRetainIdx3DS = idx;
                        gPsRetainIdxCount3DS = nIdx;
                        gPsRetainVaryingIn3DS = d.varying;
                        gPsSkinFog3DS = 0;
                        if (g.fogVtx) {
                            const bool fogAlpha = g.alphaIn && d.varying >= 0 && d.varying < g.nIn && g.inp[d.varying].shadeA;
                            gPsSkinFog3DS = fogAlpha ? 2 : 1;
                            gPsSkinFogParams3DS[0] = rsp->fog_mul;
                            gPsSkinFogParams3DS[1] = rsp->fog_offset;
                        }
                        g.drawPending();
                        gPsRetainBuf3DS = nullptr;
                        gPsRetainIdx3DS = nullptr;
                        gPsSkinFog3DS = 0;
                        gPsRetainStats3DS[0]++;
                    };
                    if (gPsSkinLo3DS == gPsSkinHi3DS && gPsRetDbg3DS-- > 0)
                        PsLog3DS("[rdbg] draw %u seg %u slots %d vtx %u parcial %d idx %u ops %u lit %d cull %d luz %d\n", (unsigned)di, (unsigned)d.seg, d.nSlots,
                                 (unsigned)d.nVtx, (int)pl.partial, (unsigned)pl.nIdx, (unsigned)pl.nOp, (int)g.lit, g.cull, (int)g.gpuLit);
                    if (!pl.partial) drawGpu(nullptr, 0);
                    else {
                        size_t at = 0;
                        for (size_t o = 0; o < pl.nOp; o++) {
                            const int32_t op = ops[pl.op0 + o];
                            if (op < 0) { drawGpu(pl.idx + at, (size_t)(-op)); at += (size_t)(-op); }
                            else {
                                const uint16_t* sr = &d.src[size_t(op) * 4];
                                const PsSkinVtx3DS* sv = segV[sr[0]];
                                const PsSkinVtx3DS tv[3] = { sv[sr[1]], sv[sr[2]], sv[sr[3]] };
                                g.cpuTri(tv);
                            }
                        }
                    }
                }
                di++;
            }
        }
        if (di != e.draws.size()) { rsp->geometry_mode = geom0; return false; }
    }
    g.active = false;
    return true;
}

#include "ps_resident_interp.inc"

bool gfx_ps_skin_handler(F3DGfx** cmd0) {
    Interpreter* gfx = mInstance;
    F3DGfx* cmd = *cmd0;
    const uint32_t* b = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(cmd->words.w1));
    if (b == nullptr) return false;
    // 'PSSK': cuerpo en la arena despues de los huesos. 'PSK2' (F2): id del lote en la cache y apuntador a su cuerpo
    const bool psk2 = b[0] == 0x50534B32u;
    if (!psk2 && b[0] != 0x5053534Bu) return false;
    const uint32_t nBones = b[1], nSegs = b[2];
    const uint32_t* bones = b + (psk2 ? 6 : 3);
    const uint32_t* segStart = psk2 ? reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(b[4])) : bones + nBones;
    ::mk64_3ds::TriangleCommandScope scope3DS(sTriangleRun3DS, false);
    const uint8_t mvLoad = get_attr(MTX_LOAD); // modelview, cargar, sin push
    int& loadedBone = sPsSkinGpu3DS.cpuBone; // la comparten el respaldo CPU y los triangulos cerca del ojo
    loadedBone = -1;
    gPsSkinStats3DS[0]++;
    sPsSkinGpu3DS.gfx = gfx;
    sPsSkinGpu3DS.bones = bones;
    sPsSkinGpu3DS.nBones = nBones;
    sPsSkinGpu3DS.active = false;
    const int blk3DS = gPsSkinBlock3DS++;
    const bool gpuOk3DS = blk3DS >= gPsSkinLo3DS && blk3DS <= gPsSkinHi3DS;
    if (psk2) {
        if (gpuOk3DS && gPsSkinGpuEnabled3DS && PsResDraw3DS(gfx, b)) return false;
        gPsResStats3DS[13]++; // va por el camino anterior con el mismo cuerpo
    }
    auto finish = [&]() {
        sPsSkinGpu3DS.active = false; // el lote sigue abierto: lo cierra el siguiente Flush()
        // el interprete queda con la matriz del ultimo hueso, como si hubiera corrido los G_MTX originales
        if (nBones > 0 && loadedBone != (int)nBones - 1 && bones[nBones - 1] != 0)
            gfx->GfxSpMatrix(mvLoad, reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(bones[nBones - 1])));
        return false;
    };
    // recorre los segmentos; capturando, un segmento que va por el CPU corta la captura (no dibuja nada)
    auto runSegments = [&](bool capturing) {
        const uint32_t* p = segStart;
        sPsSkinGpu3DS.resetPalette();
        for (uint32_t sg = 0; sg < nSegs; sg++) {
            const uint32_t nOps = p[0], nVtx = p[1], nIdx = p[2];
            sPsSkinGpu3DS.curSeg = sg;
            p += 3;
            for (uint32_t o = 0; o < nOps; o++) gfx->GfxSpGeometryMode(p[o * 2], p[o * 2 + 1]);
            p += nOps * 2;
            const PsSkinVtx3DS* v = reinterpret_cast<const PsSkinVtx3DS*>(p);
            const uint16_t* ix = reinterpret_cast<const uint16_t*>(p + nVtx * sizeof(PsSkinVtx3DS) / 4);
            const uint32_t segWords3DS = nVtx * sizeof(PsSkinVtx3DS) / 4 + (nIdx * 2 + 3) / 4;
            gPsSkinStats3DS[2]++;
            if (nIdx >= 3 && gpuOk3DS) {
                auto& g = sPsSkinGpu3DS;
                // mismo estado que el segmento anterior (solo cambian huesos): se sigue llenando el mismo envio
                if (!(gPsSkinMerge3DS && g.active && gfx->mRsp->geometry_mode == g.geom)) {
                    if (!gPsSkinMerge3DS) g.submit();
                    g.begin();
                }
                if (g.active) {
                    g.append(v, nVtx, ix, nIdx);
                    p += segWords3DS;
                    continue;
                }
            }
            if (capturing && nIdx >= 3) { gPsRetainWhy3DS[3]++; sPsSkinGpu3DS.captureFail = true; return; }
            sPsSkinGpu3DS.submit();
            sPsSkinGpu3DS.active = false;
            for (uint32_t t = 0; t + 2 < nIdx; t += 3) {
                if (ix[t] >= nVtx || ix[t + 1] >= nVtx || ix[t + 2] >= nVtx) continue;
                for (int k = 0; k < 3; k++) {
                    const PsSkinVtx3DS& sv = v[ix[t + k]];
                    if (sv.bone != loadedBone && sv.bone < nBones && bones[sv.bone] != 0) {
                        gfx->GfxSpMatrix(mvLoad, reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(bones[sv.bone])));
                        loadedBone = sv.bone;
                        gPsSkinStats3DS[3]++;
                    }
                    gfx->GfxSpVertex(1, k, &sv.v);
                }
                gfx->GfxSpTri1(0, 1, 2, false);
                gPsSkinStats3DS[1]++;
            }
            p += segWords3DS;
        }
        if (!gPsSkinMerge3DS) sPsSkinGpu3DS.submit();
    };
    // malla retenida
    // lotes chicos no: por el camino normal se juntan con los siguientes en un solo dibujo, y retenidos cada uno
    // seria un dibujo aparte (mas caro que empacar sus pocos vertices). retenermin.txt cambia el minimo
    uint32_t blockTris3DS = 0;
    if (gPsRetain3DS && gpuOk3DS) {
        const uint32_t* q = segStart;
        for (uint32_t sg = 0; sg < nSegs; sg++) { const uint32_t nOps = q[0], nVtx = q[1], nIdx = q[2]; blockTris3DS += nIdx / 3; q += 3 + nOps * 2 + nVtx * sizeof(PsSkinVtx3DS) / 4 + (nIdx * 2 + 3) / 4; }
    }
    if (!(gPsRetain3DS && gpuOk3DS && gPsSkinGpuEnabled3DS && gPsSkinMerge3DS)) gPsRetainSkip3DS[1]++;
    else if (blockTris3DS < (uint32_t)gPsRetainMinTris3DS) gPsRetainSkip3DS[0]++;
    if (gPsRetain3DS && gpuOk3DS && gPsSkinGpuEnabled3DS && gPsSkinMerge3DS && blockTris3DS >= (uint32_t)gPsRetainMinTris3DS) {
        const uint32_t* q = segStart;
        for (uint32_t sg = 0; sg < nSegs; sg++) { const uint32_t nOps = q[0], nVtx = q[1], nIdx = q[2]; q += 3 + nOps * 2 + nVtx * sizeof(PsSkinVtx3DS) / 4 + (nIdx * 2 + 3) / 4; }
        const uint64_t key = PsRetHash3DS(segStart, q, nBones);
        PsRetEntry3DS* ret = nullptr;
        bool capture = false;
        auto it = sPsRet3DS.find(key);
        if (it != sPsRet3DS.end()) {
            ret = &it->second;
            ret->lastUse = gPsRetainFrame3DS;
            if (!ret->ok && gPsRetainFrame3DS - ret->failFrame > 90 && sPsRetBytes3DS < kPsRetMaxBytes3DS) capture = true;
        } else {
            if (sPsRetBytes3DS >= kPsRetMaxBytes3DS) {
                // lleno: soltar lo que no se ha usado en el ultimo segundo
                for (auto it2 = sPsRet3DS.begin(); it2 != sPsRet3DS.end();) {
                    if (gPsRetainFrame3DS - it2->second.lastUse > 30) { PsRetFree3DS(it2->second); it2 = sPsRet3DS.erase(it2); }
                    else ++it2;
                }
            }
        }
        if (!ret && sPsRetBytes3DS < kPsRetMaxBytes3DS) {
            ret = &sPsRet3DS[key];
            ret->lastUse = gPsRetainFrame3DS;
            capture = true;
        }
        const uint32_t geom0 = gfx->mRsp->geometry_mode;
        if (!ret) gPsRetainSkip3DS[2]++;
        else if (!capture && !ret->ok) gPsRetainSkip3DS[4]++;
        if (ret && capture) {
            auto& g = sPsSkinGpu3DS;
            g.submit(); // el lote anterior va aparte
            if (gfx->mBufVboNumTris) gfx->Flush();
            g.capturing = true;
            g.captureFail = false;
            g.capDraws.clear();
            runSegments(true);
            g.submit(); // el ultimo envio tambien se captura (sin dibujar)
            g.capturing = false;
            g.active = false;
            if (!g.captureFail && !g.capDraws.empty()) {
                PsRetFree3DS(*ret);
                ret->draws = std::move(g.capDraws);
                for (auto& d : ret->draws) sPsRetBytes3DS += d.nVtx * 40;
                ret->ok = true;
            } else {
                for (auto& d : g.capDraws) { PsRetainFree3DS(d.buf); gPsRetainStats3DS[5] -= d.nVtx * 40; }
                ret->ok = false;
                ret->failFrame = gPsRetainFrame3DS;
                gPsRetainStats3DS[3]++;
            }
            g.capDraws.clear();
            g.batchPos.clear(); g.batchSrc.clear();
            gfx->mRsp->geometry_mode = geom0;
            if (!ret->ok) { gPsRetainSkip3DS[3]++; runSegments(false); return finish(); } // la captura no dibujo nada: camino normal
            gPsRetainSkip3DS[6]++;
        }
        if (ret && ret->ok) {
            sPsSkinGpu3DS.submit(); // el lote anterior va aparte
            sPsSkinGpu3DS.resetPalette();
            if (PsRetReplay3DS(gfx, *ret, segStart, nSegs)) {
                gPsRetainStats3DS[2]++;
                if (nBones > 0 && bones[nBones - 1] != 0)
                    gfx->GfxSpMatrix(mvLoad, reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(bones[nBones - 1])));
                sPsSkinGpu3DS.active = false;
                return false;
            }
            gPsRetainSkip3DS[5]++;
            gfx->mRsp->geometry_mode = geom0;
        }
    }
    runSegments(false);
    return finish();
}
'''
rep("void Interpreter::Flush() {", "static bool sPsSkinPending3DS = false;\nstatic void PsSkinDrawPending3DS();\n"
    "void Interpreter::Flush() {\n    if (sPsSkinPending3DS) { PsSkinDrawPending3DS(); return; }")
rep("void Interpreter::GfxSpTri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect) {",
    "void Interpreter::GfxSpTri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx, bool is_rect) {\n    if (sPsSkinPending3DS) Flush();")
rep("static bool Mk64ReplayStaticRun3DS(Interpreter* gfx, const ::mk64_3ds::StaticRunCache<MAX_VERTICES + 4>::Entry& entry) {",
    "static bool Mk64ReplayStaticRun3DS(Interpreter* gfx, const ::mk64_3ds::StaticRunCache<MAX_VERTICES + 4>::Entry& entry) {\n    if (sPsSkinPending3DS) gfx->Flush();")
rep("static constexpr UcodeHandler otrHandlers = {", handler + "\nstatic constexpr UcodeHandler otrHandlers = {")
rep("    { OTR_G_SETINTENSITY, { \"G_SETINTENSITY\", gfx_set_intensity_handler_custom } }, // G_SETINTENSITY (0x40)",
    "    { OTR_G_SETINTENSITY, { \"G_SETINTENSITY\", gfx_set_intensity_handler_custom } }, // G_SETINTENSITY (0x40)\n"
    "    { (int8_t)0x41, { \"G_PS_SKIN\", gfx_ps_skin_handler } },                          // Stadium: modelos con huesos")
s = '#include "ps_resident.h" // F2 (fuera del namespace Fast)\n' + s
open(p, "w").write(s)
print("patch_interp_skin: ok")

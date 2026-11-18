#!/usr/bin/env python3
"""Copia el backend citro3d de MK64 3DS con un arreglo para combinadores de Stadium.

El TEV del PICA tiene UNA constante por etapa. El backend original arma (A-B)*C+D con fuentes que pueden
ser varias constantes distintas (PRIM, ENV, cero, uno) en la misma etapa; la etapa solo carga la primera
y las demas leen ese mismo valor. Ejemplo real (piso de la arena): (SHADE-ENV)*PRIM + 0 se volvia
prev*PRIM + PRIM = blanco. Aqui cada operacion queda con a lo mas una constante distinta, usando
"1 - x" (bit 0x80 en la fuente) para las interpolaciones entre dos constantes.

Uso: patch_citro3d.py entrada.cpp salida.cpp
"""
import sys

src = open(sys.argv[1]).read()


def rep(old, new):
    global src
    if old not in src:
        sys.exit("patch_citro3d: no encontre: " + old[:70])
    src = src.replace(old, new, 1)


# --- fuentes con bandera "1 - x" ---
rep("GPU_TEVSRC SourceToTev(int source, int varyingInput, int cycle) {\n",
    "GPU_TEVSRC SourceToTev(int source, int varyingInput, int cycle) {\n    source &= 0x7F;\n")
rep("""GPU_TEVOP_RGB SourceToRgbOperand(int source) {
    return source == SourceTexel0Alpha || source == SourceTexel1Alpha ? GPU_TEVOP_RGB_SRC_ALPHA
                                                                      : GPU_TEVOP_RGB_SRC_COLOR;
}""", """GPU_TEVOP_RGB SourceToRgbOperand(int source) {
    const bool inv = (source & 0x80) != 0;
    source &= 0x7F;
    const bool alpha = source == SourceTexel0Alpha || source == SourceTexel1Alpha;
    if (inv) return alpha ? GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA : GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR;
    return alpha ? GPU_TEVOP_RGB_SRC_ALPHA : GPU_TEVOP_RGB_SRC_COLOR;
}

GPU_TEVOP_A SourceToAlphaOperand(int source) {
    return (source & 0x80) ? GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA : GPU_TEVOP_A_SRC_ALPHA;
}""")
rep("std::array<float, 4> ConstantForSource(int source, const std::array<std::array<float, 4>, 7>& inputs) {\n",
    "std::array<float, 4> ConstantForSource(int source, const std::array<std::array<float, 4>, 7>& inputs) {\n    source &= 0x7F;\n")
rep("bool IsConstantSource(int source, int varyingInput) {\n",
    "bool IsConstantSource(int source, int varyingInput) {\n    source &= 0x7F;\n")
rep("""    C3D_TexEnvOpAlpha(environment, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA);
    (void) alphaOperation;""", """    C3D_TexEnvOpAlpha(environment, SourceToAlphaOperand(alphaOperation.source[0]),
                      SourceToAlphaOperand(alphaOperation.source[1]), SourceToAlphaOperand(alphaOperation.source[2]));""")
rep("                    const int source = encoded.source[term];",
    "                    const int source = encoded.source[term] & 0x7F;")

# --- plan por canal sin choques de constantes ---
start = src.index("ChannelPlan BuildChannelPlan(const int formula[4]) {")
end = src.index("ChannelOperation PassPrevious() {")
src = src[:start] + r'''// Fuentes que el TEV lee del registro de constante de la etapa (cero/uno/ruido o una entrada del
// combinador; una de las entradas puede ser el color del vertice en tiempo de dibujo, pero aqui se cuenta
// como constante para no arriesgar).
static bool StadiumConstLike(int s) {
    s &= 0x7F;
    return (s >= SourceInput1 && s <= SourceInput7) || s == SourceZero || s == SourceOne || s == SourceNoise;
}
static bool StadiumConstConflict(std::initializer_list<int> sources) {
    int first = -1;
    for (int s : sources) {
        if (!StadiumConstLike(s)) continue;
        if (first < 0) first = s & 0x7F;
        else if ((s & 0x7F) != first) return true;
    }
    return false;
}
static ChannelPlan StadiumTwo(ChannelOperation a, ChannelOperation b) {
    ChannelPlan plan;
    plan.operations[0] = a;
    plan.operations[1] = b;
    plan.count = 2;
    return plan;
}

ChannelPlan BuildChannelPlan(const int formula[4]) {
    const int a = formula[0];
    const int b = formula[1];
    const int c = formula[2];
    const int d = formula[3];
    constexpr int kInv = 0x80;

    if (c == SourceZero) {
        return SingleOperationPlan({ GPU_REPLACE, { d, SourceZero, SourceZero } });
    }
    if (b == SourceZero && d == SourceZero) {
        if (!StadiumConstConflict({ a, c })) return SingleOperationPlan({ GPU_MODULATE, { a, c, SourceZero } });
        if (c == SourceCombined) return SingleOperationPlan({ GPU_MODULATE, { a, c, SourceZero } });
        return StadiumTwo({ GPU_REPLACE, { a, SourceZero, SourceZero } }, { GPU_MODULATE, { SourceCombined, c, SourceZero } });
    }
    if (b == d) {
        // lerp(b, a, c) = a*c + b*(1-c)
        if (!StadiumConstConflict({ a, b, c })) return SingleOperationPlan({ GPU_INTERPOLATE, { a, b, c } });
        if (a == SourceZero) return SingleOperationPlan({ GPU_MODULATE, { b, c | kInv, SourceZero } });
        if (c != SourceCombined && b != SourceCombined && !StadiumConstConflict({ a, c }) && !StadiumConstConflict({ c, b }))
            return StadiumTwo({ GPU_MODULATE, { a, c, SourceZero } }, { GPU_MULTIPLY_ADD, { c | kInv, b, SourceCombined } });
        // a y b constantes distintas y c otra constante: no cabe exacto en dos etapas; se deja el original
        return SingleOperationPlan({ GPU_INTERPOLATE, { a, b, c } });
    }
    if (b == SourceZero) {
        if (!StadiumConstConflict({ a, c, d }) || d == SourceCombined) return SingleOperationPlan({ GPU_MULTIPLY_ADD, { a, c, d } });
        return StadiumTwo({ GPU_MODULATE, { a, c, SourceZero } }, { GPU_ADD, { SourceCombined, d, SourceZero } });
    }
    if (d == SourceZero && c != SourceCombined) {
        return StadiumTwo({ GPU_SUBTRACT, { a, b, SourceZero } }, { GPU_MODULATE, { SourceCombined, c, SourceZero } });
    }
    return StadiumTwo({ GPU_SUBTRACT, { a, b, SourceZero } }, { GPU_MULTIPLY_ADD, { SourceCombined, c, d } });
}

''' + src[end:]

# --- Stadium: modelos con huesos (skinning en el GPU, shader de patch_shader.py) ---
rep("    int originalFogUniform = -1;\n", "    int originalFogUniform = -1;\n    int psResidentUniform = -1;\n    int psSkinUniform = -1;\n    int psSkinBonesUniform = -1;\n    int psSkinLitUniform = -1;\n    int psSkinLightColorUniform = -1;\n    int psSkinFogUniform = -1;\n    int psSkinFogAlphaUniform = -1;\n    int psSkinFogParamsUniform = -1;\n")
rep("""    mImpl->originalFogUniform = shaderInstanceGetUniformLocation(mImpl->shaderProgram.vertexShader, "originalFogEnabled");\n""",
    """    mImpl->originalFogUniform = shaderInstanceGetUniformLocation(mImpl->shaderProgram.vertexShader, "originalFogEnabled");
    mImpl->psSkinUniform = shaderInstanceGetUniformLocation(mImpl->shaderProgram.vertexShader, "skinEnabled");
    mImpl->psResidentUniform = shaderInstanceGetUniformLocation(mImpl->shaderProgram.vertexShader, "residentEnabled");
    mImpl->psSkinBonesUniform = shaderInstanceGetUniformLocation(mImpl->shaderProgram.vertexShader, "skinBones");
    mImpl->psSkinLitUniform = shaderInstanceGetUniformLocation(mImpl->shaderProgram.vertexShader, "skinLit");
    mImpl->psSkinLightColorUniform = shaderInstanceGetUniformLocation(mImpl->shaderProgram.vertexShader, "skinLightColor");
    mImpl->psSkinFogUniform = shaderInstanceGetUniformLocation(mImpl->shaderProgram.vertexShader, "skinFog");
    mImpl->psSkinFogAlphaUniform = shaderInstanceGetUniformLocation(mImpl->shaderProgram.vertexShader, "skinFogAlpha");
    mImpl->psSkinFogParamsUniform = shaderInstanceGetUniformLocation(mImpl->shaderProgram.vertexShader, "skinFogParams");
""")
i = src.index("\nnamespace")
src = src[:i] + """
// Lote de huesos activo (lo llena el comando G_PS_SKIN del interprete justo antes de Flush)
extern "C" {
bool gPsSkinActive3DS = false;
float gPsSkinRows3DS[66][4];  // por hueso, 6 filas: 4 de MV*P (columnas) + direccion de las 2 luces en su espacio
int gPsSkinRowCount3DS = 0;
bool gPsSkinLit3DS = false;   // luz en el GPU (la normal va en el color)
float gPsSkinLightColor3DS[3][4]; // difusa 1, difusa 2, ambiente (0..255)
int gPsSkinVarying3DS = -1;   // entrada del combinador que lleva la sombra
// Mallas retenidas: el interprete pide copiar los vertices empacados de un envio a un buffer suyo (captura) o
// dibujar directo de ese buffer sin volver a empacar (en ese caso bufVbo trae solo un triangulo de muestra
// para sacar el estado: constantes, recortes de UV, niebla)
void* gPsRetainCaptureDst3DS = nullptr;
size_t gPsRetainCaptureMax3DS = 0;
size_t gPsRetainCaptured3DS = 0;   // vertices copiados (0 = no se pudo)
const void* gPsRetainBuf3DS = nullptr;
size_t gPsRetainCount3DS = 0;
bool gPsRetainNoDraw3DS = false;           // captura: solo empacar y copiar, sin dibujar
const uint16_t* gPsRetainIdx3DS = nullptr; // dibujo parcial: solo estos vertices del buffer retenido
size_t gPsRetainIdxCount3DS = 0;
int gPsRetainVaryingOut3DS = -1;  // captura: que entrada salio variable
int gPsRetainVaryingIn3DS = -1;
int gPsSkinFog3DS = 0;          // retenido con niebla en el GPU: 1 coordenada, 2 tambien el alfa del color
float gPsSkinFogParams3DS[2];   // fog_mul, fog_offset   // dibujo retenido: forzar esa (la muestra de 3 vertices puede parecer constante)
void* PsRetainAlloc3DS(size_t bytes);
void PsRetainFree3DS(void* p);
int gPsSkinCull3DS = 0;       // 0 ninguno, 1 frente, 2 atras
}
""" + src[i:]
rep("""    for (size_t vertex = 0; vertex < sourceVertexCount; ++vertex) {
        const float w = bufVbo[vertex * program->strideFloats + 3];""",
    """    const bool psSkin = gPsSkinActive3DS && mImpl->psSkinUniform >= 0 && mImpl->psSkinBonesUniform >= 0;
    for (size_t vertex = 0; !psSkin && vertex < sourceVertexCount; ++vertex) {
        const float w = bufVbo[vertex * program->strideFloats + 3];""")
rep("""            destination.clipDepth = mImpl->nativeGeometry.enabled""",
    """            destination.clipDepth = psSkin ? 0.0f : mImpl->nativeGeometry.enabled""")
draw_old = "    C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(firstVertex), static_cast<int>(vertexCount));\n    ++mImpl->drawCallCount;"
rep(draw_old, """    if (psSkin) {
        auto* rows = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, mImpl->psSkinBonesUniform, gPsSkinRowCount3DS);
        for (int r = 0; r < gPsSkinRowCount3DS; ++r) {
            rows[r].x = gPsSkinRows3DS[r][0]; rows[r].y = gPsSkinRows3DS[r][1];
            rows[r].z = gPsSkinRows3DS[r][2]; rows[r].w = gPsSkinRows3DS[r][3];
        }
        C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinUniform, true);
        if (mImpl->psSkinLitUniform >= 0) {
            C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinLitUniform, gPsSkinLit3DS);
            if (gPsSkinLit3DS && mImpl->psSkinLightColorUniform >= 0) {
                auto* lc = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, mImpl->psSkinLightColorUniform, 3);
                for (int r = 0; r < 3; ++r) { lc[r].x = gPsSkinLightColor3DS[r][0]; lc[r].y = gPsSkinLightColor3DS[r][1]; lc[r].z = gPsSkinLightColor3DS[r][2]; lc[r].w = 0.0f; }
            }
        }
        if (mImpl->psSkinFogUniform >= 0 && mImpl->psSkinFogAlphaUniform >= 0 && mImpl->psSkinFogParamsUniform >= 0) {
            const bool fogOn = gPsSkinFog3DS != 0 && gPsRetainBuf3DS != nullptr;
            C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinFogUniform, fogOn);
            C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinFogAlphaUniform, fogOn && gPsSkinFog3DS == 2);
            if (fogOn) C3D_FVUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinFogParamsUniform, gPsSkinFogParams3DS[0], gPsSkinFogParams3DS[1], 0.0f, 0.0f);
        }
        C3D_CullFace(gPsSkinCull3DS == 1 ? GPU_CULL_FRONT_CCW : gPsSkinCull3DS == 2 ? GPU_CULL_BACK_CCW : GPU_CULL_NONE);
    }
    C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(firstVertex), static_cast<int>(vertexCount));
    if (psSkin) {
        C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinUniform, false);
        if (mImpl->psSkinLitUniform >= 0) C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinLitUniform, false);
        if (mImpl->psSkinFogUniform >= 0) C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinFogUniform, false);
        if (mImpl->psSkinFogAlphaUniform >= 0) C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinFogAlphaUniform, false);
        mImpl->nativeVertexStateDirty = true; // el siguiente dibujo vuelve a poner su descarte de caras
    }
    ++mImpl->drawCallCount;""")

# mallas retenidas: asignacion en memoria lineal (la lee el GPU)
i = src.index("\nnamespace")
src = src[:i] + """
// La memoria lineal es de 20 MB y la comparten texturas, paginas del atlas y buffers del GPU: si una de esas no
// consigue memoria el juego truena. Las mallas retenidas solo toman memoria si quedan 6 MB libres despues.
extern "C" void* PsRetainAlloc3DS(size_t bytes) {
    if (linearSpaceFree() < bytes + (6u << 20)) return nullptr;
    return linearAlloc(bytes);
}
extern "C" void PsRetainFree3DS(void* p) { if (p) linearFree(p); }
""" + src[i:]
# dibujar de un buffer retenido: no se empaca
rep("""        const bool reuseConvertedVertices = vertexCount >= 6;""",
    """        const bool psRetainDraw3DS = psSkin && gPsRetainBuf3DS != nullptr;
        if (psRetainDraw3DS) vertexCount = 0; // los vertices ya estan en el buffer retenido
        const bool reuseConvertedVertices = vertexCount >= 6;""")
rep("""    std::array<float, 4> grayscaleColor = { 1.0f, 1.0f, 1.0f, 0.0f };""",
    """    if (psSkin && gPsRetainCaptureDst3DS != nullptr && gPsRetainBuf3DS == nullptr) {
        gPsRetainCaptured3DS = 0;
        if (uniformClamps && vertexCount <= gPsRetainCaptureMax3DS) {
            std::memcpy(gPsRetainCaptureDst3DS, &mImpl->packedVertices[firstVertex], vertexCount * sizeof(PackedVertex));
            if (Mk64System3DSCleanDataCache(gPsRetainCaptureDst3DS, vertexCount * sizeof(PackedVertex))) gPsRetainCaptured3DS = vertexCount;
        }
    }
    std::array<float, 4> grayscaleColor = { 1.0f, 1.0f, 1.0f, 0.0f };""")
rep("""    if (mImpl->dirtyVertexBegin == mImpl->dirtyVertexEnd) {
        mImpl->dirtyVertexBegin = firstVertex;
    }
    mImpl->dirtyVertexEnd = firstVertex + vertexCount;
    BindPackedVertices();""",
    """    const bool psRetainNow3DS = psSkin && gPsRetainBuf3DS != nullptr;
    if (!psRetainNow3DS) {
    if (mImpl->dirtyVertexBegin == mImpl->dirtyVertexEnd) {
        mImpl->dirtyVertexBegin = firstVertex;
    }
    mImpl->dirtyVertexEnd = firstVertex + vertexCount;
    BindPackedVertices();
    } else {
        C3D_BufInfo* retainInfo3DS = C3D_GetBufInfo();
        BufInfo_Init(retainInfo3DS);
        BufInfo_Add(retainInfo3DS, gPsRetainBuf3DS, sizeof(PackedVertex), 5, 0x43210);
        mImpl->packedBufferBound = false;
    }""")
rep("""    C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(firstVertex), static_cast<int>(vertexCount));
    if (psSkin) {
        C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinUniform, false);""",
    """    if (psSkin && gPsRetainNoDraw3DS) {}
    else if (psRetainNow3DS && gPsRetainIdx3DS) {
        Mk64System3DSCleanDataCache(const_cast<uint16_t*>(gPsRetainIdx3DS), gPsRetainIdxCount3DS * 2);
        C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(gPsRetainIdxCount3DS), C3D_UNSIGNED_SHORT, gPsRetainIdx3DS);
    }
    else if (psRetainNow3DS) C3D_DrawArrays(GPU_TRIANGLES, 0, static_cast<int>(gPsRetainCount3DS));
    else
    C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(firstVertex), static_cast<int>(vertexCount));
    if (psSkin) {
        C3D_BoolUnifSet(GPU_VERTEX_SHADER, mImpl->psSkinUniform, false);""")

rep("""    if (varyingInput < 0 && program->numInputs > 0) varyingInput = 0;""",
    """    if (varyingInput < 0 && program->numInputs > 0) varyingInput = 0;
    if (psSkin && gPsRetainBuf3DS != nullptr && gPsRetainVaryingIn3DS < program->numInputs) varyingInput = gPsRetainVaryingIn3DS;
    if (psSkin && gPsRetainCaptureDst3DS != nullptr && gPsRetainBuf3DS == nullptr) gPsRetainVaryingOut3DS = varyingInput;
    { // sklog.txt (repeticion): entradas del combinador de los dibujos con 3 o mas
        if (gPsTevLog3DS > 0 && program->numInputs >= 2 && psSkin) {
            gPsTevLog3DS--;
            char t[240]; int o = snprintf(t, sizeof t, "[tev] skin %d ret %d ent %d var %d vtx %u 2c %d alfa %d ops %d/%d %d/%d:", (int)psSkin, (int)(gPsRetainBuf3DS != nullptr),
                                          (int)program->numInputs, varyingInput, (unsigned)vertexCount, (int)program->twoCycle, (int)program->alpha,
                                          (int)program->tevOperationCount[0][0], (int)program->tevOperationCount[0][1], (int)program->tevOperationCount[1][0], (int)program->tevOperationCount[1][1]);
            for (int j = 0; j < program->numInputs && o < 200; j++) o += snprintf(t + o, sizeof t - o, " %.2f,%.2f,%.2f", constants[j][0], constants[j][1], constants[j][2]);
            PsLog3DS("%s\\n", t);
        }
        if (gPsTevLog3DS > 0 && !psSkin) {
            gPsTevLog3DS--;
            const auto& g = mImpl->nativeGeometry;
            PsLog3DS("[nat] vtx %u nativo %d fila1 %.3f %.3f %.3f fila3 %.1f %.1f %.1f %.1f pos0 %.1f %.1f %.1f %.1f\\n", (unsigned)vertexCount, (int)g.enabled, g.matrix[1][0], g.matrix[1][1], g.matrix[1][2],
                     g.matrix[3][0], g.matrix[3][1], g.matrix[3][2], g.matrix[3][3], drawVertices[0], drawVertices[1], drawVertices[2], drawVertices[3]);
        }
    }""")

# imagenes aparte dibujadas en el GPU (G_PS_FB): al empezar el siguiente cuadro (el GPU ya termino el anterior)
# se copian a la memoria del juego en RGBA16 big-endian. Orientacion: fborient.txt (0-7: bit0 girar, bit1/2 espejo)
i = src.index("\nnamespace")
src = src[:i] + """
#include <vector>
struct PsFbReadback3DS { int fbId; uint32_t addr, w, h; };
static std::vector<PsFbReadback3DS> sPsFbPending3DS;
extern "C" void PsFbQueueReadback3DS(int fbId, uint32_t addr, uint32_t w, uint32_t h) {
    for (auto& r : sPsFbPending3DS) if (r.fbId == fbId && r.addr == addr) return;
    sPsFbPending3DS.push_back({ fbId, addr, w, h });
}
extern "C" uint8_t gPsFbFillA3DS[64];
extern "C" int gPsFbOrient3DS;
extern "C" int gPsTevLog3DS;
int gPsTevLog3DS = 0;
int gPsFbOrient3DS = 5; // confirmado con fbtest (rojo arriba-izq, azul abajo-der)
extern "C" void PsLog3DS(const char* fmt, ...);
extern "C" int gPsFbStage3DS;
int gPsFbStage3DS = 0; // fbtest: que prueba se esta corriendo
extern "C" uint32_t gPsFbCopies3DS;
uint32_t gPsFbCopies3DS = 0;
// rangos de la memoria del juego que la copia acaba de escribir: el interprete borra de su cache las texturas
// que se leyeron de ahi (la cache solo usa la direccion, no el contenido)
extern "C" uint32_t gPsFbWritten3DS[16][2];
extern "C" int gPsFbWrittenN3DS;
uint32_t gPsFbWritten3DS[16][2];
int gPsFbWrittenN3DS = 0;
#include <functional>
static std::function<void()> sPsFbDoCopies3DS;
""" + src[i:]
# al terminar un cuadro que dibujo en imagenes aparte: esperar al GPU y copiar antes de regresarle el control al
# juego (en el N64 el RDP ya escribio la memoria cuando el juego recibe el aviso de "termino")
src += """
extern "C" bool PsFbFlushNow3DS(void) {
    if (sPsFbPending3DS.empty() || !sPsFbDoCopies3DS) return false;
    if (!mk64_3ds::BeginFrame3DS(0, "psfb-readback-gpu-wait")) return false;
    sPsFbDoCopies3DS();
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    return true;
}
"""
rep("""    mk64_3ds::FrameBeginWait3DS measuredWait;
""", """    mk64_3ds::FrameBeginWait3DS measuredWait;
""")
rep("""    mImpl->frameBeginWaitMicroseconds = static_cast<uint32_t>((svcGetSystemTick() - beginTick) * 1000000ULL / SYSCLOCK_ARM11);
    mImpl->frameActive = true;
""", """    mImpl->frameBeginWaitMicroseconds = static_cast<uint32_t>((svcGetSystemTick() - beginTick) * 1000000ULL / SYSCLOCK_ARM11);
    mImpl->frameActive = true;
    // La copia vive en una funcion para poder hacerla tambien al terminar el cuadro (PsFbFlushNow3DS):
    // el juego cree que el RDP ya acabo en cuanto el host regresa, y copia esas imagenes enseguida.
    if (!sPsFbDoCopies3DS) sPsFbDoCopies3DS = [this]() {
        static uint32_t* scratch = nullptr;
        static size_t scratchPx = 0;
        for (const auto& r : sPsFbPending3DS) {
            if (r.fbId <= 0 || r.fbId >= static_cast<int>(mImpl->framebuffers.size()) || !mImpl->framebuffers[r.fbId] ||
                !mImpl->framebuffers[r.fbId]->initialized)
                continue;
            auto& slot = *mImpl->framebuffers[r.fbId];
            const uint32_t tw = slot.texture.width, th = slot.texture.height;
            if (scratchPx < size_t(tw) * th) {
                if (scratch) free(scratch);
                scratch = static_cast<uint32_t*>(malloc(size_t(tw) * th * 4));
                scratchPx = scratch ? size_t(tw) * th : 0;
                if (!scratch) continue;
            }
            // de mosaico del GPU a lineal en el CPU: bloques de 8x8, dentro de cada uno en orden Morton
            {
                const uint32_t* tex = static_cast<const uint32_t*>(slot.texture.data);
                for (uint32_t ty = 0; ty < th; ty++)
                    for (uint32_t tx = 0; tx < tw; tx++) {
                        uint32_t m = 0;
                        for (int b = 0; b < 3; b++) m |= (((tx >> b) & 1u) << (2 * b)) | (((ty >> b) & 1u) << (2 * b + 1));
                        scratch[size_t(ty) * tw + tx] = tex[(size_t(ty >> 3) * (tw >> 3) + (tx >> 3)) * 64 + m];
                    }
            }
            // El bit de alfa de una imagen RGBA16 del N64 lo pone el RDP en cada pixel que dibuja un triangulo
            // (cobertura), no sale del combinador; el fondo se queda con el del relleno (0 = transparente).
            // Aqui: dibujado = la z ya no tiene el valor con que se limpio.
            static uint32_t* zscratch = nullptr;
            static size_t zscratchPx = 0;
            const uint32_t* ztex = slot.target ? static_cast<const uint32_t*>(slot.target->frameBuf.depthBuf) : nullptr;
            uint32_t zclear = 0;
            if (ztex) {
                if (zscratchPx < size_t(tw) * th) {
                    if (zscratch) free(zscratch);
                    zscratch = static_cast<uint32_t*>(malloc(size_t(tw) * th * 4));
                    zscratchPx = zscratch ? size_t(tw) * th : 0;
                }
                if (!zscratch) ztex = nullptr;
            }
            if (ztex) {
                for (uint32_t ty = 0; ty < th; ty++)
                    for (uint32_t tx = 0; tx < tw; tx++) {
                        uint32_t m = 0;
                        for (int b = 0; b < 3; b++) m |= (((tx >> b) & 1u) << (2 * b)) | (((ty >> b) & 1u) << (2 * b + 1));
                        zscratch[size_t(ty) * tw + tx] = ztex[(size_t(ty >> 3) * (tw >> 3) + (tx >> 3)) * 64 + m] & 0xFFFFFFu;
                    }
                // valor de limpieza: el que mas se repite en las 4 esquinas de la parte usada
                // esquinas de la imagen (mismo mapeo que la copia de abajo, modo 5)
                const uint32_t y0 = th > r.w ? th - r.w : 0, y1 = (y0 + r.w - 1 < th) ? y0 + r.w - 1 : th - 1;
                const uint32_t x1 = r.h - 1 < tw ? r.h - 1 : tw - 1;
                const uint32_t c[4] = { zscratch[size_t(y0) * tw], zscratch[size_t(y0) * tw + x1],
                                        zscratch[size_t(y1) * tw], zscratch[size_t(y1) * tw + x1] };
                int best = 0, bestN = 0;
                for (int i = 0; i < 4; i++) { int n = 0; for (int j = 0; j < 4; j++) n += c[j] == c[i]; if (n > bestN) { bestN = n; best = i; } }
                zclear = c[best];
            }
            const bool fillA = r.fbId > 0 && r.fbId < 64 && gPsFbFillA3DS[r.fbId];
            uint8_t* dst = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(r.addr));
            const int o = gPsFbOrient3DS;
            for (uint32_t gy = 0; gy < r.h; gy++) {
                for (uint32_t gx = 0; gx < r.w; gx++) {
                    const uint32_t ax = (o & 2) ? r.w - 1 - gx : gx, ay = (o & 4) ? r.h - 1 - gy : gy;
                    // con el giro (modo 5) la x del juego va por las filas de la textura, alineada al final: una imagen
                    // mas angosta que la textura (las fotos de 76 en una de 128) empieza en la fila th - w
                    const uint32_t tx = (o & 1) ? ay : ax, ty = (o & 1) ? ax + (th > r.w ? th - r.w : 0) : ay;
                    if (tx >= tw || ty >= th) continue;
                    const uint32_t px = scratch[size_t(ty) * tw + tx]; // 0xRRGGBBAA
                    const uint32_t R = px >> 24, G = (px >> 16) & 0xFF, B = (px >> 8) & 0xFF, A = px & 0xFF;
                    const bool drawn = ztex ? (zscratch[size_t(ty) * tw + tx] != zclear || fillA) : A >= 128;
                    const uint16_t c = static_cast<uint16_t>(((R >> 3) << 11) | ((G >> 3) << 6) | ((B >> 3) << 1) | (drawn ? 1 : 0));
                    uint8_t* d = dst + (size_t(gy) * r.w + gx) * 2;
                    d[0] = static_cast<uint8_t>(c >> 8);
                    d[1] = static_cast<uint8_t>(c);
                }
            }
            if (gPsFbWrittenN3DS < 16) {
                gPsFbWritten3DS[gPsFbWrittenN3DS][0] = r.addr;
                gPsFbWritten3DS[gPsFbWrittenN3DS][1] = r.w * r.h * 2;
                gPsFbWrittenN3DS++;
            }
            gPsFbCopies3DS++;
        }
        sPsFbPending3DS.clear();
    };
    if (!sPsFbPending3DS.empty()) sPsFbDoCopies3DS(); // respaldo: lo que quedo pendiente del cuadro anterior
""")

# la z se perdia: con enums cortos C3D_DEPTHTYPE(GPU_RB_DEPTH24_STENCIL8) solo llena un byte de la union y el resto
# queda negativo, asi que citro3d creaba el destino sin z; se arma el valor completo a mano
rep("""    slot.target = C3D_RenderTargetCreateFromTex(
        &slot.texture, GPU_TEXFACE_2D, 0, hasDepthBuffer ? C3D_DEPTHTYPE(GPU_RB_DEPTH24_STENCIL8) : C3D_DEPTHTYPE(-1));""",
    """    C3D_DEPTHTYPE psDepth3DS(-1);
    if (hasDepthBuffer) {
        static_assert(sizeof(C3D_DEPTHTYPE) == sizeof(int), "C3D_DEPTHTYPE");
        const int v = static_cast<int>(GPU_RB_DEPTH24_STENCIL8);
        memcpy(&psDepth3DS, &v, sizeof v);
    }
    slot.target = C3D_RenderTargetCreateFromTex(&slot.texture, GPU_TEXFACE_2D, 0, psDepth3DS);""")

rep("""    C3D_ClearBits clear = static_cast<C3D_ClearBits>((color ? C3D_CLEAR_COLOR : 0) | (depth ? C3D_CLEAR_DEPTH : 0));
    if (clear != 0 && mImpl->activeTarget != nullptr) {""", """    C3D_ClearBits clear = static_cast<C3D_ClearBits>((color ? C3D_CLEAR_COLOR : 0) | (depth ? C3D_CLEAR_DEPTH : 0));
    // un destino sin z no se puede limpiar solo la z (el relleno iria a la direccion 0 y el GPU se traba)
    if ((clear & C3D_CLEAR_DEPTH) && mImpl->activeTarget != nullptr && mImpl->activeTarget->frameBuf.depthBuf == nullptr) {
        clear = static_cast<C3D_ClearBits>(clear & ~C3D_CLEAR_DEPTH);
    }
    if (clear != 0 && mImpl->activeTarget != nullptr) {""")

# skin con luz en el GPU: la entrada de la sombra lleva la normal; esa es la variable aunque todas sean iguales
rep("""    // Uniform inputs still need distinct TEV sources. In particular, karts""",
    """    if (psSkin && gPsSkinLit3DS && gPsSkinVarying3DS >= 0 && gPsSkinVarying3DS < program->numInputs) varyingInput = gPsSkinVarying3DS;
    // Uniform inputs still need distinct TEV sources. In particular, karts""")

# Cronometros opcionales (PS_PROF=1): indices 10..13 de gPsProf (los imprime el host en [p])
import os, re
if os.environ.get("PS_PROF"):
    hdr = """
#include <3ds/svc.h>
#include <alloca.h>
#include <new>
extern "C" { extern uint64_t gPsProf[24]; extern uint32_t gPsProfCount[24]; }
namespace { struct PsProfB3DS { int id; uint64_t t0; explicit PsProfB3DS(int i) : id(i), t0(svcGetSystemTick()) {}
    ~PsProfB3DS() { gPsProf[id] += svcGetSystemTick() - t0; gPsProfCount[id]++; } }; }
"""
    i = src.index("\nnamespace")
    src = src[:i] + hdr + src[i:]
    for fn, idx in (("DrawTriangles", 10), ("ApplyCombinerState", 11), ("ApplyNativeVertexState", 12)):
        pat = re.compile(r"(\n[^\n]*GfxRenderingAPICitro3D::" + fn + r"\([^;{]*\)\s*\{\n)")
        src, k = pat.subn(lambda m, idx=idx: m.group(1) + "    PsProfB3DS _psprofb(%d);\n" % idx, src, count=1)
        if not k: sys.exit("patch_citro3d: no encontre " + fn)
    src = src.replace("""    RequireGpuCommandRoom(mk64_3ds::kGpuCommandTailWords + mk64_3ds::kGpuCommandDrawWords);
    const size_t sourceVertexCount = std::min(bufVboNumTris * 3""", """    PsProfB3DS* _psprofpre = new (alloca(sizeof(PsProfB3DS))) PsProfB3DS(15);
    RequireGpuCommandRoom(mk64_3ds::kGpuCommandTailWords + mk64_3ds::kGpuCommandDrawWords);
    const size_t sourceVertexCount = std::min(bufVboNumTris * 3""", 1)
    src = src.replace("""    {
        mk64_3ds::PerformanceTimer packTimer(mk64_3ds::PerformanceCurrent().vertex_pack_us);""", """    _psprofpre->~PsProfB3DS();
    {
        PsProfB3DS _psprofpack(14);
        mk64_3ds::PerformanceTimer packTimer(mk64_3ds::PerformanceCurrent().vertex_pack_us);""", 1)
    src = src.replace("    C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(firstVertex), static_cast<int>(vertexCount));",
                      "    { PsProfB3DS _psprofd(13); C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(firstVertex), static_cast<int>(vertexCount)); }", 1)

# F2: dibujo residente de modelos con huesos (codigo en host/ps_resident_backend.inc)
# capa del HUD: mientras se captura, el alfa del destino guarda la transmitancia (ver ps_resident_backend.inc)
rep("""void GfxRenderingAPICitro3D::SetUseAlpha(bool useAlpha) {""", """bool gPsLayerCapturing3DS = false;
void GfxRenderingAPICitro3D::SetUseAlpha(bool useAlpha) {
    if (gPsLayerCapturing3DS) {
        mImpl->useAlpha = useAlpha;
        const bool cutoutL = mImpl->currentProgram && mImpl->currentProgram->textureEdge;
        const bool blendL = mk64_3ds::Fast3DAlphaBlend(useAlpha, cutoutL);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, blendL ? GPU_SRC_ALPHA : GPU_ONE,
                       blendL ? GPU_ONE_MINUS_SRC_ALPHA : GPU_ZERO, GPU_ZERO,
                       blendL ? GPU_ONE_MINUS_SRC_ALPHA : GPU_ZERO);
        return;
    }""")
src += "\n#include \"ps_resident_backend.inc\"\n"
if "#include <initializer_list>" not in src:
    src = "#include <initializer_list>\n" + src
# Imagenes aparte (G_PS_FB): se dibujan giradas como la pantalla (C3D_SetViewport(y, x, alto, ancho)), asi que
# la textura necesita el ancho para el ALTO de la imagen y el alto para su ANCHO. Con las dos potencias iguales
# (128x96, 76x76) daba lo mismo; el tablero del final de la pelea usa 128x192: la textura quedaba de 128x256, el
# dibujo se salia de cada fila (los Pokemon salian cortados y con la parte de arriba abajo) y la copia no
# alcanzaba las primeras 64 filas.
rep("""    const uint16_t textureWidth = NextPowerOfTwo(width);
    const uint16_t textureHeight = NextPowerOfTwo(height);
    if (slot.initialized && slot.logicalWidth == width""", """    const uint16_t textureWidth = NextPowerOfTwo(height);
    const uint16_t textureHeight = NextPowerOfTwo(width);
    if (slot.initialized && slot.logicalWidth == width""")
# --- Formato 4:3 (opcion de la pantalla de abajo, gb_saves.inc) ---
# Normal: la imagen de 320x240 del juego se estira a los 400 de la pantalla de arriba. Con gPsAspect43_3DS el area de
# dibujo y el recorte de la pantalla (no las imagenes aparte) se pasan a los 320 del centro; nada mas cambia.
rep("void GfxRenderingAPICitro3D::SetViewport(int x, int y, int width, int height) {\n",
    """extern "C" { bool gPsAspect43_3DS = false; }
static inline void PsAspect43Map3DS(bool top, int& x, int& width) {
    if (!top || !gPsAspect43_3DS) return;
    const int x0 = 40 + (x * 4 + 2) / 5, x1 = 40 + ((x + width) * 4 + 2) / 5;
    x = x0;
    width = x1 - x0;
}
void GfxRenderingAPICitro3D::SetViewport(int x, int y, int width, int height) {
""")
rep("""    mImpl->viewportX = x;
    mImpl->viewportY = y;
    mImpl->viewportWidth = width;
    mImpl->viewportHeight = height;
    C3D_SetViewport(static_cast<uint32_t>(std::max(0, y)), static_cast<uint32_t>(std::max(0, x)),""",
    """    PsAspect43Map3DS(mImpl->activeTarget == mImpl->topTarget, x, width);
    mImpl->viewportX = x;
    mImpl->viewportY = y;
    mImpl->viewportWidth = width;
    mImpl->viewportHeight = height;
    C3D_SetViewport(static_cast<uint32_t>(std::max(0, y)), static_cast<uint32_t>(std::max(0, x)),""")
rep("""    mImpl->scissorX = x;
    mImpl->scissorY = y;
    mImpl->scissorWidth = width;
    mImpl->scissorHeight = height;
    mImpl->scissorEnabled = width > 0 && height > 0;
    ApplyViewportScissor();
}""", """    PsAspect43Map3DS(mImpl->activeTarget == mImpl->topTarget, x, width);
    mImpl->scissorX = x;
    mImpl->scissorY = y;
    mImpl->scissorWidth = width;
    mImpl->scissorHeight = height;
    mImpl->scissorEnabled = width > 0 && height > 0;
    ApplyViewportScissor();
}""")
# las franjas de los lados quedan en negro (la imagen se conserva entre cuadros: basta borrar al principio)
rep("""    C3D_FrameDrawOn(mImpl->gameTarget);
    RestoreFast3DState();
}""", """    { static int sBars; if (gPsAspect43_3DS && sBars < 2) { sBars++; C3D_RenderTargetClear(mImpl->topTarget, C3D_CLEAR_ALL, 0x000000FF, 0); } }
    C3D_FrameDrawOn(mImpl->gameTarget);
    RestoreFast3DState();
}""")
# limpieza del color de una imagen aparte (G_PS_FB): con alfa 0, para que la copia a la memoria del juego sepa que
# pixeles pinto un rectangulo opaco (el fondo de la ventana del modelo) y cuales quedaron sin tocar
rep("""        C3D_RenderTargetClear(mImpl->activeTarget, clear, 0x000000FF, 0);""",
    """        C3D_RenderTargetClear(mImpl->activeTarget, clear, gPsClearRgba3DS, 0);""")
src = src.replace("\nnamespace", "\nextern \"C\" uint32_t gPsClearRgba3DS;\nuint32_t gPsClearRgba3DS = 0x000000FF;\nnamespace", 1)
# pixel "dibujado" (bit de alfa del RGBA16): un triangulo cambio la z, o un rectangulo opaco dejo alfa en el color
rep("""const bool drawn = ztex ? (zscratch[size_t(ty) * tw + tx] != zclear || fillA) : A >= 128;""",
    """const bool drawn = ztex ? (zscratch[size_t(ty) * tw + tx] != zclear || fillA || A >= 128) : A >= 128;""")
open(sys.argv[2], "w").write(src)


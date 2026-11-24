#!/usr/bin/env python3
"""G_PS_FB (0x4D): el juego dibuja triangulos en una imagen aparte (cambiando el color image). El convertidor
cambia ese SETCIMG por este comando: el interprete dibuja en un framebuffer del GPU del mismo tamano y el
backend copia el resultado a la direccion del juego al terminar el cuadro (PsFbQueueReadback3DS).
El siguiente SETCIMG (pantalla, z-buffer u otra imagen) regresa al dibujo normal.

Uso: patch_interp_fb.py interprete.cpp   (despues de los demas parches del interprete)
"""
import sys

p = sys.argv[1]
s = open(p).read()


def rep(old, new):
    global s
    if old not in s:
        sys.exit("patch_interp_fb: no encontre: " + old[:80])
    s = s.replace(old, new, 1)


handler = r'''
// ---------------- G_PS_FB: imagen aparte dibujada en el GPU (Stadium) ----------------
extern "C" void PsFbQueueReadback3DS(int fbId, uint32_t addr, uint32_t w, uint32_t h);
extern "C" void PsLog3DS(const char* fmt, ...);
extern "C" uint32_t gPsFbStats3DS[4]; // cambios a imagen del GPU, regresos, llamadas, rechazos
uint32_t gPsFbStats3DS[4];
static std::unordered_map<uint32_t, int> sPsFbIds3DS; // direccion del juego -> framebuffer
static bool sPsFbActive3DS = false;
extern "C" bool gPsFbActive3DS; // el host no aplica el centrado/reflejo de la pantalla de arriba mientras esta activa
bool gPsFbActive3DS = false;
static void PsFbForgetRects3DS(Interpreter* gfx) { // que el viewport y el recorte se vuelvan a mandar al cambiar de destino
    memset(&gfx->mRenderingState.viewport, 0xFF, sizeof(gfx->mRenderingState.viewport));
    memset(&gfx->mRenderingState.scissor, 0xFF, sizeof(gfx->mRenderingState.scissor));
}
static void PsFbLeave3DS(Interpreter* gfx) {
    if (!sPsFbActive3DS) return;
    gfx->Flush();
    sPsFbActive3DS = false;
    gPsFbActive3DS = false;
    PsFbForgetRects3DS(gfx);
    gfx->mFbActive = false;
    gfx->mActiveFrameBuffer = gfx->mFrameBuffers.end();
    gfx->mRapi->StartDrawToFramebuffer(gfx->mRendersToFb ? gfx->mGameFb : 0,
                                       (float)gfx->mCurDimensions.height / gfx->mNativeDimensions.height);
    gfx->mRdp->viewport_or_scissor_changed = true;
    gPsFbStats3DS[1]++;
}
extern "C" uint32_t gPsFbWritten3DS[16][2];
extern "C" int gPsFbWrittenN3DS;
extern "C" uint32_t gPsFbInval3DS;
uint32_t gPsFbInval3DS = 0;
// al empezar el cuadro (despues de la copia del backend): fuera de la cache lo que se leyo de esas imagenes
static void PsFbInvalidateWritten3DS(Interpreter* gfx) {
    for (int k = 0; k < gPsFbWrittenN3DS; k++) {
        const uintptr_t lo = gPsFbWritten3DS[k][0], hi = lo + gPsFbWritten3DS[k][1];
        for (;;) {
            const uint8_t* hit = nullptr;
            for (auto it = gfx->mTextureCache.map.begin(); it != gfx->mTextureCache.map.end(); ++it) {
                const uintptr_t a = reinterpret_cast<uintptr_t>(it->first.texture_addr);
                if (a >= lo && a < hi) { hit = it->first.texture_addr; break; }
            }
            if (hit == nullptr) break;
            gfx->TextureCacheDelete(hit);
            gPsFbInval3DS++;
        }
    }
    gPsFbWrittenN3DS = 0;
}
// bit de alfa del ultimo relleno (modo FILL) de cada imagen: el RDP lo escribe tal cual en los pixeles que no
// toca ningun triangulo (la copia lo usa para el fondo)
extern "C" uint8_t gPsFbFillA3DS[64];
uint8_t gPsFbFillA3DS[64];
extern "C" int gPsFbCurId3DS;
int gPsFbCurId3DS = 0;
extern "C" uint32_t gPsClearRgba3DS;
static uint32_t sPsFbFrame3DS = 1;      // cuadro en curso (sube en StartFrame)
static uint32_t sPsFbSeenFrame3DS[64];  // ultimo cuadro en que se entro a cada imagen
bool gfx_ps_fb_handler(F3DGfx** cmd0) {
    Interpreter* gfx = mInstance;
    F3DGfx* cmd = *cmd0;
    const uint32_t* q = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(cmd->words.w1));
    gPsFbStats3DS[2]++;
    if (q == nullptr || q[0] != 0x50534642u || q[2] == 0 || q[3] == 0) { gPsFbStats3DS[3]++; return false; }
    gfx->Flush();
    int& id = sPsFbIds3DS[q[1]];
    if (id <= 0) id = gfx->CreateFrameBuffer(q[2], q[3], q[2], q[3], 0, false);
    if (id <= 0) { gPsFbStats3DS[3] += 100; return false; }
    gPsFbCurId3DS = id;
    gfx->SetFrameBuffer(id, 1.0f);
    // La primera vez que se entra a la imagen en el cuadro se limpia su color: el juego la redibuja completa (fondo o
    // relleno, luego el modelo). Si su fondo no llega a dibujarse aqui, sin esto el modelo dejaba rastro cuadro tras
    // cuadro sobre lo que hubiera antes en esa memoria de video.
    if (id < 64 && sPsFbSeenFrame3DS[id] != sPsFbFrame3DS) {
        sPsFbSeenFrame3DS[id] = sPsFbFrame3DS;
        gPsFbFillA3DS[id] = 0;
        gPsClearRgba3DS = 0; // alfa 0: sin tocar (ver la copia en patch_citro3d.py)
        gfx->mRapi->ClearFramebuffer(true, false);
        gPsClearRgba3DS = 0x000000FF;
    }
    // El juego limpia una imagen aparte asi: color image = su z, relleno; color image = la imagen, relleno. El
    // segundo cambio llega aqui, no a GfxDpSetColorImage: sin esto el RDP seguia creyendo que dibujaba en la z y
    // el relleno de color se descartaba (el retrato nuevo salia encima del anterior, y con la z vieja, cortado).
    gfx->mRdp->color_image_address = reinterpret_cast<void*>(static_cast<uintptr_t>(q[1]));
    // El z que esta puesto al entrar a una imagen aparte es el de esa imagen: se anota para reconocer su limpieza
    // aunque llegue despues (ver PsFbIsFbZ3DS)
    if (gfx->mRdp->z_buf_address != nullptr && gfx->mRdp->z_buf_address != sPsMainZ3DS && !PsFbIsFbZ3DS(gfx->mRdp->z_buf_address) && sPsFbZN3DS < 8)
        sPsFbZs3DS[sPsFbZN3DS++] = gfx->mRdp->z_buf_address;
    if (sPsFbZCleared3DS != nullptr && sPsFbZCleared3DS == gfx->mRdp->z_buf_address) gfx->mRapi->ClearFramebuffer(false, true);
    sPsFbZCleared3DS = nullptr;
    gfx->mActiveFrameBuffer = gfx->mFrameBuffers.find(id);
    gfx->mFbActive = true;
    gfx->mRdp->viewport_or_scissor_changed = true;
    sPsFbActive3DS = true;
    gPsFbActive3DS = true;
    PsFbForgetRects3DS(gfx);
    PsFbQueueReadback3DS(id, q[1], q[2], q[3]);
    gPsFbStats3DS[0]++;
    return false;
}
'''

rep("bool gfx_set_c_img_handler_rdp(F3DGfx** cmd0) {", handler + "\n#include \"ps_hud_interp.inc\"\n" + "\nbool gfx_set_c_img_handler_rdp(F3DGfx** cmd0) {")
# cualquier color image normal termina el dibujo en la imagen del GPU
rep("""    gfx->GfxDpSetColorImage(C0(21, 3), C0(19, 2), C0(0, 11), gfx->SegAddr(cmd->words.w1));
    return false;""", """    PsFbLeave3DS(gfx);
    gfx->GfxDpSetColorImage(C0(21, 3), C0(19, 2), C0(0, 11), gfx->SegAddr(cmd->words.w1));
    if (gfx->mRdp->color_image_address != gfx->mRdp->z_buf_address && !PsFbIsFbZ3DS(gfx->mRdp->z_buf_address)) sPsMainZ3DS = gfx->mRdp->z_buf_address;
    return false;""")
# al empezar un cuadro no puede quedar activa
rep("void Interpreter::StartFrame() {", "void Interpreter::StartFrame() {\n    sPsFbActive3DS = false;\n    gPsFbActive3DS = false;\n    sPsFbFrame3DS++;")
rep("""    { (int8_t)0x41, { "G_PS_SKIN", gfx_ps_skin_handler } },""",
    """    { (int8_t)0x41, { "G_PS_SKIN", gfx_ps_skin_handler } },
    { (int8_t)0x4D, { "G_PS_FB", gfx_ps_fb_handler } },                              // Stadium: imagen aparte en el GPU
    { (int8_t)0x4E, { "G_PS_HUD", gfx_ps_hud_handler } },                            // Stadium: capa del HUD""")
# despues de que el backend empieza el cuadro (ahi se hace la copia), sacar de la cache lo que cambio
_a = """    mRapi->StartFrame();
    mRapi->StartDrawToFramebuffer(mRendersToFb ? mGameFb : 0"""
if s.count(_a) < 1:
    sys.exit("patch_interp_fb: no encontre el inicio del cuadro")
s = s.replace(_a, """    mRapi->StartFrame();
    PsFbInvalidateWritten3DS(this);
    mRapi->StartDrawToFramebuffer(mRendersToFb ? mGameFb : 0""")
# El N64 nunca borra el framebuffer por su cuenta: el juego lo limpia con rellenos cuando quiere, y algunas
# pantallas (seleccion de Pokemon) dibujan el fondo y los paneles solo en los primeros cuadros y despues
# dibujan encima. Al empezar el cuadro solo se limpia la z; el color se conserva (limpiarfb.txt = como antes).
_c = """    mRapi->StartDrawToFramebuffer(mRendersToFb ? mGameFb : 0, (float)mCurDimensions.height / mNativeDimensions.height);
    mRapi->ClearFramebuffer(true, true);"""
if s.count(_c) < 1:
    sys.exit("patch_interp_fb: no encontre la limpieza del inicio del cuadro")
s = s.replace(_c, """    mRapi->StartDrawToFramebuffer(mRendersToFb ? mGameFb : 0, (float)mCurDimensions.height / mNativeDimensions.height);
    mRapi->ClearFramebuffer(gPsClearFb3DS, true);""")
# con mRendersToFb el dibujo va al mismo destino (en el 3DS el framebuffer 0 es la pantalla) y esa limpieza se
# encola antes del dibujo pendiente del cuadro: tambien borraba lo que el juego espera que siga ahi
_d = """        mRapi->StartDrawToFramebuffer(0, 1);
        mRapi->ClearFramebuffer(true, true);"""
if s.count(_d) < 1:
    sys.exit("patch_interp_fb: no encontre la limpieza de mRendersToFb")
s = s.replace(_d, """        mRapi->StartDrawToFramebuffer(0, 1);
        mRapi->ClearFramebuffer(gPsClearFb3DS, true);""")
s = s.replace(handler, handler + """
extern "C" bool gPsClearFb3DS;
bool gPsClearFb3DS = false; // true: limpiar el color en cada cuadro (comportamiento anterior)
""", 1) if False else s
s = s.replace("bool gfx_ps_fb_handler(F3DGfx** cmd0) {", """extern "C" bool gPsClearFb3DS;
bool gPsClearFb3DS = false; // true: limpiar el color en cada cuadro (comportamiento anterior)
bool gfx_ps_fb_handler(F3DGfx** cmd0) {""", 1)
rep("""void Interpreter::GfxDpFillRectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {""",
    """extern "C" bool gPsFbActive3DS;
extern "C" int gPsFbCurId3DS;
extern "C" uint8_t gPsFbFillA3DS[64];
static void* sPsMainZ3DS = nullptr;      // z-buffer de la pantalla (el que esta puesto cuando se fija el color image normal)
static void* sPsFbZCleared3DS = nullptr; // z de una imagen aparte que el juego acaba de limpiar
// z-buffers de las imagenes aparte. En las pantallas de menu (2D) el juego solo pone un z: el de la imagen donde
// dibuja el modelo del Pokemon (resumen del laboratorio, renta). Ese z seguia puesto al empezar el cuadro siguiente
// y se tomaba por el de la pantalla; entonces su limpieza (que ahi llega despues de dibujar el fondo de la imagen)
// no se reconocia, la imagen conservaba la z anterior y el modelo no salia.
static void* sPsFbZs3DS[8];
static int sPsFbZN3DS = 0;
static bool PsFbIsFbZ3DS(void* z) {
    for (int i = 0; i < sPsFbZN3DS; i++) if (sPsFbZs3DS[i] == z) return true;
    return false;
}
void Interpreter::GfxDpFillRectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    if (gPsFbActive3DS && mRdp->color_image_address != mRdp->z_buf_address && gPsFbCurId3DS > 0 && gPsFbCurId3DS < 64)
        gPsFbFillA3DS[gPsFbCurId3DS] = mRdp->fill_color.a >= 128;
    if (mRdp->color_image_address == mRdp->z_buf_address &&
        (PsFbIsFbZ3DS(mRdp->z_buf_address) || (sPsMainZ3DS != nullptr && mRdp->z_buf_address != sPsMainZ3DS))) {
        sPsFbZCleared3DS = mRdp->z_buf_address;
        return;
    }""")
rep("""    if (depth_test_and_mask != gfx->mRenderingState.depth_test_and_mask) {""", """    { extern bool gPsHudCapturing3DS; void PsHudAbort3DS(Interpreter*); if ((depth_test_and_mask & 1) && gPsHudCapturing3DS) PsHudAbort3DS(gfx); }
    if (depth_test_and_mask != gfx->mRenderingState.depth_test_and_mask) {""")
rep("""    if ((mRdp->other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_COPY) {""",
    """    // Stadium: un rectangulo en modo copia dentro de una imagen aparte es su fondo (la rejilla detras del modelo):
    // la imagen completa cuenta como dibujada al copiarla a la memoria del juego (bit de alfa)
    if (gPsFbActive3DS && gPsFbCurId3DS > 0 && gPsFbCurId3DS < 64 && (mRdp->other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_COPY)
        gPsFbFillA3DS[gPsFbCurId3DS] = 1;
    if ((mRdp->other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_COPY) {""")
rep("""void Interpreter::GfxDpTextureRectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry, uint8_t tile, int16_t uls,""",
    """extern "C" bool gPsFbActive3DS;
extern "C" int gPsFbCurId3DS;
extern "C" uint8_t gPsFbFillA3DS[64];
void Interpreter::GfxDpTextureRectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry, uint8_t tile, int16_t uls,""")
open(p, "w").write(s)
print("patch_interp_fb: ok")

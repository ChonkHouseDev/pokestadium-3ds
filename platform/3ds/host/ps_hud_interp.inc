// Capa del HUD (G_PS_HUD, 0x4E). El convertidor marca tramos del HUD (rectangulos con textura y su estado) con
// una llave del contenido (comandos + texturas + matrices + vertices que usan). Si un tramo no cambio desde que
// se capturo, el interprete no lo ejecuta: copia su capa (una textura del tamano de la pantalla) y deja el estado
// del RDP/RSP como quedo al capturar. Si cambio (texto nuevo, barra de vida moviendose) se dibuja como siempre;
// cuando lleva 2 cuadros igual se captura. Hay 2 capas: lo que va antes y despues de la parte del HUD que cambia
// en cada cuadro (p. ej. el cursor que parpadea), que se sigue dibujando normal entre las dos.
extern "C" bool gPsHudEnabled3DS;
bool gPsHudEnabled3DS = true;
// 0 aciertos, 1 capturas, 2 dibujado normal, 3 sin destino (memoria/destino), 4 cortadas por z, 5 llaves nuevas,
// 6 KB de memoria de video libres al crear la capa, 7 llaves nuevas solo por el estado con que llega el tramo
extern "C" uint32_t gPsHudStats3DS[8];
uint32_t gPsHudStats3DS[8];
static constexpr int kPsHudLayers3DS = 2;
struct PsHud3DS {
    uint64_t key = 0, key2 = 0, seenKey = 0, seenKey2 = 0, capKey = 0, capKey2 = 0, badKey = 0;
    uint32_t seen = 0, badStreak = 0, pause = 0;
    bool valid = false;
    RDP rdp;
    RSP rsp;
};
static PsHud3DS* sPsHud3DS = nullptr; // [kPsHudLayers3DS]
static int sPsHudCapLayer3DS = -1;
bool gPsHudCapturing3DS = false;
// Estado vivo del que depende el tramo. mask: lo que el propio tramo fija antes de su primer dibujo (no cuenta)
extern "C" void host_print(const char* s);
extern "C" bool gPsHudDbg3DS;
bool gPsHudDbg3DS = false;
static uint32_t sPsHudParts3DS[kPsHudLayers3DS][12];
static uint64_t PsHudStateKey3DS(Interpreter* gfx, uint32_t mask, int layer) {
    uint32_t a = 2166136261u, b = 0x9E3779B9u;
    auto mix = [&](const void* p, size_t n) {
        const uint8_t* q = static_cast<const uint8_t*>(p);
        size_t i = 0;
        for (; i + 4 <= n; i += 4) {
            uint32_t v; std::memcpy(&v, q + i, 4);
            a = (a ^ v) * 0x9E3779B1u; a ^= a >> 15; b = (b + v) * 0x85EBCA77u; b ^= b >> 13;
        }
        for (; i < n; i++) { a = (a ^ q[i]) * 16777619u; b = (b + q[i]) * 0x85EBCA77u; b ^= b >> 13; }
    };
    uint32_t parts[12];
    int np = 0;
    auto part = [&]() { parts[np++] = a ^ b; };
    const RDP* r = gfx->mRdp;
    const RSP* s = gfx->mRsp;
    if (!(mask & 1)) { mix(&r->other_mode_l, sizeof r->other_mode_l); mix(&r->other_mode_h, sizeof r->other_mode_h); }
    part(); // 0
    if (!(mask & 2)) mix(&r->combine_mode, sizeof r->combine_mode);
    if (!(mask & 4)) mix(&r->scissor, sizeof r->scissor);
    part(); // 1
    if (!(mask & 8)) { mix(&r->prim_color, sizeof r->prim_color); mix(&r->prim_lod_fraction, 1); }
    part(); // 2
    if (!(mask & 16)) mix(&r->env_color, sizeof r->env_color);
    part(); // 3
    if (!(mask & 32)) mix(&r->fog_color, sizeof r->fog_color);
    if (!(mask & 64)) mix(&r->fill_color, sizeof r->fill_color);
    if (!(mask & 128)) mix(&r->blend_color, sizeof r->blend_color);
    part(); // 4
    mix(&r->viewport, sizeof r->viewport);
    mix(&r->prim_depth, sizeof r->prim_depth);
    part(); // 5
    // texturas: descriptores, de donde se cargaron y una muestra de lo cargado
    if (!(mask & 512)) {
        mix(&r->first_tile_index, 1);
        for (int t = 0; t < 8; t++) {
            const auto& d = r->texture_tile[t];
            const uint32_t w[4] = { (uint32_t)d.fmt | (d.siz << 8) | (d.cms << 16) | (d.cmt << 24), (uint32_t)d.masks | (d.maskt << 8) | (d.shifts << 16) | (d.shiftt << 24),
                                    (uint32_t)d.tmem | (d.palette << 16) | (d.tmem_index << 24), d.line_size_bytes };
            mix(w, 16);
            mix(&d.uls, 16);
        }
        part(); // 6
        mix(&r->texture_to_load.addr, sizeof(void*)); mix(&r->texture_to_load.width, 4);
        for (int t = 0; t < 2; t++) {
            const auto& l = r->loaded_texture[t];
            mix(&l.addr, sizeof(void*)); mix(&l.size_bytes, 4); mix(&l.line_size_bytes, 4);
            if (l.addr != nullptr && l.size_bytes >= 32 && l.size_bytes <= 4096) { mix(l.addr, 16); mix(l.addr + l.size_bytes - 16, 16); }
            mix(&r->palette_dram_addr[t], sizeof(void*));
        }
        part(); // 7
    } else { part(); part(); }
    mix(&s->geometry_mode, sizeof s->geometry_mode);
    mix(&s->texture_scaling_factor, sizeof s->texture_scaling_factor);
    part(); // 8
    if (!(mask & 256)) mix(s->MP_matrix, sizeof s->MP_matrix);
    part(); // 9
    if (gPsHudDbg3DS) {
        char t[120]; int o = std::snprintf(t, sizeof t, "[huddbg] estado capa %d mascara %03lX cambia:", layer, (unsigned long)mask);
        bool any = false;
        for (int i = 0; i < np; i++) if (parts[i] != sPsHudParts3DS[layer][i]) { o += std::snprintf(t + o, sizeof t - o, " %d", i); any = true; }
        if (any) { std::snprintf(t + o, sizeof t - o, "\n"); host_print(t); }
    }
    std::memcpy(sPsHudParts3DS[layer], parts, sizeof parts);
    return ((uint64_t)a << 32) | b;
}
// despues de componer o de cambiar de destino: que el siguiente dibujo vuelva a mandar todo su estado
static void PsHudResync3DS(Interpreter* gfx) {
    gfx->mRenderingState.depth_test_and_mask = 0xFF;
    PsFbForgetRects3DS(gfx);
    gfx->mRdp->viewport_or_scissor_changed = true;
    gfx->mRdp->textures_changed[0] = gfx->mRdp->textures_changed[1] = true;
    PsForgetBoundTextures3DS();
    sPsStateEpoch3DS++;
    PsStateChanged3DS();
}
// Un dibujo con prueba de z dentro del tramo: la capa no sirve (su z no es el de la escena). Se cierra aqui, se
// compone lo capturado (da lo mismo que haberlo dibujado directo) y el resto del tramo se dibuja como siempre.
void PsHudAbort3DS(Interpreter* gfx) {
    if (!gPsHudCapturing3DS || sPsHud3DS == nullptr || sPsHudCapLayer3DS < 0) return;
    const int layer = sPsHudCapLayer3DS;
    PsHud3DS& h = sPsHud3DS[layer];
    auto* rapi = static_cast<GfxRenderingAPICitro3D*>(gfx->mRapi);
    gPsHudCapturing3DS = false; // antes del Flush: no volver a entrar
    sPsHudCapLayer3DS = -1;
    gfx->Flush();
    rapi->PsLayerEnd3DS();
    rapi->PsLayerComposite3DS(layer);
    h.valid = false;
    h.badKey = h.capKey;
    if (++h.badStreak >= 4) { h.pause = 600; h.badStreak = 0; }
    gPsHudStats3DS[4]++;
    PsHudResync3DS(gfx);
}
// Para el convertidor: la capa ya tiene este tramo? (entonces no lo convierte y manda la marca 3)
static uint32_t sPsHudForceFull3DS = 0;
extern "C" bool PsHudLayerReady3DS(int layer, uint32_t kA, uint32_t kB) {
    if (!gPsHudEnabled3DS || sPsHud3DS == nullptr || layer < 0 || layer >= kPsHudLayers3DS) return false;
    if (sPsHudForceFull3DS) { sPsHudForceFull3DS--; return false; }
    const PsHud3DS& h = sPsHud3DS[layer];
    return h.valid && h.key == (((uint64_t)kA << 32) | kB);
}
bool gfx_ps_hud_handler(F3DGfx** cmd0) {
    Interpreter* gfx = mInstance;
    F3DGfx* cmd = *cmd0;
    auto* rapi = static_cast<GfxRenderingAPICitro3D*>(gfx->mRapi);
    const uint32_t kind = cmd->words.w0 & 0xFF;
    if (sPsHud3DS == nullptr) sPsHud3DS = new PsHud3DS[kPsHudLayers3DS];
    if (kind == 2) { // fin del tramo
        if (!gPsHudCapturing3DS || sPsHudCapLayer3DS < 0) return false;
        const int layer = sPsHudCapLayer3DS;
        PsHud3DS& h = sPsHud3DS[layer];
        gPsHudCapturing3DS = false;
        sPsHudCapLayer3DS = -1;
        gfx->Flush();
        rapi->PsLayerEnd3DS();
        h.rdp = *gfx->mRdp;
        h.rsp = *gfx->mRsp;
        h.key = h.capKey; h.key2 = h.capKey2;
        h.valid = true;
        h.badStreak = 0;
        rapi->PsLayerComposite3DS(layer);
        PsHudResync3DS(gfx);
        return false;
    }
    const uint32_t* q = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(cmd->words.w1));
    if ((kind != 1 && kind != 3) || q == nullptr || q[0] != 0x50534842u || q[4] >= (uint32_t)kPsHudLayers3DS) return false;
    if (kind == 3) {
        // tramo sin convertir: solo existe en su capa
        PsHud3DS& h3 = sPsHud3DS[q[4]];
        gfx->Flush();
        if (gPsHudEnabled3DS && !sPsFbActive3DS && !gfx->mFbActive && !gPsHudCapturing3DS && h3.valid && h3.key == (((uint64_t)q[1] << 32) | q[2])) {
            if (PsHudStateKey3DS(gfx, q[5], (int)q[4]) != h3.key2) { gPsHudStats3DS[7]++; sPsHudForceFull3DS = 6; } // llego con otro estado: los siguientes cuadros, completo
            rapi->PsLayerComposite3DS((int)q[4]);
            *gfx->mRdp = h3.rdp;
            *gfx->mRsp = h3.rsp;
            PsHudResync3DS(gfx);
            gPsHudStats3DS[0]++;
        } else { gPsHudStats3DS[3]++; sPsHudForceFull3DS = 6; }
        *cmd0 = reinterpret_cast<F3DGfx*>(static_cast<uintptr_t>(q[3]));
        return false;
    }
    if (!gPsHudEnabled3DS || sPsFbActive3DS || gfx->mFbActive || gPsHudCapturing3DS) { gPsHudStats3DS[2]++; return false; }
    const int layer = (int)q[4];
    PsHud3DS& h = sPsHud3DS[layer];
    gfx->Flush();
    const uint64_t key = ((uint64_t)q[1] << 32) | q[2], key2 = PsHudStateKey3DS(gfx, q[5], (int)q[4]);
    if (h.valid && h.key == key && h.key2 == key2) {
        rapi->PsLayerComposite3DS(layer);
        *gfx->mRdp = h.rdp;
        *gfx->mRsp = h.rsp;
        PsHudResync3DS(gfx);
        *cmd0 = reinterpret_cast<F3DGfx*>(static_cast<uintptr_t>(q[3])); // el paso normal avanza despues del fin
        gPsHudStats3DS[0]++;
        return false;
    }
    if (h.seenKey == key && h.seenKey2 == key2) h.seen++;
    else { if (h.seenKey == key) gPsHudStats3DS[7]++; h.seenKey = key; h.seenKey2 = key2; h.seen = 1; gPsHudStats3DS[5]++; }
    if (h.pause) h.pause--;
    if (h.seen >= 2 && key != h.badKey && !h.pause && rapi->PsLayerBegin3DS(layer)) {
        gPsHudCapturing3DS = true;
        sPsHudCapLayer3DS = layer;
        h.valid = false;
        h.capKey = key; h.capKey2 = key2;
        PsHudResync3DS(gfx);
        gPsHudStats3DS[1]++;
        return false;
    }
    if (h.seen >= 2 && key != h.badKey) gPsHudStats3DS[3]++;
    gPsHudStats3DS[2]++;
    return false;
}

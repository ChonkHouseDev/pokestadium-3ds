/* Puente entre el juego y la escena de entrada del Stadium japones: arma la sesion con el formato que esa escena
 * espera (el de pokestadium0) a partir de los datos neutros de ps_jp_in.h. Se compila con los encabezados de ese decomp. */
#include "global.h"
#include "game_state.h"
#include "fragments/vs_scene/vs_scene.h"
#include "ps_jp_in.h"

s32 VsScene_Run(s32 kind, SessionContext* session);
void ps_jp_icon_slot(void* renderer, s32 slot, const void* usMon, u16 fill);
void* memcpy(void*, const void*, unsigned int);
void* memset(void*, int, unsigned int);

void ps_jp_frame(void);
void ps_jp_us_stadium_attach(void* parent);
void ps_jp_cam_shift(CameraNode* cam);
void ps_jp_arena_add(void* node);
void* ps_jp_board_block(void);
void ps_jp_board_done(void);
void ps_jp_board_pos(s32* out);
extern u8 D_8F209E54[];
void* MainPool_AllocState(u32 size, s32 side);
s32 MainPool_GetAvailable(void);
void MainPool_FinalizeAllocation(void* block);
GraphNode* GeoLayout_Process();
/* El edificio de la pantalla gigante del estadio de torneos japones (marco, letrero y banderas), puesto en la orilla
   alta de las gradas del fondo. Su hueco mide 3200x2400; a escala 0.18 es el tamano de la pantalla de la escena. */
void ImageGrid_Clear(void);
/* arma el edificio (sin colgarlo): lo usan la escena y la batalla */
void* ps_jp_board_build(void) {
    static u32 lay[12];
    void* block;
    void* node;
    s32 pos[3];
    ps_jp_board_pos(pos); /* y, z, escala: segun el estadio */
    lay[0] = 0x1F00FFFF; lay[1] = 0; lay[2] = 0; lay[3] = ((u32)(u16)pos[0] << 16) | (u16)pos[1]; /* giro 0,0,0; posicion 0,y,z */
    lay[4] = ((u32)pos[2] << 16) | (u32)pos[2]; lay[5] = (u32)pos[2] << 16; /* en centesimas; el edificio trae 0.4 por dentro: 0.45 x 0.4 = 0.18 */
    lay[6] = 0x05000000; lay[7] = 0; lay[8] = (u32)D_8F209E54; lay[9] = 0x06000000;
    lay[10] = 0x01000000; lay[11] = 0;
    block = ps_jp_board_block(); /* memoria fija, no el pool (ver ps_jp.c) */
    ImageGrid_Clear();
    node = GeoLayout_Process(block, (u8*)lay);
    ps_jp_board_done();
    return node;
}
static s32 sStadiumDone;
static SessionContext sSession;
static const PsJpIn* sIn;

/* PokeIcon_SetMonSlot de la escena: el icono lo dibuja el juego con su propio Pokemon */
void ps_jp_SetMonSlot(void* renderer, s32 slot, BattleMon* mon, u16 fill) {
    s32 s, i;
    for (s = 0; s < 2; s++)
        for (i = 0; i < 6; i++)
            if (mon == &sSession.rosterInfo[s].battleMon[i] || mon == &sSession.trainerSlots[s].pokemonPicked[i]) { ps_jp_icon_slot(renderer, slot, sIn->side[s].mon[i], fill); return; }
    ps_jp_icon_slot(renderer, slot, NULL, fill);
}

static void copy_text(char* dst, const char* src, s32 n) { s32 i; for (i = 0; i < n - 1 && src[i]; i++) dst[i] = src[i]; dst[i] = 0; }

s32 ps_jp_scene_run(const PsJpIn* in) {
    s32 s, i;
    sIn = in;
    sStadiumDone = 0;
    memset(&sSession, 0, sizeof sSession);
    sSession.mode = in->mode;
    sSession.ruleCategory = in->ruleCategory;
    sSession.modeSelection = in->modeSelection;
    sSession.roundOrDemoId = in->round;
    for (s = 0; s < 2; s++) {
        const PsJpSideIn* si = &in->side[s];
        TeamRoster* t = &sSession.trainerSlots[s];
        ExtendedRosterInfo* r = &sSession.rosterInfo[s];
        t->isHuman = (s == 0);
        t->numberOfPokemonBrought = si->count;
        copy_text(t->trainerName, si->name, sizeof t->trainerName);
        t->rosterInfo = r;
        r->numberOfPokemonCanBePicked = si->count;
        r->trainerIndex = si->trainerIndex;
        copy_text(r->title, si->title, sizeof r->title);
        copy_text(r->fullName, si->name, sizeof r->fullName);
        for (i = 0; i < 6; i++) {
            memcpy(&r->battleMon[i], si->head[i], 0x30);
            copy_text((char*)r->battleMon[i].nickname, si->nick[i], sizeof r->battleMon[i].nickname);
            t->pokemonPicked[i] = r->battleMon[i];
        }
        sSession.battleSides[s].tagTeamSize = 1;
        sSession.battleSides[s].teamMember[0] = t;
    }
    return VsScene_Run(VS_SCENE_PRE_BATTLE, &sSession);
}

/* ---- Cuadro de la escena a pantalla completa (las flamas, las tarjetas y los paneles): el del decomp, que queda con
 * el sufijo _orig, mas el paso de los sonidos. Sin esto el contador de pasos no avanzaba durante la revelacion. ---- */
s32 VsScene_DrawFrame_orig(s32 unused);
s32 VsScene_DrawFrame(s32 unused) /* la llave va abajo a proposito: el sed de build_blob_3ds.sh renombra la del decomp */
{
    ps_jp_frame();
    return VsScene_DrawFrame_orig(unused);
}

/* ---- Cuadro de la escena con la arena a la vista (reemplaza al VsScene_DrawFrameToImage del decomp, que queda
 * con el sufijo _orig): primero la arena; despues el fuego y las tarjetas directo sobre la pantalla gigante. ---- */
#include "geo_node.h"
#include "geo_render.h"
extern GraphNode* D_82203240; /* arena */
extern GraphNode* D_82203244; /* fuego y tarjetas */
extern s32 D_82203264;        /* iconos */
s32 VsScene_BeginFrame(void);
void VsScene_DrawBackground(void);
void GeoRender_AdvanceFrameCounter(void);
s32 PokeIcon_ProcessNextSlot(void* renderer, s32 arg1);
void BgStage_DrawFrame(void);
void GfxImage_ClearDepthRectangle(Gfx** gfx_p, s32 x, s32 y, s32 w, s32 h);
s32 ps_jp_grid_rect(f32 fov, f32 (*view)[4], s32* rx, s32* ry, s32* rw, s32* rh);
void ps_jp_trace(const char*);

void ps_geo_text_flush(void);
void ps_jp_gold_frame(Gfx* start);
void ps_jp_grid_gold(void);

s32 VsScene_DrawFrameToImage(s32 unused) /* la llave va abajo a proposito: el sed de build_blob_3ds.sh renombra la del decomp */
{
    s32 ret = VsScene_BeginFrame();
    CameraNode* arenaCam = (CameraNode*)D_82203240->children;
    CameraNode* fireCam = (CameraNode*)D_82203244->children;
    if (!sStadiumDone) { sStadiumDone = 1; ps_jp_us_stadium_attach((void*)arenaCam); } /* el estadio de la US en lugar de la arena (sin el edificio: va el marco dorado) */
    s32 x, y, w, h;
    Vec3f eye, target;
    (void)unused;
    GeoRender_AdvanceFrameCounter();
    ps_jp_frame();
    PokeIcon_ProcessNextSlot((void*)D_82203264, 0);
    BgStage_DrawFrame();
    VsScene_DrawBackground();
    eye = arenaCam->view.eyePosition; target = arenaCam->view.targetPosition;
    ps_jp_cam_shift(arenaCam);
    { Gfx* gs = gDisplayListHead;
    Geo_RenderRootNode(D_82203240);
    ps_jp_gold_frame(gs); }
    arenaCam->view.eyePosition = eye; arenaCam->view.targetPosition = target;
    ps_jp_grid_gold();
    if (ps_jp_grid_rect(arenaCam->perspective.verticalFieldOfView, (f32 (*)[4])&arenaCam->view.floatMatrix, &x, &y, &w, &h)) {
        GeoCamera_SetViewport(fireCam, x, y, w, h);
        GfxImage_ClearDepthRectangle(&gDisplayListHead, x < 0 ? 0 : x, y < 0 ? 0 : y, x + w > 320 ? 320 : x + w, y + h > 240 ? 240 : y + h);
        Geo_RenderRootNode(D_82203244);
        ps_geo_text_flush();
        GeoCamera_SetViewport(fireCam, 0, 0, 320, 240);
    }
    return ret;
}

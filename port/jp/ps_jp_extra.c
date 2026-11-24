/* Funciones del motor del Stadium japones que la escena usa y que el juego de EE. UU. no tiene con esa forma:
 * trayectorias de camara (Bezier) y la imagen de 320x240 dibujada como mosaico en la pantalla gigante.
 * Reescritas a partir del decomp pokestadium0 (src/game_state.c). Se compilan con los encabezados de ese decomp. */
#include "global.h"

typedef struct { s16 n; Vec3s a; Vec3s b; } PsJpKey;
typedef struct { s32 flags; PsJpKey* keys; Vec3f p0, p1, p2, p3; s16 frame, frames; } PsJpPath;

Vec3f* Vec3f_FromVec3s(Vec3f* float_vector, Vec3s* int_vector);
Vec3s* Vec3s_Add(Vec3s* dest, Vec3s* a, Vec3s* b);
Vec3s* Vec3s_Subtract(Vec3s* dest, Vec3s* a, Vec3s* subtrahend);
void* MainPool_Alloc(u32 size, s32 side);
void Gfx_SetVertexAttributes(Vtx* vtx, s16 x, s16 y, s16 z, s16 s, s16 t, u8 r, u8 g, u8 b, u8 a);

static s32 path_next(PsJpPath* path) {
    Vec3s out, in;
    if ((path->frames = path->keys->n) == 0) return 0;
    path->frame = 0;
    Vec3s_Add(&out, &path->keys->a, &path->keys->b);
    Vec3s_Subtract(&in, &path->keys[1].a, &path->keys[1].b);
    Vec3f_FromVec3s(&path->p0, &path->keys->a);
    Vec3f_FromVec3s(&path->p1, &out);
    Vec3f_FromVec3s(&path->p2, &in);
    Vec3f_FromVec3s(&path->p3, &path->keys[1].a);
    path->keys++;
    return 1;
}

s32 BezierPath_Step(PsJpPath* path, Vec3f* out) {
    f32 t, u, b0, b1, b2, b3;
    if (path->frames > 0) {
        if ((path->frame >= path->frames) && (path_next(path) == 0)) return 0;
        path->frame++;
        t = (f32)path->frame / (f32)path->frames;
        if (path->flags & 1) t = (-2.0f * t * t * t) + (3.0f * t * t);
        u = 1.0f - t;
        b0 = u * u * u; b1 = 3.0f * t * u * u; b2 = 3.0f * t * t * u; b3 = t * t * t;
        out->x = (path->p3.x * b3) + ((b0 * path->p0.x) + (b1 * path->p1.x) + (b2 * path->p2.x));
        out->y = (path->p3.y * b3) + ((b0 * path->p0.y) + (b1 * path->p1.y) + (b2 * path->p2.y));
        out->z = (path->p3.z * b3) + ((b0 * path->p0.z) + (b1 * path->p1.z) + (b2 * path->p2.z));
    }
    return 1;
}
void BezierPath_InitLinear(PsJpPath* path, PsJpKey* keys) { path->keys = keys; path->flags = 0; path_next(path); }
void BezierPath_InitEased(PsJpPath* path, PsJpKey* keys) { path->keys = keys; path->flags = 1; path_next(path); }

/* ---- La pantalla gigante ----
 * El original dibuja el fuego y las tarjetas en una imagen de 320x240 y la pega como textura en la pantalla de la
 * arena (40 mosaicos). Aqui no hay imagen: la escena del fuego se dibuja directo en la pantalla, dentro del
 * rectangulo que ocupa la pantalla gigante en ese cuadro (la camara siempre la ve de frente). Se evita dibujar a
 * una textura y copiarla, que en el 3DS costaba ~200 ms por cuadro.
 * ImageGrid_Create guarda donde esta la pantalla; el callback de su nodo anota la matriz con la que se dibujaria. */
f32 (*ps_geo_cur_mtx(void))[4]; /* matriz del nodo que se esta dibujando (src/12D80.c) */
/* La pantalla sube a los focos de la torre central del estadio de la US (en unidades del mundo) */
#define PS_GRID_DY 975.0f
#define PS_GRID_DZ -1638.0f
static f32 sGridX, sGridY, sGridZ, sGridW, sGridH;
static f32 sGridMtx[4][4];
static s32 sGridSeen, sGridBoard;

void ps_jp_board_pos(s32* out);
s32 ImageGrid_Create(s16 x, s16 y, s16 z, s32 tex, s16 width, s16 height) {
    (void)tex;
    sGridX = x; sGridY = y + PS_GRID_DY * 10.0f; sGridZ = z + PS_GRID_DZ * 10.0f; sGridW = width; sGridH = height; /* el nodo de la pantalla va a escala 0.1 */
    sGridSeen = 0; sGridBoard = 0;
    return 0;
}
void ImageGrid_Clear(void) { sGridSeen = 0; }
void ps_jp_tracef(const char* fmt, ...);
s32 ImageGrid_GeoCallback(s32 stage, s8* node) {
    (void)node;
    if (stage == 2) {
        s32 i, j;
        f32 (*m)[4] = ps_geo_cur_mtx();
        /* El edificio de la pantalla trae su propio nodo de pantalla (se reconoce porque esta desplazado): si esta, manda el,
           y la pantalla es su hueco (3200x2400 al centro del edificio). */
        s32 board = m[3][1] > 1.0f || m[3][2] < -1.0f; /* el nodo de la arena japonesa no esta desplazado */
        if (sGridBoard && !board) return 0;
        if (board) { sGridBoard = 1; sGridX = 0; sGridY = 0; sGridZ = 0; sGridW = 3200; sGridH = 2400; }
        for (i = 0; i < 4; i++) for (j = 0; j < 4; j++) sGridMtx[i][j] = m[i][j];
        sGridSeen = 1;
    }
    return 0;
}
/* la presentacion va en el hueco del marco dorado (en el mundo, sin matriz de modelo): centro y, fondo z, ancho, alto */
void ps_jp_grid_gold(void) {
    s32 i, j;
    s32 pos[3];
    ps_jp_board_pos(pos);
    for (i = 0; i < 4; i++) for (j = 0; j < 4; j++) sGridMtx[i][j] = (i == j) ? 1.0f : 0.0f;
    sGridX = 0; sGridY = pos[0]; sGridZ = pos[1]; sGridW = 640; sGridH = 480;
    sGridSeen = 1; sGridBoard = 0;
}
/* la matriz del hueco del edificio en el ultimo cuadro (para la pantalla de la batalla); 0 si aun no se dibujo */
s32 ps_jp_board_mtx(f32 (*out)[4]) {
    s32 i, j;
    if (!sGridSeen || !sGridBoard) return 0;
    for (i = 0; i < 4; i++) for (j = 0; j < 4; j++) out[i][j] = sGridMtx[i][j];
    return 1;
}
void ps_jp_tracef(const char* fmt, ...);
f32 sinf(f32);
f32 cosf(f32);
/* Rectangulo (en pixeles de 320x240) que ocupa la pantalla gigante en el cuadro que se acaba de dibujar.
 * fov: campo de vision vertical de la camara de la arena, en grados. Regresa 0 si no se dibujo. */
s32 ps_jp_grid_rect(f32 fov, f32 (*view)[4], s32* rx, s32* ry, s32* rw, s32* rh) {
    f32 c[2][3], t, sx[2], sy[2];
    s32 k;
    if (!sGridSeen) return 0;
    t = fov * 0.5f * 0.017453292f;
    t = sinf(t) / cosf(t);
    for (k = 0; k < 2; k++) {
        f32 px = sGridX + (k ? sGridW : -sGridW) * 0.5f, py = sGridY + (k ? -sGridH : sGridH) * 0.5f, pz = sGridZ;
        /* del modelo al mundo (matriz del nodo) y del mundo a la camara (matriz de vista) */
        f32 wx = px * sGridMtx[0][0] + py * sGridMtx[1][0] + pz * sGridMtx[2][0] + sGridMtx[3][0];
        f32 wy = px * sGridMtx[0][1] + py * sGridMtx[1][1] + pz * sGridMtx[2][1] + sGridMtx[3][1];
        f32 wz = px * sGridMtx[0][2] + py * sGridMtx[1][2] + pz * sGridMtx[2][2] + sGridMtx[3][2];
        c[k][0] = wx * view[0][0] + wy * view[1][0] + wz * view[2][0] + view[3][0];
        c[k][1] = wx * view[0][1] + wy * view[1][1] + wz * view[2][1] + view[3][1];
        c[k][2] = wx * view[0][2] + wy * view[1][2] + wz * view[2][2] + view[3][2];
        if (c[k][2] > -1.0f) return 0; /* detras de la camara */
        sx[k] = 160.0f + (c[k][0] / (-c[k][2] * t * (320.0f / 240.0f))) * 160.0f;
        sy[k] = 120.0f - (c[k][1] / (-c[k][2] * t)) * 120.0f;
    }
    *rx = (s32)(sx[0] + 0.5f); *ry = (s32)(sy[0] + 0.5f);
    *rw = (s32)(sx[1] - sx[0] + 0.5f); *rh = (s32)(sy[1] - sy[0] + 0.5f);
    return (*rw > 1 && *rh > 1);
}
/* guardado, torneo y sonido de la escena: no se usan en la entrada de batalla del port */
s32 ps_jp_stub(void) { return 0; }
f32 sqrtf(f32);
void ps_jp_board_pos(s32* out);
/* La camara sigue a la pantalla: siempre mira hacia ella, y entre mas cerca esta, mas sube con ella. De lejos se queda
   a nivel de cancha, viendola hacia arriba. */
void ps_jp_cam_shift(CameraNode* cam) {
    f32 dx = cam->view.eyePosition.x - cam->view.targetPosition.x, dy = cam->view.eyePosition.y - cam->view.targetPosition.y, dz = cam->view.eyePosition.z - cam->view.targetPosition.z;
    f32 d = sqrtf(dx * dx + dy * dy + dz * dz);
    f32 w = 1.0f - (d - 400.0f) / 1900.0f;
    f32 gy, gz;
    s32 pos[3];
    if (w < 0.0f) w = 0.0f;
    if (w > 1.0f) w = 1.0f;
    ps_jp_board_pos(pos);
    gy = pos[0] - 346.5f; gz = pos[1] + 1127.1f; /* de donde estaba la pantalla en la arena japonesa a donde va ahora */
    cam->view.targetPosition.y += gy; cam->view.targetPosition.z += gz;
    cam->view.eyePosition.y += gy * w; cam->view.eyePosition.z += gz * w;
}

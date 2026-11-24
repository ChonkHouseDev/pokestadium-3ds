#include <time.h>
// ---- Pantalla libre (fuera de batalla): los Pokemon de los equipos de los cartuchos caminan por el pasto ----
// Se incluye desde gb_saves.inc. Cada Pokemon es su mini (o un marcador con su numero) con un aro del color de su
// cartucho. Caminan solos, chocan entre ellos, se pueden agarrar con la pluma y aventar: si salen del campo regresan
// caminando por una orilla. L (o tocar el boton) cambia a la pantalla de los cartuchos y de regreso.
// Dibujo barato: el fondo queda guardado (sLayer, tipo 3) y cada paso solo repone los pedazos donde habia un mini en
// esa pagina y pinta los minis en su lugar nuevo.
#define GR_X0 9
#define GR_Y0 32
#define GR_X1 311
#define GR_Y1 205
#define GR_MAX 12
#define GR_RW 40 // pedazo que ocupa un mini (con su aro y el brinco)
#define GR_RH 46
enum { GR_WALK, GR_HELD, GR_FLY, GR_AWAY };
struct GrMon { u8 dex, slot, st, alt; float x, y, vx, vy, t; bool right; u8 src; u32 ring; float slp, ang; bool zz; };
// De noche (reloj del 3DS) el pasto se oscurece y se van durmiendo (slp: lo que falta para dormirse; zz: dormido, sale una z).
// Si los despiertan (agarrarlos, empujarlos) sale la vena de enojado un rato (ang) y luego se vuelven a dormir.
static int sGrNight; // 0 dia, 1 tarde (6 a 7 pm), 2 noche (7 pm a 6 am)
static float gr_rand(void);
static void gr_wake(struct GrMon& m) { if (m.zz) { m.zz = false; m.ang = 2.5f; } m.slp = 6.0f + gr_rand() * 8.0f; }
// todos los que se pueden sacar (hasta 12, una bola cada uno en la barra); en el pasto andan hasta 6 (sGr)
#define GR_OUT_MAX 6
struct GrPick { u8 dex, slot, alt, out; u32 ring; };
static GrPick sAll[GR_MAX];
static int sAllN;
static char sGrLabel[48]; // abajo del pasto: de donde son (GB 1: Red  GB 2: Blue, Rental, ...)
// los que el usuario guardo en su bola: se recuerdan (tambien al apagar, en cartuchos/guardados.bin) hasta que los saque
#define GR_HIDE_MAX 32
#define GR_HIDE_FILE GB_LBL_DIR "/guardados.bin"
static u32 sGrHide[GR_HIDE_MAX];
static int sGrHideN = -1;
static u32 gr_key(const GrPick& p) { return (u32)p.dex << 24 | (p.ring & 0xFFFFFF); }
static void gr_hide_load(void) {
    if (sGrHideN >= 0) return;
    sGrHideN = 0;
    if (FILE* f = fopen(GR_HIDE_FILE, "rb")) { sGrHideN = (int)(fread(sGrHide, 4, GR_HIDE_MAX, f)); fclose(f); }
}
static bool gr_hidden(u32 k) { gr_hide_load(); for (int i = 0; i < sGrHideN; i++) if (sGrHide[i] == k) return true; return false; }
static void gr_hide_set(u32 k, bool hide) {
    gr_hide_load();
    int i = 0;
    while (i < sGrHideN && sGrHide[i] != k) i++;
    if (hide && i == sGrHideN) { if (sGrHideN == GR_HIDE_MAX) { memmove(sGrHide, sGrHide + 1, 4 * (GR_HIDE_MAX - 1)); sGrHideN--; } sGrHide[sGrHideN++] = k; }
    else if (!hide && i < sGrHideN) { memmove(sGrHide + i, sGrHide + i + 1, 4 * (sGrHideN - i - 1)); sGrHideN--; }
    else return;
    if (FILE* f = fopen(GR_HIDE_FILE, "wb")) { fwrite(sGrHide, 4, sGrHideN, f); fclose(f); }
}
static GrMon sGr[GR_MAX];
static int sGrN, sGrHeld = -1;
static u32 sGrSig = 0xFFFFFFFFu;
static int sGrView;      // 0 los Pokemon libres, 1 los cartuchos
static bool sGrDrawn;    // la pantalla de fuera de batalla ya esta pintada
static u32 sGrStep, sGrSteps, sGrFulls, sGrBumps, sGrThrows, sGrGrabs;
static u64 sGrStepTicks, sGrShownTicks;
static struct { u8* fb; s16 r[GR_MAX][2]; int n; } sGrPage[2];
static u32 sGrRnd = 0x1234ABCDu;
static float gr_rand(void) { sGrRnd = sGrRnd * 1664525u + 1013904223u; return (float)((sGrRnd >> 8) & 0xFFFF) / 65536.0f; }
// indice interno de Rojo/Azul/Amarillo -> numero de Pokedex (0: no existe)
static const u8 kGen1Dex[191] = {
    0, 112, 115, 32, 35, 21, 100, 34, 80, 2, 103, 108, 102, 88, 94, 29, 31, 104, 111, 131, 59, 151, 130, 90, 72, 92, 123, 120, 9, 127, 114, 0,
    0, 58, 95, 22, 16, 79, 64, 75, 113, 67, 122, 106, 107, 24, 47, 54, 96, 76, 0, 126, 0, 125, 82, 109, 0, 56, 86, 50, 128, 0, 0, 0,
    83, 48, 149, 0, 0, 0, 84, 60, 124, 146, 144, 145, 132, 52, 98, 0, 0, 0, 37, 38, 25, 26, 0, 0, 147, 148, 140, 141, 116, 117, 0, 0,
    27, 28, 138, 139, 39, 40, 133, 136, 135, 134, 66, 41, 23, 46, 61, 62, 13, 14, 15, 0, 85, 57, 51, 49, 87, 0, 0, 10, 11, 12, 68, 0,
    55, 97, 42, 150, 143, 129, 0, 0, 89, 0, 99, 91, 0, 101, 36, 110, 53, 105, 0, 93, 63, 65, 17, 18, 121, 1, 3, 73, 0, 118, 119, 0,
    0, 0, 0, 77, 78, 19, 20, 33, 30, 74, 137, 142, 0, 81, 0, 0, 4, 7, 5, 8, 6, 0, 0, 0, 0, 43, 44, 45, 69, 70, 71,
};
// los equipos de los cartuchos conectados; regresa una firma para saber si cambiaron
static u32 gb_roam_scan_gb(GrPick* out, int* n, char* label, size_t lsz) {
    u32 sig = 2166136261u;
    *n = 0;
    label[0] = 0;
    for (int s = 0; s < GB_SLOTS; s++) {
        if (!sGb[s].present || !sGb[s].file || sGb[s].fileSize < GB_RAM_SIZE) continue;
        const size_t ll = strlen(label);
        snprintf(label + ll, lsz - ll, "%sGB %d: %s", ll ? "   " : "", s + 1, sGb[s].e.version >= 0 && sGb[s].e.version < GB_VER_COUNT ? kGbVerNameEn[sGb[s].e.version] : "?");
        const u8* p = sGb[s].file + 0x2F2C;
        const int cnt = p[0];
        if (cnt < 1 || cnt > 6) continue;
        for (int i = 0; i < cnt && *n < GR_MAX; i++) {
            const u8 dex = p[1 + i] <= 190 ? kGen1Dex[p[1 + i]] : 0;
            if (!dex) continue;
            GrPick& m = out[(*n)++];
            m.dex = dex; m.slot = (u8)s;
            m.ring = sGb[s].e.version >= 0 && sGb[s].e.version < GB_VER_COUNT ? kGbVerColor[sGb[s].e.version] : 0xFFFFFF;
            // el Pikachu inicial de Yellow (mismo entrenador que la partida) usa su mini aparte, si lo hay
            const u8* mon = p + 8 + 44 * i;
            m.alt = sGb[s].e.version == GB_VER_YELLOW && dex == 25 && mon[12] == sGb[s].file[0x2605] && mon[13] == sGb[s].file[0x2606];
            sig = (sig ^ (u32)(dex | s << 8)) * 16777619u;
        }
    }
    return sig ^ (u32)*n;
}
// Dentro de un reto (Stadium, Gym Leader Castle, Mewtwo, Free Battle) con el equipo ya inscrito: solo ese equipo. El juego
// lo guarda en D_800AE540 (jugador 0: unk_01C[6], cuantos en unk_002). Cada uno es del cartucho cuyo entrenador tiene su
// numero de ID (unk_0E); si no es de ninguno, es de renta (aro gris).
#define GR_RENTAL_RING 0xC8C8D2u
static bool sChalClear, sBlock;
static void gb_roam_chal_reset(void) { sChalClear = true; sBlock = false; }
static bool gb_roam_scan_chal(GrPick* out, int* n, char* label, size_t lsz, u32* sig) {
    if (!BLOB_AE540 || !BLOB_GAME_STATE) return false;
    const u32 gs = intro_be(BLOB_GAME_STATE);
    static bool sIn;
    if (gs != 0x20 && gs != 0x21 && gs != 0x22 && gs != 0x27) { if (sIn) { sIn = false; gb_roam_chal_reset(); } return false; }
    sIn = true;
    // Al pelear, el juego deja ahi solo los 3 que van a la batalla (y entre batallas puede quedar vacio): el equipo
    // inscrito (hasta 6) se guarda aqui mientras dure el reto, y los que aparecen despues se toman como parte de el
    static u8 sChalDex[6]; static u16 sChalOt[6]; static int sChalN;
    if (sChalClear) { sChalClear = false; sChalN = 0; }
    const u8* q = (const u8*)BLOB_AE540 + 4;
    const int cnt = q[0] ? q[2] : 0;
    // al terminar un reto (copa o gimnasio, ganado o dejado) se suelta el equipo; el que sigue inscrito en el juego no se
    // vuelve a tomar hasta que cambie (otro equipo) o se salga del modo
    static u32 sEndSeen; static bool sEndInit; static u32 sBlockSig;
    u32 qsig = 0x811C9DC5u;
    for (int j = 0; j < 6; j++) { const u8* m = q + 0x1C + j * 0x54; qsig = (qsig ^ (u32)(m[0] | m[0x0E] << 8 | m[0x0F] << 16)) * 16777619u; }
    qsig ^= (u32)cnt;
    if (BLOB_CHAL_END) {
        const u32 e = intro_be(BLOB_CHAL_END);
        if (!sEndInit) { sEndInit = true; sEndSeen = e; }
        if (e != sEndSeen) { sEndSeen = e; sChalN = 0; sBlock = true; sBlockSig = qsig; }
    }
    if (sBlock && qsig != sBlockSig) sBlock = false;
    if (sBlock) return false;
    if (cnt >= 1 && cnt <= 6) {
        u8 dx[6]; u16 ots[6]; int k = 0;
        for (int j = 0; j < cnt; j++) { const u8* m = q + 0x1C + j * 0x54; if (m[0] >= 1 && m[0] <= 151) { dx[k] = m[0]; ots[k] = (u16)(m[0x0E] << 8 | m[0x0F]); k++; } }
        bool subset = sChalN > 0;
        for (int j = 0; j < k && subset; j++) { bool f = false; for (int c = 0; c < sChalN && !f; c++) f = sChalDex[c] == dx[j] && sChalOt[c] == ots[j]; subset = f; }
        if (!subset && k > 0) { sChalN = k; memcpy(sChalDex, dx, k); memcpy(sChalOt, ots, k * 2); }
    }
    if (sChalN == 0) return false;
    *n = 0;
    *sig = 0x811C9DC5u ^ 0x5A5A;
    bool from[GB_SLOTS + 1] = {};
    for (int j = 0; j < sChalN; j++) {
        const u8 dex = sChalDex[j];
        const u16 ot = sChalOt[j];
        int slot = -1;
        for (int s = 0; s < GB_SLOTS && slot < 0; s++)
            if (sGb[s].present && sGb[s].file && sGb[s].fileSize >= GB_RAM_SIZE && ot == (u16)(sGb[s].file[0x2605] << 8 | sGb[s].file[0x2606])) slot = s;
        GrPick& p = out[(*n)++];
        p.dex = dex; p.slot = (u8)(slot < 0 ? 0 : slot); p.alt = 0;
        p.ring = slot < 0 ? GR_RENTAL_RING : sGb[slot].e.version >= 0 && sGb[slot].e.version < GB_VER_COUNT ? kGbVerColor[sGb[slot].e.version] : 0xFFFFFF;
        from[slot < 0 ? GB_SLOTS : slot] = true;
        *sig = (*sig ^ (u32)(dex | (slot + 1) << 8 | j << 12)) * 16777619u;
    }
    if (!*n) return false;
    label[0] = 0;
    for (int s = 0; s <= GB_SLOTS; s++) {
        if (!from[s]) continue;
        const size_t ll = strlen(label);
        snprintf(label + ll, lsz - ll, "%s%s", ll ? " + " : "", s == GB_SLOTS ? "Rental" : sGb[s].e.version >= 0 && sGb[s].e.version < GB_VER_COUNT ? kGbVerNameEn[sGb[s].e.version] : "?");
    }
    return true;
}
static u32 gb_roam_scan(GrPick* out, int* n, char* label, size_t lsz) {
    u32 sig = 0;
    if (gb_roam_scan_chal(out, n, label, lsz, &sig)) return sig;
    return gb_roam_scan_gb(out, n, label, lsz);
}
// los que andan en el pasto: los sAll con su bola afuera. Los que ya andaban se quedan donde estaban
static void gb_roam_place(void);
static void gb_roam_active(bool keep) {
    GrMon old[GR_MAX];
    const int oldN = keep ? sGrN : 0;
    memcpy(old, sGr, sizeof old);
    sGrN = 0;
    for (int i = 0; i < sAllN && sGrN < GR_OUT_MAX; i++) {
        if (!sAll[i].out) continue;
        GrMon& m = sGr[sGrN];
        int k = 0;
        while (k < oldN && old[k].src != i) k++;
        if (k < oldN) m = old[k];
        else {
            m = GrMon{};
            m.st = GR_AWAY; m.t = 0.0f; // entra caminando por una orilla
            m.slp = 4.0f + gr_rand() * 8.0f;
        }
        m.dex = sAll[i].dex; m.slot = sAll[i].slot; m.alt = sAll[i].alt; m.ring = sAll[i].ring; m.src = (u8)i;
        sGrN++;
    }
    if (sGrHeld >= 0) { sGrHeld = -1; }
    for (int i = 0; i < sGrN; i++) if (sGr[i].st == GR_HELD) sGr[i].st = GR_WALK;
    if (!keep) gb_roam_place();
}
// la barra: una bola por Pokemon, agrupadas por cartucho, pegadas a la derecha
static int gb_roam_ball_x(int i, int* sz) {
    const int n = sAllN, s = n <= 7 ? 15 : 11, step = n <= 7 ? 21 : 15, gap = 4;
    int groups = 0;
    for (int k = 1; k < n; k++) if (sAll[k].ring != sAll[k - 1].ring) groups++;
    int x = 313 - n * step - groups * gap;
    for (int k = 1; k <= i; k++) x += step + (sAll[k].ring != sAll[k - 1].ring ? gap : 0);
    *sz = s;
    return x + step / 2; // centro
}
static void gb_roam_ball(int cx, int cy, int sz, u32 ring, bool lit) {
    const float r = sz * 0.5f;
    gb_disc(cx, cy, r + 2.5f, lit ? ring : gb_shade(ring, 55));
    gb_disc(cx, cy, r + 0.8f, 0x070D42);
    const int x0 = cx - sz / 2, y0 = cy - sz / 2;
    if (const u8* ic = gb_icon_get()) {
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
            const int sx0 = x * 32 / sz, sx1 = (x + 1) * 32 / sz, sy0 = y * 32 / sz, sy1 = (y + 1) * 32 / sz;
            u32 acc[4] = {}, cnt = 0;
            for (int j = sy0; j < sy1; j++) for (int i = sx0; i < sx1; i++, cnt++) {
                const u8* q = ic + (j * 32 + i) * 4;
                acc[0] += q[0] * q[3]; acc[1] += q[1] * q[3]; acc[2] += q[2] * q[3]; acc[3] += q[3];
            }
            if (!cnt || !acc[3]) continue;
            u32 R = acc[0] / acc[3], G = acc[1] / acc[3], B = acc[2] / acc[3];
            int a = (int)(acc[3] / cnt);
            if (!lit) { const u32 g = (R * 3 + G * 6 + B) / 10 * 55 / 100 + 30; R = G = B = g; a = a * 60 / 100; }
            gb_blend(x0 + x, y0 + y, R << 16 | G << 8 | B, a);
        }
        return;
    }
    // sin iconball.png: una bola dibujada (arriba roja, abajo blanca, linea y boton)
    for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) {
        const float dx = x + 0.5f - r, dy = y + 0.5f - r, d = dx * dx + dy * dy;
        if (d > r * r) continue;
        u32 c = dy < -0.8f ? 0xE03028 : dy > 0.8f ? 0xF0F0F0 : 0x202020;
        if (dx * dx + dy * dy < (r * 0.32f) * (r * 0.32f)) c = 0xF0F0F0;
        if (!lit) c = gb_shade(c == 0xE03028 ? 0x808080 : c, 55);
        gb_blend(x0 + x, y0 + y, c, lit ? 255 : 160);
    }
}
static void gb_roam_balls(void) {
    for (int i = 0; i < sAllN; i++) { int sz; const int cx = gb_roam_ball_x(i, &sz); gb_roam_ball(cx, 13, sz, sAll[i].ring, sAll[i].out); }
}
// tocar una bola: sale o se guarda (en el pasto caben 6)
static bool gb_roam_ball_tap(int px, int py) {
    if (py >= 27) return false;
    for (int i = 0; i < sAllN; i++) {
        int sz; const int cx = gb_roam_ball_x(i, &sz), half = sz <= 11 ? 7 : 10;
        if (px < cx - half || px > cx + half) continue;
        if (sAll[i].out) { sAll[i].out = 0; gr_hide_set(gr_key(sAll[i]), true); }
        else {
            int outN = 0;
            for (int k = 0; k < sAllN; k++) outN += sAll[k].out;
            if (outN >= GR_OUT_MAX) return false;
            sAll[i].out = 1;
            gr_hide_set(gr_key(sAll[i]), false);
        }
        gb_roam_active(true);
        return true;
    }
    return false;
}
static void gb_roam_place(void) {
    for (int i = 0; i < sGrN; i++) {
        GrMon& m = sGr[i];
        m.st = GR_WALK; m.vx = m.vy = 0; m.t = gr_rand() * 1.5f; m.right = gr_rand() < 0.5f;
        m.zz = false; m.ang = 0.0f; m.slp = 2.0f + gr_rand() * 12.0f;
        // repartidos en una cuadricula de 4 x 3 con algo de azar
        m.x = GR_X0 + 40 + (i % 4) * 74 + (gr_rand() - 0.5f) * 30.0f;
        m.y = GR_Y0 + 34 + (i / 4) * 52 + (gr_rand() - 0.5f) * 20.0f;
    }
    sGrHeld = -1;
}
static inline void gr_put(int x, int y, u32 rgb, int a) { if (x > GR_X0 && x < GR_X1 - 1 && y > GR_Y0 && y < GR_Y1 - 1) gb_blend(x, y, rgb, a); }
static void gb_roam_sprite_body(const GrMon& m) {
    const int cx = (int)(m.x + 0.5f), cy = (int)(m.y + 0.5f);
    const bool moving = m.st == GR_WALK && (m.vx != 0.0f || m.vy != 0.0f);
    const int lift = m.st == GR_HELD ? 6 : m.st == GR_FLY ? 4 : (moving && ((sGrStep + (u32)(&m - sGr)) & 2)) ? 2 : 0;
    // aro en el piso, del color del cartucho
    const u32 ring = m.ring;
    for (int y = -5; y <= 5; y++) for (int x = -15; x <= 15; x++) {
        const float d = (float)(x * x) / 225.0f + (float)(y * y) / 25.0f;
        if (d > 1.0f) continue;
        gr_put(cx + x, cy + 13 + y, d > 0.62f ? ring : 0x163C22, d > 0.62f ? 255 : 150);
    }
    const u8* px = m.alt ? gb_mini_get(GB_MINI_ALT) : nullptr;
    if (!px) px = gb_mini_get(m.dex);
    const int x0 = cx - 16, y0 = cy - 18 - lift;
    if (px) {
        static const u8 kSpin[4] = { 0, 1, 3, 2 };
        const int rot = m.st == GR_FLY ? kSpin[(sGrStep >> 1) & 3] : 0;
        if (rot == 0 && x0 > GR_X0 && x0 + 32 < GR_X1 - 1 && y0 > GR_Y0 && y0 + 32 < GR_Y1 - 1) { // entero dentro del campo: directo a la pagina
            for (int x = 0; x < 32; x++) {
                u8* col = sGbFb + ((x0 + (m.right ? 31 - x : x)) * 240 + (239 - y0)) * 3;
                const u8* q = px + x * 4;
                for (int y = 0; y < 32; y++, q += 128, col -= 3) {
                    const int a = q[3];
                    if (a < 8) continue;
                    if (a > 247) { col[0] = q[2]; col[1] = q[1]; col[2] = q[0]; continue; }
                    col[0] = (u8)(col[0] + (q[2] - col[0]) * a / 255); col[1] = (u8)(col[1] + (q[1] - col[1]) * a / 255); col[2] = (u8)(col[2] + (q[0] - col[2]) * a / 255);
                }
            }
            return;
        }
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++, px += 4) {
            if (px[3] < 8) continue;
            const int mx = m.right ? 31 - x : x; // los minis miran a la izquierda
            gr_put(x0 + (rot == 1 ? 31 - y : rot == 2 ? y : rot == 3 ? 31 - mx : mx), y0 + (rot == 1 ? mx : rot == 2 ? 31 - mx : rot == 3 ? 31 - y : y),
                   (u32)(px[0] << 16 | px[1] << 8 | px[2]), px[3]);
        }
        return;
    }
    // sin minis: un marcador con su numero
    for (int y = 3; y < 29; y++) for (int x = 3; x < 29; x++) {
        const bool edge = x == 3 || x == 28 || y == 3 || y == 28;
        if ((x < 5 || x > 26) && (y < 5 || y > 26)) continue;
        gr_put(x0 + x, y0 + y, edge ? 0x141840 : m.st == GR_HELD ? 0xF8C828 : 0xC8B878, 255);
    }
    char t[8];
    snprintf(t, sizeof t, "#%d", m.dex);
    const int tx = x0 + 16 - gb_tw(t) / 2, ty = y0 + 9;
    if (tx > GR_X0 + 1 && tx + gb_tw(t) < GR_X1 - 2 && ty > GR_Y0 + 1 && ty + 14 < GR_Y1 - 1) gb_text(tx, ty, t, 0x1E1E32);
}
// la z de dormido (5x5) y la vena de enojado (dos esquinas de cada lado, en rojo)
static void gr_zee(int x, int y, int k, int a) {
    for (int i = 0; i < k; i++) { gr_put(x + i, y, 0xE8F0FF, a); gr_put(x + i, y + k - 1, 0xE8F0FF, a); gr_put(x + k - 1 - i, y + i, 0xE8F0FF, a); }
}
// la vena de enojo de los dibujos japoneses: cuatro trazos doblados con la punta hacia el centro
static void gr_vein(int x, int y) {
    static const char* const kV[16] = { ".....##..##.....", ".....##..##.....", ".....##..##.....", "....###..###....",
                                        "..####....####..", "#####......#####", "####........####", "................",
                                        "................", "####........####", "#####......#####", "..####....####..",
                                        "....###..###....", ".....##..##.....", ".....##..##.....", ".....##..##....." };
    auto on = [](int i, int j) { return i >= 0 && i < 16 && j >= 0 && j < 16 && kV[j][i] == '#'; };
    for (int j = -1; j <= 16; j++) for (int i = -1; i <= 16; i++) {
        if (on(i, j)) { gr_put(x + i, y + j, 0xF02828, 255); continue; }
        if (on(i - 1, j) || on(i + 1, j) || on(i, j - 1) || on(i, j + 1)) gr_put(x + i, y + j, 0x3C0606, 170); // orilla oscura: se lee sobre el pasto y el mini
    }
}
static void gb_roam_sprite(const GrMon& m) {
    gb_roam_sprite_body(m);
    const int cx = (int)(m.x + 0.5f), cy = (int)(m.y + 0.5f);
    if (m.zz && m.st == GR_WALK) { // dos z que suben
        const int ph = (int)((sGrStep / 3 + (u32)(&m - sGr) * 2) % 6);
        gr_zee(cx + 7, cy - 18 - ph / 2, 4, 230);
        if (ph >= 2) gr_zee(cx + 12, cy - 25 + (ph - 2) / 2, 5, 255);
    } else if (m.ang > 0.0f && m.st != GR_AWAY) {
        gr_vein(cx + 3, cy - 25);
    }
}
static void gb_roam_restore(int rx, int ry) {
    int x0 = rx, x1 = rx + GR_RW, y0 = ry, y1 = ry + GR_RH;
    if (x0 < GR_X0) x0 = GR_X0;
    if (x1 > GR_X1) x1 = GR_X1;
    if (y0 < GR_Y0) y0 = GR_Y0;
    if (y1 > GR_Y1) y1 = GR_Y1;
    if (x0 >= x1 || y0 >= y1) return;
    for (int x = x0; x < x1; x++) { const int o = (x * 240 + (239 - (y1 - 1))) * 3; memcpy(sGbFb + o, sLayer + o, (size_t)(y1 - y0) * 3); }
}
// pinta la pantalla completa (sin minis) y la guarda; las dos paginas quedan iguales
static void gb_roam_full(void) {
    if (!gb_screen_begin()) return;
    if (!gb_layer_get(3)) {
        gb_bg("");
        gb_rect_title("YOUR POKEMON");
        gb_roam_balls();
        gb_panel(GR_X0 - 1, GR_Y0 - 1, GR_X1 - GR_X0 + 2, GR_Y1 - GR_Y0 + 2, 0x2E7846, 0x78C882, 1, 255, 8);
        u32 h = 0x9E3779B9u;
        for (int i = 0; i < 70; i++) { // matas de pasto, regadas
            h = h * 1664525u + 1013904223u;
            const int x = GR_X0 + 8 + (int)((h >> 8) % (u32)(GR_X1 - GR_X0 - 18)), y = GR_Y0 + 10 + (int)((h >> 20) % (u32)(GR_Y1 - GR_Y0 - 30));
            const u32 c = (h & 1) ? 0x4FA364 : 0x24693A;
            for (int k = 0; k < 5; k++) { gb_blend(x - k / 2, y - k, c, 255); gb_blend(x + 3 + k / 2, y - k, c, 255); if (k < 4) gb_blend(x + 1, y - k - 1, c, 255); }
        }
        if (sGrNight) { // tarde: naranja; noche: azul oscuro
            const u32 c = sGrNight == 2 ? 0x060C34 : 0xA84A1C; const int a = sGrNight == 2 ? 140 : 100; // tarde: naranja, como el mapa
            for (int y = GR_Y0; y < GR_Y1; y++) for (int x = GR_X0; x < GR_X1; x++) gb_blend(x, y, c, a);
        }
        char t[80];
        int outN = 0;
        for (int k = 0; k < sAllN; k++) outN += sAll[k].out;
        snprintf(t, sizeof t, "%s      %d/%d out", sGrLabel, outN, sAllN);
        gb_text(160 - gb_tw(t) / 2, GR_Y1 - 17, t, 0xB4E6BE);
        gb_panel(8, 210, 198, 24, 0x1C2C90, 0x6088F0, 1, 235);
        gb_label(107, 215, "Flick them away - they come back", 0xDCE4FF, kUiFontSm, true);
        gb_button(212, 210, 100, 24, "R", 0xC89628, "Game Paks", 0x1C2C90, 0x6088F0);
        gb_layer_put(3);
    }
    gb_screen_end();
    sGrPage[0].n = sGrPage[1].n = 0;
    sGrPage[0].fb = sGrPage[1].fb = nullptr;
    sGrFulls++;
}
static void gb_roam_draw(void) {
    if (!sLayer || sLayerKind != 3 || !gb_screen_begin()) return;
    auto* pg = sGrPage[0].fb == sGbFb ? &sGrPage[0] : sGrPage[1].fb == sGbFb ? &sGrPage[1] : !sGrPage[0].fb ? &sGrPage[0] : &sGrPage[1];
    if (pg->fb != sGbFb) { pg->fb = sGbFb; pg->n = 0; }
    for (int i = 0; i < pg->n; i++) gb_roam_restore(pg->r[i][0], pg->r[i][1]);
    pg->n = 0;
    u8 ord[GR_MAX]; // los de mas abajo se pintan al final (quedan delante); el agarrado, encima de todos
    for (int i = 0; i < sGrN; i++) ord[i] = (u8)i;
    for (int i = 1; i < sGrN; i++) for (int j = i; j > 0; j--) {
        const GrMon &a = sGr[ord[j - 1]], &b = sGr[ord[j]];
        if ((a.st == GR_HELD) > (b.st == GR_HELD) || ((a.st == GR_HELD) == (b.st == GR_HELD) && a.y > b.y)) { const u8 t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; } else break;
    }
    for (int k = 0; k < sGrN; k++) {
        const GrMon& m = sGr[ord[k]];
        if (m.st == GR_AWAY) continue;
        gb_roam_sprite(m);
        pg->r[pg->n][0] = (s16)((int)(m.x + 0.5f) - GR_RW / 2); pg->r[pg->n][1] = (s16)((int)(m.y + 0.5f) - 26); pg->n++;
    }
    GSPGPU_FlushDataCache(sGbFb, 240 * 320 * 3);
    gfxScreenSwapBuffers(GFX_BOTTOM, false);
}
static void gb_roam_physics(float dt) {
    const float minx = GR_X0 + 18, maxx = GR_X1 - 18, miny = GR_Y0 + 22, maxy = GR_Y1 - 22;
    for (int i = 0; i < sGrN; i++) {
        GrMon& m = sGr[i];
        if (m.ang > 0.0f) m.ang -= dt;
        if (sGrNight < 2) m.zz = false;
        if (m.st == GR_WALK && sGrNight == 2 && !m.zz && m.ang <= 0.0f) { m.slp -= dt; if (m.slp <= 0.0f) { m.zz = true; m.vx = m.vy = 0.0f; } }
        if (m.st == GR_WALK && m.zz) { m.vx = m.vy = 0.0f; continue; }
        if (m.st == GR_WALK) {
            m.t -= dt;
            if (m.t <= 0.0f) {
                if (gr_rand() < 0.3f) { m.vx = m.vy = 0.0f; m.t = 0.8f + gr_rand() * 1.4f; }
                else { const float a = gr_rand() * 6.2832f, sp = 9.0f + gr_rand() * 10.0f; m.vx = cosf(a) * sp; m.vy = sinf(a) * sp * 0.7f; m.t = 1.5f + gr_rand() * 2.5f; }
            }
            m.x += m.vx * dt; m.y += m.vy * dt;
            // en la orilla se da la vuelta (sin brincar de lugar: asi el que regresa entra caminando)
            if (m.x < minx && m.vx <= 0.0f) m.vx = m.vx < 0.0f ? -m.vx : 12.0f;
            if (m.x > maxx && m.vx >= 0.0f) m.vx = m.vx > 0.0f ? -m.vx : -12.0f;
            if (m.y < miny && m.vy <= 0.0f) m.vy = m.vy < 0.0f ? -m.vy : 10.0f;
            if (m.y > maxy && m.vy >= 0.0f) m.vy = m.vy > 0.0f ? -m.vy : -10.0f;
        } else if (m.st == GR_FLY) {
            m.x += m.vx * dt; m.y += m.vy * dt;
            const float k = 1.0f - 0.9f * dt;
            m.vx *= k; m.vy *= k;
            if (m.x < GR_X0 - 20 || m.x > GR_X1 + 20 || m.y < GR_Y0 - 24 || m.y > GR_Y1 + 24) { m.st = GR_AWAY; m.t = 1.5f + gr_rand() * 2.0f; }
            else if (m.vx * m.vx + m.vy * m.vy < 30.0f * 30.0f) { m.st = GR_WALK; m.t = 0.0f; }
        } else if (m.st == GR_AWAY) {
            m.t -= dt;
            if (m.t <= 0.0f) { // regresa caminando por una orilla
                const int e = (int)(gr_rand() * 4.0f) & 3;
                const float sp = 16.0f;
                if (e == 0) { m.x = GR_X0 - 18; m.y = miny + gr_rand() * (maxy - miny); m.vx = sp; m.vy = 0; }
                else if (e == 1) { m.x = GR_X1 + 18; m.y = miny + gr_rand() * (maxy - miny); m.vx = -sp; m.vy = 0; }
                else if (e == 2) { m.y = GR_Y0 - 20; m.x = minx + gr_rand() * (maxx - minx); m.vy = sp; m.vx = 0; }
                else { m.y = GR_Y1 + 22; m.x = minx + gr_rand() * (maxx - minx); m.vy = -sp; m.vx = 0; }
                m.st = GR_WALK; m.t = 3.0f;
            }
        }
        if (m.vx > 1.0f) m.right = true; else if (m.vx < -1.0f) m.right = false;
    }
    // choques: dos que se enciman se separan y cambian la parte de su velocidad que los acercaba
    const float R = 25.0f;
    for (int i = 0; i < sGrN; i++) for (int j = i + 1; j < sGrN; j++) {
        GrMon &a = sGr[i], &b = sGr[j];
        if (a.st == GR_AWAY || b.st == GR_AWAY) continue;
        const float dx = b.x - a.x, dy = (b.y - a.y) * 1.3f; // el campo se ve de lado: en vertical caben mas juntos
        const float d2 = dx * dx + dy * dy;
        if (d2 >= R * R || d2 < 0.01f) continue;
        const float d = sqrtf(d2), nx = dx / d, ny = dy / d, over = R - d;
        const bool ha = a.st == GR_HELD, hb = b.st == GR_HELD;
        if (ha && hb) continue;
        const float fa = ha ? 0.0f : hb ? 1.0f : 0.5f, fb = 1.0f - fa;
        a.x -= nx * over * fa; a.y -= ny * over * fa / 1.3f;
        b.x += nx * over * fb; b.y += ny * over * fb / 1.3f;
        if (ha || hb) { // el agarrado empuja al otro
            GrMon& o = ha ? b : a;
            gr_wake(o);
            const float s = ha ? 1.0f : -1.0f;
            o.vx = nx * 26.0f * s; o.vy = ny * 26.0f * s; o.t = 0.8f;
            sGrBumps++;
            continue;
        }
        const float vr = (a.vx - b.vx) * nx + (a.vy - b.vy) * ny;
        if (vr <= 0.0f) continue; // ya se alejan
        if (vr > 40.0f) { gr_wake(a); gr_wake(b); } // un golpe fuerte (uno aventado) despierta
        a.vx -= vr * nx; a.vy -= vr * ny;
        b.vx += vr * nx; b.vy += vr * ny;
        sGrBumps++;
        for (GrMon* m : { &a, &b }) { // un golpe fuerte manda volando al que estaba caminando
            const float sp2 = m->vx * m->vx + m->vy * m->vy;
            if (m->st == GR_WALK) { if (sp2 > 45.0f * 45.0f) m->st = GR_FLY; else { m->t = 1.0f; if (sp2 < 16.0f) { m->vx = (m == &a ? -nx : nx) * 10.0f; m->vy = (m == &a ? -ny : ny) * 8.0f; } } }
        }
    }
}
// la pluma: agarrar al que esta debajo, llevarlo, y al soltar sale con la velocidad que traia
static struct { float x, y; u64 t; } sGrHist[8];
static int sGrHistN;
static void gb_roam_touch(bool down, int px, int py, u64 now) {
    static bool sWas;
    if (down && !sWas) {
        int best = -1; float bd = 22.0f * 22.0f;
        for (int i = 0; i < sGrN; i++) {
            if (sGr[i].st == GR_AWAY) continue;
            const float dx = sGr[i].x - px, dy = sGr[i].y - 4 - py, d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = i; }
        }
        if (best >= 0) { sGrHeld = best; gr_wake(sGr[best]); sGr[best].st = GR_HELD; sGrHistN = 0; sGrGrabs++; }
    }
    if (sGrHeld >= 0) {
        GrMon& m = sGr[sGrHeld];
        if (down) {
            float x = (float)px, y = (float)py + 4.0f;
            if (x < GR_X0 + 14) x = GR_X0 + 14;
            if (x > GR_X1 - 14) x = GR_X1 - 14;
            if (y < GR_Y0 + 20) y = GR_Y0 + 20;
            if (y > GR_Y1 - 16) y = GR_Y1 - 16;
            if (x > m.x + 1.0f) m.right = true; else if (x < m.x - 1.0f) m.right = false;
            m.x = x; m.y = y;
            if (sGrHistN == 0 || now - sGrHist[sGrHistN - 1].t > 268111856ull / 60) { // una muestra cada ~16 ms: las 8 cubren mas de 0.1 s
                if (sGrHistN == 8) { memmove(&sGrHist[0], &sGrHist[1], sizeof sGrHist[0] * 7); sGrHistN = 7; }
                sGrHist[sGrHistN].x = x; sGrHist[sGrHistN].y = y; sGrHist[sGrHistN].t = now; sGrHistN++;
            }
        } else {
            // velocidad de los ultimos ~150 ms
            float vx = 0, vy = 0;
            for (int k = 0; k < sGrHistN; k++) {
                const float dt = (float)(now - sGrHist[k].t) / 268111856.0f;
                if (dt > 0.02f && dt < 0.16f) { vx = (m.x - sGrHist[k].x) / dt; vy = (m.y - sGrHist[k].y) / dt; break; }
            }
            const float sp = sqrtf(vx * vx + vy * vy);
            if (sp > 70.0f) {
                const float k = sp > 700.0f ? 700.0f / sp : 1.0f;
                m.vx = vx * k; m.vy = vy * k; m.st = GR_FLY; sGrThrows++;
            } else { m.vx = m.vy = 0.0f; m.st = GR_WALK; m.t = 0.6f; }
            sGrHeld = -1;
        }
    }
    sWas = down;
}
#include "gb_prof.inc"
static void gb_roam_reset(void) { sGrDrawn = false; if (sGrHeld >= 0) { sGr[sGrHeld].st = GR_WALK; sGrHeld = -1; } }
// cada vuelta del host mientras no hay batalla
static void gb_roam_tick(u64 now) {
    static u64 sNextStep, sLastStep, sNextScan, sShownAt;
    static bool sLWas, sTapWas;
    const u32 held = hidKeysHeld();
    bool down = (held & KEY_TOUCH) != 0;
    int px = 0, py = 0;
    if (down) { touchPosition tp; hidTouchRead(&tp); px = tp.px; py = tp.py; }
    {   // (solo para probar en el emulador, que no tiene pantalla tactil) libreprueba.txt = 1: cada 4 s agarra a uno y lo
        // avienta, una vez despacio y otra fuerte; = 2: ademas cambia de pantalla cada 10 s
        static int sTest = -1, sWho;
        static u64 sT0;
        static float sX0, sY0;
        if (sTest < 0) { const u32 v = host_flag("libreprueba"); sTest = v == 1 || v == 2 ? (int)v : 0; }
        if (sTest && sGrN > 0 && sGrView == 0) {
            const u64 sec = 268111856ull, ph = now % (4 * sec);
            if (ph < sec / 2) {
                if (sT0 == 0 || now - sT0 > 2 * sec) { sT0 = now; sWho = (sWho + 1) % sGrN; sX0 = sGr[sWho].x; sY0 = sGr[sWho].y - 4; }
                const float f = (float)(now - sT0) / (float)(sec / 2), far = (sWho & 1) ? 130.0f : 14.0f;
                down = sGr[sWho].st != GR_AWAY; px = (int)(sX0 + f * far); py = (int)(sY0 - f * far * 0.35f);
            }
        }
        if (sTest == 2 && (now / (10 * 268111856ull)) % 2 == 1 && sGrView == 0) sGrView = 1, sGrDrawn = false;
        if (sTest == 2 && (now / (10 * 268111856ull)) % 2 == 0 && sGrView == 1) sGrView = 0, sGrDrawn = false;
    }
    // los equipos (pueden cambiar en el laboratorio): se revisan cada segundo
    if (now >= sNextScan) {
        sNextScan = now + 268111856ull;
        {   // la hora del juego (la del 3DS o la de Options, ver ps_hour_now)
            const int h = ps_hour_now();
            const int ph = h >= 19 || h < 6 ? 2 : h == 18 ? 1 : 0; // igual que el mapa de arriba (fragment37.c)
            if (ph != sGrNight) { sGrNight = ph; if (sGrView == 0) { sGrDrawn = false; sLayerKind = 0; } }
        }
        GrPick tmp[GR_MAX]; int n = 0;
        char label[48];
        const u32 sig = gb_roam_scan(tmp, &n, label, sizeof label);
        if (sig != sGrSig) {
            sGrSig = sig; sAllN = n;
            memcpy(sAll, tmp, sizeof tmp);
            snprintf(sGrLabel, sizeof sGrLabel, "%s", label);
            // salen los que el usuario no guardo, hasta 6
            int outN = 0;
            for (int i = 0; i < n; i++) { sAll[i].out = outN < GR_OUT_MAX && !gr_hidden(gr_key(sAll[i])); outN += sAll[i].out; }
            gb_roam_active(false);
            u8 recs[14]; int k = 0;
            for (int i = 0; i < n && k < 13; i++) recs[k++] = sAll[i].dex;
            for (int i = 0; i < n && k < 14; i++) if (sAll[i].alt) { recs[k++] = GB_MINI_ALT; break; }
            gb_mini_prime(recs, k);
            if (sGrView == 0) { sGrDrawn = false; sLayerKind = 0; }
        }
    }
    // L o tocar el boton: cambiar entre los Pokemon y los cartuchos
    // Al arrancar (logos y titulo) y en los minijuegos solo se ve la de los cartuchos, fija; al pasar del titulo
    // (Start) cambia sola a la de los Pokemon.
    static bool sTitleWas = true, sLockWas;
    const u32 gs = BLOB_GAME_STATE ? intro_be(BLOB_GAME_STATE) : 4;
    const bool title = gs <= 2 || gs == 0x40, lock = title || gs == 0x23 || gs == 0x82;
    if (sTitleWas && !title) { sGrView = 0; gb_roam_reset(); }
    sTitleWas = title;
    if (lock != sLockWas) { sLockWas = lock; gb_roam_reset(); }
    pf_init();
    const bool lNow = (held & (KEY_L | KEY_R)) != 0;
    const bool tapEdge = down && !sTapWas;
    bool toggle = false;
    // L / R: pantalla anterior / siguiente (Pokemon, cartuchos, perfil)
    // Profile solo en el camino principal (logo, titulo, menu y mapa); ya dentro de cualquier modo (Stadium, gimnasio,
    // Free Battle, Battle Now, Mewtwo, eventos, etc.) no sale hasta volver
    const bool fight = !(gs <= 4 || gs == 0x10 || gs == 0x40);
    if (fight != sGbRoamFight) { sGbRoamFight = fight; sLayerKind = 0; gb_roam_reset(); } // el letrero de L / R cambia
    if (fight && sGrView == 2) { sGrView = sAllN > 0 ? 0 : 1; sPfKb = false; gb_roam_reset(); }
    if (lNow && !sLWas && !lock) {
        if (fight) sGrView = sAllN > 0 ? (sGrView ^ 1) : 1; // solo los Pokemon y los cartuchos
        else {
            const int d = (held & KEY_R) ? 1 : 2;
            sGrView = (sGrView + d) % 3;
            if (sGrView == 0 && sAllN == 0) sGrView = (sGrView + d) % 3;
        }
        gb_roam_reset();
    }
    {   // (solo para probar en el emulador, que no tiene pantalla tactil) perfilprueba.txt = 1: abre el perfil y elige
        static int sT = -1, sStep;
        static u64 sAt;
        if (sT < 0) sT = host_flag("perfilprueba") == 1;
        if (sT && !lock && now - sAt > 268111856ull * 2) {
            sAt = now;
            if (sGrView != 2 && !fight) { sGrView = 2; gb_roam_reset(); }
            else { static const short kT[][2] = { {80, 100}, {120, 220}, {0, 0}, {172 + 2 * 34 + 10, 54 + 35 + 10}, {238, 94}, {238, 200} }; const int k = sStep++; if (k < 6 && kT[k][0] && pf_tap(kT[k][0], kT[k][1])) gb_roam_reset(); }
        }
    }
    if (!lock && !fight && sGrView == 2 && tapEdge && pf_tap(px, py)) gb_roam_reset();
    if (!lock && tapEdge && sGrView == 0 && px >= 212 && py >= 210) toggle = true;
    if (!lock && tapEdge && sGrView == 1 && py >= GB_FMT_Y) toggle = true;
    sLWas = lNow; sTapWas = down;
    if (!lock && tapEdge && sGrView == 0 && gb_roam_ball_tap(px, py)) { sLayerKind = 0; gb_roam_reset(); }
    {   // (solo para probar en el emulador) bolaprueba.txt = 1: cada 3 s toca la bola siguiente
        static int sT = -1, sK;
        static u64 sAt;
        if (sT < 0) sT = host_flag("bolaprueba") == 1;
        if (sT && !lock && sGrView == 0 && sAllN > 0 && now - sAt > 268111856ull * 3) {
            sAt = now;
            int sz; const int cx = gb_roam_ball_x(sK % sAllN, &sz);
            sK++;
            if (gb_roam_ball_tap(cx, 13)) { sLayerKind = 0; gb_roam_reset(); }
        }
    }
    if (toggle && sAllN > 0) { sGrView ^= 1; gb_roam_reset(); }
    const int view = lock || (sGrView == 0 && sAllN == 0) ? 1 : sGrView;
    sGbRoamHint = !lock;
    sGbNoWait = true;
    if (!sGrDrawn) {
        if (view == 2) pf_draw();
        else if (view == 0) { gb_roam_full(); if (!sLayer || sLayerKind != 3) { sGrView = 1; gb_draw_main(sGbUiList, sGbUiPick, -1, sGbUiAspect, -1, true); } }
        else gb_draw_main(sGbUiList, sGbUiPick, -1, sGbUiAspect, -1, true);
        sGrDrawn = true; sLastStep = now; sNextStep = 0; sShownAt = now;
    }
    if (view == 0 && sGrView == 0 && sLayer && sLayerKind == 3) {
        sGrShownTicks += now - sShownAt;
        gb_roam_touch(down && py >= 28 && py < 208, px, py, now);
        // 10 pasos por segundo; 30 mientras hay uno agarrado o volando (se nota en la mano)
        bool fast = sGrHeld >= 0;
        for (int i = 0; i < sGrN && !fast; i++) fast = sGr[i].st == GR_FLY;
        if (now >= sNextStep) {
            sNextStep = now + 268111856ull / (fast ? 30 : 10);
            float dt = (float)(now - sLastStep) / 268111856.0f;
            if (dt > 0.12f) dt = 0.12f;
            sLastStep = now;
            sGrStep++;
            gb_roam_physics(dt);
            gb_roam_draw();
            sGrSteps++;
            sGrStepTicks += svcGetSystemTick() - now;
        }
    }
    sShownAt = now;
    sGbNoWait = false;
}

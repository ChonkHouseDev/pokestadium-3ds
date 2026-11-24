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
struct GrMon { u8 dex, slot, st, alt; float x, y, vx, vy, t; bool right; };
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
static u32 gb_roam_scan(GrMon* out, int* n) {
    u32 sig = 2166136261u;
    *n = 0;
    for (int s = 0; s < GB_SLOTS; s++) {
        if (!sGb[s].present || !sGb[s].file || sGb[s].fileSize < GB_RAM_SIZE) continue;
        const u8* p = sGb[s].file + 0x2F2C;
        const int cnt = p[0];
        if (cnt < 1 || cnt > 6) continue;
        for (int i = 0; i < cnt && *n < GR_MAX; i++) {
            const u8 dex = p[1 + i] <= 190 ? kGen1Dex[p[1 + i]] : 0;
            if (!dex) continue;
            GrMon& m = out[(*n)++];
            m.dex = dex; m.slot = (u8)s;
            // el Pikachu inicial de Yellow (mismo entrenador que la partida) usa su mini aparte, si lo hay
            const u8* mon = p + 8 + 44 * i;
            m.alt = sGb[s].e.version == GB_VER_YELLOW && dex == 25 && mon[12] == sGb[s].file[0x2605] && mon[13] == sGb[s].file[0x2606];
            sig = (sig ^ (u32)(dex | s << 8)) * 16777619u;
        }
    }
    return sig ^ (u32)*n;
}
static void gb_roam_place(void) {
    for (int i = 0; i < sGrN; i++) {
        GrMon& m = sGr[i];
        m.st = GR_WALK; m.vx = m.vy = 0; m.t = gr_rand() * 1.5f; m.right = gr_rand() < 0.5f;
        // repartidos en una cuadricula de 4 x 3 con algo de azar
        m.x = GR_X0 + 40 + (i % 4) * 74 + (gr_rand() - 0.5f) * 30.0f;
        m.y = GR_Y0 + 34 + (i / 4) * 52 + (gr_rand() - 0.5f) * 20.0f;
    }
    sGrHeld = -1;
}
static inline void gr_put(int x, int y, u32 rgb, int a) { if (x > GR_X0 && x < GR_X1 - 1 && y > GR_Y0 && y < GR_Y1 - 1) gb_blend(x, y, rgb, a); }
static void gb_roam_sprite(const GrMon& m) {
    const int cx = (int)(m.x + 0.5f), cy = (int)(m.y + 0.5f);
    const bool moving = m.st == GR_WALK && (m.vx != 0.0f || m.vy != 0.0f);
    const int lift = m.st == GR_HELD ? 6 : m.st == GR_FLY ? 4 : (moving && ((sGrStep + (u32)(&m - sGr)) & 2)) ? 2 : 0;
    // aro en el piso, del color del cartucho
    const u32 ring = sGb[m.slot].e.version >= 0 && sGb[m.slot].e.version < GB_VER_COUNT ? kGbVerColor[sGb[m.slot].e.version] : 0xFFFFFF;
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
        gb_bg("Grab them!");
        gb_rect_title("YOUR POKEMON");
        gb_panel(GR_X0 - 1, GR_Y0 - 1, GR_X1 - GR_X0 + 2, GR_Y1 - GR_Y0 + 2, 0x2E7846, 0x78C882, 1, 255, 8);
        u32 h = 0x9E3779B9u;
        for (int i = 0; i < 70; i++) { // matas de pasto, regadas
            h = h * 1664525u + 1013904223u;
            const int x = GR_X0 + 8 + (int)((h >> 8) % (u32)(GR_X1 - GR_X0 - 18)), y = GR_Y0 + 10 + (int)((h >> 20) % (u32)(GR_Y1 - GR_Y0 - 30));
            const u32 c = (h & 1) ? 0x4FA364 : 0x24693A;
            for (int k = 0; k < 5; k++) { gb_blend(x - k / 2, y - k, c, 255); gb_blend(x + 3 + k / 2, y - k, c, 255); if (k < 4) gb_blend(x + 1, y - k - 1, c, 255); }
        }
        char t[64] = "";
        for (int s = 0; s < GB_SLOTS; s++)
            if (sGb[s].present) snprintf(t + strlen(t), sizeof t - strlen(t), "%sGB %d: %s", t[0] ? "   " : "", s + 1, sGb[s].e.version >= 0 && sGb[s].e.version < GB_VER_COUNT ? kGbVerNameEn[sGb[s].e.version] : "?");
        gb_text(160 - gb_tw(t) / 2, GR_Y1 - 17, t, 0xB4E6BE);
        gb_panel(8, 210, 198, 24, 0x1C2C90, 0x6088F0, 1, 235);
        gb_label(107, 215, "Flick them away - they come back", 0xDCE4FF, kUiFontSm, true);
        gb_button(212, 210, 100, 24, "L", 0xC89628, "Game Paks", 0x1C2C90, 0x6088F0);
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
            const float s = ha ? 1.0f : -1.0f;
            o.vx = nx * 26.0f * s; o.vy = ny * 26.0f * s; o.t = 0.8f;
            sGrBumps++;
            continue;
        }
        const float vr = (a.vx - b.vx) * nx + (a.vy - b.vy) * ny;
        if (vr <= 0.0f) continue; // ya se alejan
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
        if (best >= 0) { sGrHeld = best; sGr[best].st = GR_HELD; sGrHistN = 0; sGrGrabs++; }
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
        GrMon tmp[GR_MAX]; int n = 0;
        const u32 sig = gb_roam_scan(tmp, &n);
        if (sig != sGrSig) {
            sGrSig = sig; sGrN = n;
            for (int i = 0; i < n; i++) { sGr[i].dex = tmp[i].dex; sGr[i].slot = tmp[i].slot; sGr[i].alt = tmp[i].alt; }
            gb_roam_place();
            u8 recs[14]; int k = 0;
            for (int i = 0; i < n; i++) recs[k++] = sGr[i].dex;
            for (int i = 0; i < n && k < 14; i++) if (sGr[i].alt) { recs[k++] = GB_MINI_ALT; break; }
            gb_mini_prime(recs, k);
            if (sGrView == 0) sGrDrawn = false;
        }
    }
    // L o tocar el boton: cambiar entre los Pokemon y los cartuchos
    const bool lNow = (held & KEY_L) != 0;
    const bool tapEdge = down && !sTapWas;
    bool toggle = lNow && !sLWas;
    if (tapEdge && sGrView == 0 && px >= 212 && py >= 210) toggle = true;
    if (tapEdge && sGrView == 1 && py >= GB_FMT_Y) toggle = true;
    sLWas = lNow; sTapWas = down;
    if (toggle && sGrN > 0) { sGrView ^= 1; gb_roam_reset(); }
    const int view = sGrN > 0 ? sGrView : 1;
    sGbRoamHint = sGrN > 0;
    sGbNoWait = true;
    if (!sGrDrawn) {
        if (view == 0) { gb_roam_full(); if (!sLayer || sLayerKind != 3) { sGrView = 1; gb_draw_main(sGbUiList, sGbUiPick, -1, sGbUiAspect, -1, true); } }
        else gb_draw_main(sGbUiList, sGbUiPick, -1, sGbUiAspect, -1, true);
        sGrDrawn = true; sLastStep = now; sNextStep = 0; sShownAt = now;
    }
    if (view == 0 && sGrView == 0 && sLayer && sLayerKind == 3) {
        sGrShownTicks += now - sShownAt;
        gb_roam_touch(down && py < 208, px, py, now);
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

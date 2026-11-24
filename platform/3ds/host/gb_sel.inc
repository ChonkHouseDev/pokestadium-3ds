// ---- Eleccion de Pokemon antes de la batalla, tocando (pantalla "Choose the order", fragment64) ----
// Se incluye desde gb_saves.inc. El juego publica en gPsSel (func_84802C40) el estado del jugador humano del control 1.
// En el original cada Pokemon se elige con su boton (B, C izquierda, C arriba, A, C abajo, C derecha); aqui tocar su
// tarjeta manda ese boton. "OK!" contesta "Yes" a "Is this OK?" y "Start over" borra lo elegido (L, o "Re-select").
#ifndef PS_SPECIES_GEN1
#define PS_SPECIES_GEN1
#include "species_gen1.inc"
#endif
struct GbSel { u32 st, cnt, have, need, cur, mon[6], picks, wait, mv[6], pp[6]; };
static int sSelInfo = -1;      // ficha abierta (dejar presionada una tarjeta): sus 4 ataques
static int sSelHold = -1, sSelHoldStep; // tarjeta que se esta dejando presionada (otro color y una barra que se llena)
static u32 sSelTick;
static u64 sSelTickAt;
static bool sSelShown;
static GbSel sSelPrev;
static u16 sSelQ[4];          // botones por mandar, en orden
static int sSelQn, sSelQphase; // cada uno: 3 lecturas apretado y 3 suelto
static u32 sSelTaps, sSelSent, sSelBad, sSelFulls;
static int sSelFlash = -1;     // tarjeta tocada sin efecto (ya elegida): se marca en rojo un momento
static u64 sSelFlashUntil;
#define SEL_CARD_X(i) (8 + ((i) % 3) * 102)
#define SEL_CARD_Y(i) (31 + ((i) / 3) * 70)
#define SEL_CARD_W 98
#define SEL_CARD_H 66
static bool gb_sel_read(GbSel* s, u64 now) {
    if (!BLOB_SEL) return false;
    const u32 t = intro_be(BLOB_SEL);
    if (t != sSelTick) { sSelTick = t; sSelTickAt = now; }
    if (sSelTickAt == 0 || now - sSelTickAt > 268111856ull / 2) return false; // el juego ya no esta en esa pantalla
    s->st = intro_be(BLOB_SEL + 4); s->cnt = intro_be(BLOB_SEL + 8); s->have = intro_be(BLOB_SEL + 12); s->need = intro_be(BLOB_SEL + 16);
    s->cur = intro_be(BLOB_SEL + 20);
    for (int i = 0; i < 6; i++) s->mon[i] = intro_be(BLOB_SEL + 24 + 4 * i);
    s->picks = intro_be(BLOB_SEL + 48); s->wait = intro_be(BLOB_SEL + 52);
    for (int i = 0; i < 6; i++) { s->mv[i] = intro_be(BLOB_SEL + 64 + 4 * i); s->pp[i] = intro_be(BLOB_SEL + 88 + 4 * i); }
    return s->cnt >= 1 && s->cnt <= 6 && s->st <= 4 && s->have <= 6 && s->need >= 1 && s->need <= 6;
}
static int gb_sel_order(const GbSel& s, int idx) { // lugar (1..) en que se eligio ese Pokemon; 0: no elegido
    for (u32 k = 0; k < s.have && k < 6; k++) if ((int)((s.picks >> (4 * k)) & 15) == idx + 1) return (int)k + 1;
    return 0;
}
static void gb_sel_mini(int cx, int cy, int species, int level) {
    if (const u8* px = gb_mini_get(species)) { gb_mini_draw(px, cx - 16, cy - 16, level, 32, false); return; }
    gb_panel(cx - 14, cy - 14, 28, 28, level ? 0x8C8C96 : 0xF8C828, 0x141840, 1, 255, 7);
    char t[8];
    snprintf(t, sizeof t, "#%d", species);
    gb_text(cx - gb_tw(t) / 2, cy - 7, t, 0x1E1E32);
}
static void gb_sel_draw(const GbSel& s, u64 now) {
    if (!gb_screen_begin()) return;
    char t[40];
    snprintf(t, sizeof t, "Pick %lu", (unsigned long)s.need);
    gb_bg(t);
    gb_rect_title("CHOOSE YOUR TEAM");
    for (int i = 0; i < 6; i++) {
        const int x = SEL_CARD_X(i), y = SEL_CARD_Y(i);
        if (i >= (int)s.cnt) { gb_panel(x, y, SEL_CARD_W, SEL_CARD_H, 0x0E1658, 0x34489C, 1, 150); continue; }
        const int ord = gb_sel_order(s, i), sp = (int)(s.mon[i] & 0xFF), lv = (int)((s.mon[i] >> 8) & 0xFF);
        const bool bad = i == sSelFlash && now < sSelFlashUntil, hold = i == sSelHold;
        gb_panel(x, y, SEL_CARD_W, SEL_CARD_H, hold ? 0x8C4A10 : ord ? 0x2436A0 : 0x16227C, bad ? 0xFF5040 : hold ? 0xFFA040 : ord ? 0xFFD830 : 0x6088F0, ord || bad || hold ? 2 : 1, 240);
        if (hold) { // se llena mientras se deja presionada; llena, se abre la ficha
            const int w = (SEL_CARD_W - 12) * (sSelHoldStep + 1) / 5;
            gb_panel(x + 6, y + SEL_CARD_H - 9, SEL_CARD_W - 12, 5, 0x40200A, 0x40200A, 1, 255, 2);
            gb_panel(x + 6, y + SEL_CARD_H - 9, w, 5, 0xFFA040, 0xFFA040, 1, 255, 2);
        }
        gb_sel_mini(x + SEL_CARD_W / 2, y + 22, sp, 0);
        char nm[24];
        gb_fit(nm, sizeof nm, sp >= 1 && sp <= 151 ? kGen1Names[sp] : "?", SEL_CARD_W - 8);
        gb_label(x + SEL_CARD_W / 2, y + 38, nm, 0xFFFFFF, kUiFontSm, true);
        snprintf(t, sizeof t, "L%d", lv);
        gb_label(x + SEL_CARD_W / 2, y + 50, t, 0xC8D8FF, kUiFontSm, true);
        if (ord) {
            gb_disc(x + 12, y + 12, 9.0f, 0x141840); gb_disc(x + 12, y + 12, 8.0f, 0xFFD830);
            snprintf(t, sizeof t, "%d", ord);
            gb_text(x + 12 - gb_tw(t) / 2, y + 5, t, 0x141840);
        }
    }
    // los elegidos, en orden
    gb_panel(8, 173, 224, 34, 0x0E1658, 0x6088F0, 1, 235);
    gb_label(14, 176, "ENTRY", 0xFFD830);
    snprintf(t, sizeof t, "%lu / %lu", (unsigned long)s.have, (unsigned long)s.need);
    gb_label(14, 189, t, 0xFFFFFF);
    const int sw = (232 - 66) / (int)s.need;
    for (u32 k = 0; k < s.need; k++) {
        const int cx = 66 + sw * (int)k + sw / 2, idx = (int)((s.picks >> (4 * k)) & 15) - 1;
        if (k < s.have && idx >= 0 && idx < 6) gb_sel_mini(cx, 190, (int)(s.mon[idx] & 0xFF), 0);
        else gb_panel(cx - 11, 179, 22, 22, 0x0A1048, 0x6088F0, 1, 255, 5);
    }
    const bool ask = s.st == 1, done = s.st == 3;
    gb_panel(236, 173, 76, 34, ask ? 0x189040 : done ? 0x1C2C90 : 0x282C46, ask ? 0xA8F4C0 : done ? 0x6088F0 : 0x6E6E82, ask ? 2 : 1, 240);
    gb_label(274, 176, done ? "Ready" : "OK!", ask || done ? 0xFFFFFF : 0x9696A5, kUiFontLg, true);
    gb_panel(8, 211, 148, 24, done ? 0x282C46 : 0x782424, done ? 0x6E6E82 : 0xFFA096, 1, 235);
    gb_label(82, 216, "Start over", done ? 0x9696A5 : 0xFFFFFF, kUiFontSm, true);
    gb_panel(164, 211, 148, 24, 0x101A66, 0x6088F0, 1, 225);
    gb_label(238, 216, s.st == 2 ? "Not allowed" : ask ? "Is this OK?" : done ? "Waiting..." : s.wait ? "Wait..." : "Tap in order", s.st == 2 ? 0xFF8070 : 0xDCE4FF, kUiFontSm, true);
    if (sSelInfo >= 0 && sSelInfo < (int)s.cnt && s.st != 2) { // la ficha: sus 4 ataques, como en la batalla
        const int i = sSelInfo, sp = (int)(s.mon[i] & 0xFF), lv = (int)((s.mon[i] >> 8) & 0xFF);
        gb_panel(6, 30, 308, 178, 0x0E1A6E, 0xFFD830, 2, 250);
        gb_sel_mini(26, 52, sp, 0);
        gb_label(48, 36, sp >= 1 && sp <= 151 ? kGen1Names[sp] : "?", 0xFFFFFF, kUiFontLg);
        snprintf(t, sizeof t, "L%d   #%d", lv, sp);
        gb_label(48, 56, t, 0xC8D8FF);
        gb_label(304 - gb_tw("Moves"), 40, "Moves", 0xFFD830);
        for (int k = 0; k < 4; k++) {
            const int x = 12 + (k & 1) * 150, y = 76 + (k >> 1) * 46;
            const int mv = (int)((s.mv[i] >> (24 - 8 * k)) & 0xFF), pp = (int)((s.pp[i] >> (24 - 8 * k)) & 0xFF);
            if (mv < 1 || mv > 165) { gb_panel(x, y, 146, 42, 0x1A2060, 0x40508C, 1, 200); gb_label(x + 73, y + 13, "-", 0x9696A5, kUiFontSm, true); continue; }
            const GbTypeInfo& ty = gb_type_of_move(mv);
            gb_panel(x, y, 146, 42, gb_shade(ty.color, 78), gb_shade(ty.color, 135), 2, 240);
            char nm[40];
            gb_fit(nm, sizeof nm, kMoveName[mv], 130);
            gb_label(x + 8, y + 3, nm, 0xFFFFFF);
            const int tw = gb_tw(ty.name) + 12;
            gb_panel(x + 7, y + 22, tw, 15, 0xFFFFFF, 0xFFFFFF, 1, 255, 7);
            gb_text(x + 13, y + 22, ty.name, gb_shade(ty.color, 60));
            snprintf(t, sizeof t, "PP %d", pp);
            gb_label(x + 139 - gb_tw(t), y + 22, t, 0xFFFFFF);
        }
        const bool can = s.st == 0 && !gb_sel_order(s, i);
        gb_panel(12, 172, 140, 30, 0x2C3A9C, 0x6088F0, 1, 245);
        gb_label(82, 179, "Back", 0xFFFFFF, kUiFontSm, true);
        gb_panel(160, 172, 148, 30, can ? 0x189040 : 0x282C46, can ? 0xA8F4C0 : 0x6E6E82, 1, 245);
        gb_label(234, 179, gb_sel_order(s, i) ? "Already picked" : "Pick this one", can ? 0xFFFFFF : 0x9696A5, kUiFontSm, true);
    }
    if (s.st == 2) { // la regla no deja esa combinacion (suma de niveles): el juego espera un boton y borra lo elegido
        gb_panel(40, 84, 240, 56, 0x782424, 0xFFA096, 2, 245);
        gb_label(160, 92, "The rules don't allow this team", 0xFFFFFF, kUiFontSm, true);
        gb_label(160, 112, "Tap to choose again", 0xFFD830, kUiFontSm, true);
    }
    gb_screen_end();
    sSelFulls++;
}
static void gb_sel_push(u16 b) { if (sSelQn < 4) { if (sSelQn == 0) sSelQphase = 0; sSelQ[sSelQn++] = b; sSelSent++; } }
// lo que se manda al juego en cada lectura del control
static u16 gb_sel_buttons(void) {
    if (sSelQn == 0) return 0;
    const u16 b = sSelQphase < 3 ? sSelQ[0] : 0;
    if (++sSelQphase >= 6) { sSelQphase = 0; sSelQn--; memmove(&sSelQ[0], &sSelQ[1], sizeof sSelQ[0] * 3); }
    return b;
}
static void gb_sel_tap(const GbSel& s, int px, int py, u64 now) {
    static const u16 kBtn[6] = { 0x4000, 0x0002, 0x0008, 0x8000, 0x0004, 0x0001 }; // B, C izq, C arriba, A, C abajo, C der
    sSelTaps++;
    if (sSelQn > 0 || s.wait) { sSelBad++; return; } // todavia va lo anterior, o la pantalla va entrando
    if (s.st == 2) { gb_sel_push(0x8000); return; }
    if (s.st == 3) return;
    if (py >= 173 && py < 207 && px >= 236) { // OK!
        if (s.st != 1) { sSelBad++; return; }
        if (s.cur != 0) gb_sel_push(0x0200);
        gb_sel_push(0x8000);
        return;
    }
    if (py >= 211 && px < 156) { // Start over
        if (s.st == 1) { if (s.cur == 0) gb_sel_push(0x0100); gb_sel_push(0x8000); }
        else if (s.have > 0) gb_sel_push(0x0020);
        return;
    }
    if (s.st != 0) return;
    for (int i = 0; i < (int)s.cnt; i++) {
        const int x = SEL_CARD_X(i), y = SEL_CARD_Y(i);
        if (px < x || px >= x + SEL_CARD_W || py < y || py >= y + SEL_CARD_H) continue;
        if (gb_sel_order(s, i)) { sSelFlash = i; sSelFlashUntil = now + 268111856ull / 3; sSelBad++; }
        else gb_sel_push(kBtn[i]);
        return;
    }
}
// cada vuelta del host fuera de batalla; true: la pantalla de abajo es la de eleccion
static bool gb_sel_tick(u64 now) {
    static bool sTouchWas, sFlashOn;
    static u64 sNext;
    GbSel s;
    if (!gb_sel_read(&s, now)) {
        if (sSelShown) { sSelShown = false; sSelQn = 0; sSelInfo = -1; }
        return false;
    }
    // Toque corto: igual que siempre (al soltar). Dejar presionada una tarjeta medio segundo abre su ficha con los ataques;
    // con la ficha abierta, "Back" la cierra y "Pick this one" lo elige.
    const bool down = (hidKeysHeld() & KEY_TOUCH) != 0;
    static int sDownX, sDownY, sDownCard = -1;
    static u64 sDownAt;
    static bool sLongDone;
    static int sInfoWas = -1;
    if (down) {
        touchPosition tp; hidTouchRead(&tp);
        if (!sTouchWas) {
            sDownX = tp.px; sDownY = tp.py; sDownAt = now; sLongDone = false; sDownCard = -1;
            if (sSelInfo < 0)
                for (int i = 0; i < (int)sSelPrev.cnt; i++)
                    if (tp.px >= SEL_CARD_X(i) && tp.px < SEL_CARD_X(i) + SEL_CARD_W && tp.py >= SEL_CARD_Y(i) && tp.py < SEL_CARD_Y(i) + SEL_CARD_H) sDownCard = i;
            // lo que no es una tarjeta (OK!, Start over) va al momento, como antes
            if (sSelShown && sSelInfo < 0 && sDownCard < 0) gb_sel_tap(sSelPrev, tp.px, tp.py, now);
        } else if (sDownCard >= 0 && !sLongDone && now - sDownAt > 268111856ull / 2) {
            sLongDone = true; sSelInfo = sDownCard; // abrir la ficha
        }
    } else if (sTouchWas && sSelShown) {
        if (sSelInfo >= 0 && !sLongDone) { // tocar dentro de la ficha
            const int i = sSelInfo;
            if (sDownY >= 172 && sDownY < 202 && sDownX >= 160 && sDownX < 308) { sSelInfo = -1; if (sSelPrev.st == 0 && !gb_sel_order(sSelPrev, i)) gb_sel_tap(sSelPrev, SEL_CARD_X(i) + 20, SEL_CARD_Y(i) + 20, now); }
            else if (sDownY >= 172 && sDownY < 202 && sDownX >= 12 && sDownX < 152) sSelInfo = -1;
        } else if (sDownCard >= 0 && !sLongDone) gb_sel_tap(sSelPrev, sDownX, sDownY, now);
    }
    sTouchWas = down;
    {
        const int hold = down && sDownCard >= 0 && !sLongDone && sSelInfo < 0 ? sDownCard : -1;
        u64 el = hold >= 0 ? now - sDownAt : 0;
        int step = (int)(el * 10 / 268111856ull); // 0..4 en medio segundo
        if (step > 4) step = 4;
        sSelHold = hold; sSelHoldStep = hold >= 0 ? step : 0;
    }
    {   // (solo para probar en el emulador) fichaprueba.txt = 1: abre la ficha de la primera tarjeta
        static int sT = -1;
        if (sT < 0) sT = host_flag("fichaprueba") == 1;
        if (sT == 1 && sSelShown && sSelPrev.st == 0) { sT = 2; sSelInfo = 0; }
    }
    if (sSelPrev.st != 0 && sSelPrev.st != 1 && sSelInfo >= 0 && sSelShown) sSelInfo = -1;
    {   // (solo para probar en el emulador) selprueba.txt = 1: cada segundo toca la siguiente tarjeta sin elegir (primero
        // una ya elegida, para ver el rechazo) y al final "OK!"
        static int sTest = -1;
        static u64 sT;
        if (sTest < 0) sTest = host_flag("selprueba") == 1;
        if (sTest && sSelShown && now - sT > 268111856ull) {
            sT = now;
            const GbSel& q = sSelPrev;
            if (q.st == 1) gb_sel_tap(q, 270, 190, now);
            else if (q.st == 2) gb_sel_tap(q, 160, 100, now);
            else if (q.st == 0) {
                static bool sDup;
                int i = (int)q.cnt - 1;
                while (i > 0 && gb_sel_order(q, i)) i--;
                if (q.have == 1 && !sDup) { sDup = true; i = (int)((q.picks & 15) - 1); }
                gb_sel_tap(q, SEL_CARD_X(i) + 40, SEL_CARD_Y(i) + 30, now);
            }
        }
    }
    const bool flash = now < sSelFlashUntil;
    static int sHoldWas = -1, sHoldStepWas;
    const bool same = sSelInfo == sInfoWas && sSelHold == sHoldWas && sSelHoldStep == sHoldStepWas;
    if (now < sNext && sSelShown && flash == sFlashOn && same) return true;
    sNext = now + 268111856ull / 15;
    if (sSelShown && flash == sFlashOn && same && memcmp(&s, &sSelPrev, sizeof s) == 0) return true;
    sInfoWas = sSelInfo; sHoldWas = sSelHold; sHoldStepWas = sSelHoldStep;
    if (!sSelShown) { // empieza: los minis del equipo, de una vez
        u8 recs[6]; int n = 0;
        for (u32 i = 0; i < s.cnt; i++) recs[n++] = (u8)(s.mon[i] & 0xFF);
        gb_mini_prime(recs, n);
    }
    sSelPrev = s; sSelShown = true; sFlashOn = flash;
    sGbNoWait = true;
    gb_sel_draw(s, now);
    sGbNoWait = false;
    return true;
}

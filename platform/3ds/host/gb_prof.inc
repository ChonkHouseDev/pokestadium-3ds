// ---- Profile (pantalla de abajo): el retrato y el nombre del jugador ----
// Se incluye desde gb_roam.inc. Los retratos se leen de la ROM del usuario: los de la US (battle_portraits) y, si esta
// la japonesa, los suyos; son 64x64 RGBA16 comprimidos uno por uno. Los nombres son los de las partidas de los
// cartuchos. La eleccion se guarda en la SD y se le pasa al juego (gPsProfPortrait, gPsProfName).
#define PF_US_ROM 0x533B20u
#define PF_JP_ROM (0x2F5940u | ROM_JP_BIT)
#define PF_CFG "sdmc:/3ds/pokestadium/profile.cfg"
static void yay0(const u8* src, u8* dst, u32 avail);
static int sPfSrc, sPfIdx = -1; // retrato: de que ROM (0 US, 1 japonesa) y cual; -1 = el del juego
static char sPfName[12];        // el nombre escogido (el primero de la lista es el que escribe el usuario)
static char sPfCustom[12] = "TRAINER"; // el nombre escrito con el teclado de la 3DS (de fabrica TRAINER)
static bool sPfKb;              // abrir el teclado en el siguiente host_pump (fuera del dibujo)
static int sPfCount[2] = { -1, -1 };
static bool sPfInit;
// lo que esta guardado (y lo que usa el juego); lo de arriba es lo que se esta escogiendo hasta tocar Save
static int sPfSavedSrc, sPfSavedIdx = -1;
static char sPfSavedName[12];
static char sPfSavedCustom[12] = "TRAINER";
static bool pf_dirty(void) { return sPfSrc != sPfSavedSrc || sPfIdx != sPfSavedIdx || strcmp(sPfName, sPfSavedName) != 0 || strcmp(sPfCustom, sPfSavedCustom) != 0; }
static u16 sPfImg[64 * 64];
static int sPfImgKey = -1;
static char sPfNames[1 + GB_SLOTS][12];
static int sPfNameN;

static int pf_count(int src) {
    if (sPfCount[src] < 0) {
        u8 h[16];
        sPfCount[src] = 0;
        if (host_rom_read(src ? PF_JP_ROM : PF_US_ROM, h, 16) == 16 && be32(h + 12) <= 128) sPfCount[src] = (int)be32(h + 12);
    }
    return sPfCount[src];
}
static bool pf_load(int src, int idx) {
    const int key = src * 256 + idx;
    if (sPfImgKey == key) return true;
    static u8 raw[0x4000], out[0x2000];
    const u32 base = src ? PF_JP_ROM : PF_US_ROM;
    u8 e[16];
    if (idx < 0 || idx >= pf_count(src) || host_rom_read(base + 16 + (u32)idx * 16, e, 16) != 16) return false;
    const u32 off = be32(e), size = be32(e + 4);
    if (size < 0x30 || size > sizeof raw || host_rom_read(base + off, raw, size) != size || memcmp(raw, "PERS-SZP", 8) != 0) return false;
    const u32 hs = be32(raw + 8);
    if (hs + 16 >= size || memcmp(raw + hs, "Yay0", 4) != 0 || be32(raw + hs + 4) != sizeof out) return false;
    yay0(raw + hs, out, size - hs);
    for (int i = 0; i < 64 * 64; i++) sPfImg[i] = (u16)(out[2 * i] << 8 | out[2 * i + 1]);
    sPfImgKey = key;
    return true;
}
// los nombres de entrenador de las partidas (texto de Game Boy -> ASCII)
static void pf_names(void) {
    sPfNameN = 1;
    strcpy(sPfNames[0], sPfCustom);
    for (int s = 0; s < GB_SLOTS; s++) {
        if (!sGb[s].present || !sGb[s].file || sGb[s].fileSize < GB_RAM_SIZE) continue;
        char t[12]; int n = 0;
        for (int i = 0; i < 7; i++) {
            const u8 c = sGb[s].file[0x2598 + i];
            if (c == 0x50) break;
            t[n++] = c >= 0x80 && c <= 0x99 ? (char)('A' + c - 0x80) : c >= 0xA0 && c <= 0xB9 ? (char)('a' + c - 0xA0) : c >= 0xF6 ? (char)('0' + c - 0xF6) : c == 0x7F ? ' ' : '?';
        }
        t[n] = 0;
        bool dup = n == 0;
        for (int k = 0; k < sPfNameN && !dup; k++) dup = !strcmp(sPfNames[k], t);
        if (!dup) strcpy(sPfNames[sPfNameN++], t);
    }
}
static void pf_push(void) { // al juego (big-endian)
    if (!BLOB_PROF_PORTRAIT || !BLOB_PROF_NAME) return;
    *(volatile u32*)BLOB_PROF_PORTRAIT = __builtin_bswap32((u32)(sPfSavedIdx < 0 ? -1 : sPfSavedSrc ? 0x100 + sPfSavedIdx : sPfSavedIdx));
    memcpy((void*)BLOB_PROF_NAME, sPfSavedName, sizeof sPfSavedName);
}
static void pf_save(void) {
    sPfSavedSrc = sPfSrc; sPfSavedIdx = sPfIdx; memcpy(sPfSavedName, sPfName, sizeof sPfName); strcpy(sPfSavedCustom, sPfCustom);
    FILE* f = fopen(PF_CFG, "w");
    if (f) { fprintf(f, "portrait=%s:%d\nname=%s\ncustom=%s\n", sPfSrc ? "jp" : "us", sPfIdx, sPfName, sPfCustom); fclose(f); }
    pf_push();
}
static void pf_init(void) {
    if (sPfInit) return;
    sPfInit = true;
    FILE* f = fopen(PF_CFG, "r");
    if (f) {
        char l[64];
        while (fgets(l, sizeof l, f)) {
            l[strcspn(l, "\r\n")] = 0;
            if (!strncmp(l, "portrait=", 9)) { sPfSrc = !strncmp(l + 9, "jp:", 3); sPfIdx = atoi(l + 12); }
            else if (!strncmp(l, "name=", 5)) { strncpy(sPfName, l + 5, sizeof sPfName - 1); sPfName[sizeof sPfName - 1] = 0; }
            else if (!strncmp(l, "custom=", 7) && l[7]) { strncpy(sPfCustom, l + 7, sizeof sPfCustom - 1); sPfCustom[sizeof sPfCustom - 1] = 0; }
        }
        fclose(f);
    }
    if (sPfIdx >= pf_count(sPfSrc)) sPfIdx = -1; // p. ej. se quito la ROM japonesa
    if (!sPfName[0]) strcpy(sPfName, sPfCustom); // sin nombre guardado (o el "Game default" de antes): TRAINER
    sPfSavedSrc = sPfSrc; sPfSavedIdx = sPfIdx; memcpy(sPfSavedName, sPfName, sizeof sPfName); strcpy(sPfSavedCustom, sPfCustom);
    pf_push();
    // vs_sfx.txt (en el RomFS o la SD): los sonidos de la entrada del VS japones, uno por renglon: "cuando que valor".
    // cuando: inicio | paso (0 = salen las flamas, justo antes de las tarjetas). que: musica | efecto | callar.
    // valor: en hexadecimal para musica y efecto, cuadros para callar. Hasta 8 renglones; # = comentario.
    if (BLOB_JP_SFX) {
        FILE* v = ps_romfs() ? fopen("romfs:/vs_sfx.txt", "r") : nullptr;
        if (!v) v = fopen("sdmc:/3ds/pokestadium/vs_sfx.txt", "r");
        if (v) {
            char l[96], w[16], k[16], val[16]; int n = 0;
            volatile u32* dst = (volatile u32*)BLOB_JP_SFX;
            while (n < 8 && fgets(l, sizeof l, v)) {
                if (l[0] == '#' || sscanf(l, "%15s %15s %15s", w, k, val) != 3) continue;
                const int type = !strcmp(k, "musica") ? 1 : !strcmp(k, "efecto") ? 2 : !strcmp(k, "callar") ? 3 : !strcmp(k, "menu") ? 4 : 0;
                if (!type) continue;
                const int when = !strcmp(w, "inicio") ? -1000 : atoi(w);
                const u32 x = (u32)strtoul(val, nullptr, type == 3 ? 10 : 16) & 0xFFFF;
                dst[n * 2] = __builtin_bswap32((u32)when); dst[n * 2 + 1] = __builtin_bswap32((u32)type << 16 | x);
                n++;
            }
            for (; n < 8; n++) { dst[n * 2] = __builtin_bswap32((u32)-2); dst[n * 2 + 1] = 0; } // el archivo manda completo
            fclose(v);
        }
    }
}
// Miniaturas (32x32) de una pagina de 20 retratos, para escoger en el cuadro de al lado
#define PF_COLS 4
#define PF_ROWS 5
#define PF_PAGE (PF_COLS * PF_ROWS)
static bool sPfPick;      // escogiendo retrato: el cuadro de la derecha muestra las miniaturas
static int sPfPage;
static u16 sPfThumb[PF_PAGE][32 * 32];
static int sPfThumbKey = -1, sPfThumbN;
static void pf_thumbs(void) {
    const int key = sPfSrc * 256 + sPfPage;
    if (sPfThumbKey == key) return;
    const int n = pf_count(sPfSrc), keep = sPfImgKey;
    static u16 big[64 * 64];
    memcpy(big, sPfImg, sizeof big);
    sPfThumbN = 0;
    for (int i = 0; i < PF_PAGE && sPfPage * PF_PAGE + i < n; i++, sPfThumbN++) {
        u16* t = sPfThumb[i];
        if (!pf_load(sPfSrc, sPfPage * PF_PAGE + i)) { memset(t, 0, 32 * 32 * 2); continue; }
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) { // promedio de 2x2
            u32 r = 0, g = 0, b = 0, a = 0;
            for (int k = 0; k < 4; k++) { const u16 c = sPfImg[(y * 2 + (k >> 1)) * 64 + x * 2 + (k & 1)]; r += (c >> 11) & 31; g += (c >> 6) & 31; b += (c >> 1) & 31; a += c & 1; }
            t[y * 32 + x] = (u16)((r / 4) << 11 | (g / 4) << 6 | (b / 4) << 1 | (a >= 2));
        }
    }
    memcpy(sPfImg, big, sizeof big); sPfImgKey = keep; // el retrato grande sigue siendo el elegido
    sPfThumbKey = key;
}
// El teclado de la 3DS para escribir el nombre (se llama desde host_pump, entre cuadros). Solo letras que tiene la
// fuente del juego: A-Z (las minusculas pasan a mayusculas), 0-9, espacio y . - ' ! ?; hasta 10.
static void gb_roam_reset(void);
static void pf_kb_run(void) {
    if (!sPfKb) return;
    sPfKb = false;
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_WESTERN, 2, 10);
    swkbdSetInitialText(&kb, sPfCustom);
    swkbdSetHintText(&kb, "Trainer name");
    swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, "OK", true);
    swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    char buf[64] = {0};
    const SwkbdButton b = swkbdInputText(&kb, buf, sizeof buf);
    if (b == SWKBD_BUTTON_CONFIRM) {
        char out[12]; int n = 0;
        for (const char* c = buf; *c && n < 10; c++) {
            char ch = *c;
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
            if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '.' || ch == '-' || ch == '\'' || ch == '!' || ch == '?') out[n++] = ch;
        }
        while (n > 0 && out[n - 1] == ' ') n--;
        out[n] = 0;
        int st = 0; while (out[st] == ' ') st++;
        if (out[st]) { strcpy(sPfCustom, out + st); strcpy(sPfName, sPfCustom); }
    }
    gb_roam_reset(); // la pantalla de abajo se vuelve a pintar (el teclado la tapo)
}
static inline u32 pf_rgb(u16 c) { return (u32)(((c >> 11) & 31) * 255 / 31) << 16 | (u32)(((c >> 6) & 31) * 255 / 31) << 8 | (u32)(((c >> 1) & 31) * 255 / 31); }
static void pf_draw(void) {
    if (!gb_screen_begin()) return;
    pf_names();
    gb_bg("L / R: switch");
    gb_rect_title("PROFILE");
    gb_panel(8, 32, 150, 172, 0x1C2C90, 0x6088F0, 1, 235);
    if (sPfIdx >= 0 && pf_load(sPfSrc, sPfIdx)) {
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
            const u16 c = sPfImg[y * 64 + x];
            if (!(c & 1)) continue;
            for (int k = 0; k < 4; k++) gb_blend(19 + x * 2 + (k & 1), 40 + y * 2 + (k >> 1), pf_rgb(c), 255);
        }
    } else gb_label(83, 96, "Game default", 0xDCE4FF, kUiFontSm, true);
    if (!sPfPick) {
        gb_label(83, 180, sPfName, 0xFFFFFF, kUiFontSm, true);
        gb_button(8, 210, 150, 24, ">", 0xC89628, "Change picture", 0x1C2C90, 0x6088F0);
        gb_panel(164, 32, 148, 202, 0x1C2C90, 0x6088F0, 1, 235);
        gb_label(238, 38, "TRAINER NAME", 0xFFD830, kUiFontSm, true);
        for (int i = 0; i < sPfNameN; i++) {
            const bool on = !strcmp(sPfNames[i], sPfName);
            gb_panel(170, 56 + i * 28, 136, 24, on ? 0x2E7846 : 0x16226E, on ? 0x78C882 : 0x4A66C8, 1, 255);
            gb_label(238, 61 + i * 28, sPfNames[i], on ? 0xFFFFFF : 0xC8D8FF, kUiFontSm, true);
        }
        if (!strcmp(sPfName, sPfCustom)) gb_label(238, 64 + sPfNameN * 28, "Tap again to type", 0x9FB4F0, kUiFontSm, true);
        else if (sPfNameN == 1) gb_label(238, 96, "Names come from", 0x9FB4F0, kUiFontSm, true), gb_label(238, 110, "your Game Paks", 0x9FB4F0, kUiFontSm, true);
        {   // guardar: hasta aqui se usa lo escogido
            const bool d = pf_dirty();
            gb_button(170, 196, 136, 30, "S", d ? 0x2E9A4C : 0x5A6A9A, d ? "Save" : "Saved", d ? 0x1E6A3A : 0x16226E, d ? 0x78C882 : 0x4A66C8);
        }
    } else {
        const int n = pf_count(sPfSrc), pages = (n + PF_PAGE - 1) / PF_PAGE;
        if (pf_count(1) > 0) { gb_panel(14, 178, 66, 22, 0x16226E, 0x4A66C8, 1, 255); gb_label(47, 182, sPfSrc ? "JP > US" : "US > JP", 0xFFD830, kUiFontSm, true); }
        gb_panel(pf_count(1) > 0 ? 86 : 50, 178, 66, 22, 0x16226E, 0x4A66C8, 1, 255); gb_label(pf_count(1) > 0 ? 119 : 83, 182, "Default", 0xC8D8FF, kUiFontSm, true);
        gb_button(8, 210, 72, 24, "<", 0xC89628, "Page", 0x1C2C90, 0x6088F0);
        gb_button(86, 210, 72, 24, ">", 0xC89628, "Page", 0x1C2C90, 0x6088F0);
        gb_panel(164, 32, 148, 202, 0x1C2C90, 0x6088F0, 1, 235);
        char t[24];
        snprintf(t, sizeof t, "%s  %d / %d", sPfSrc ? "JP" : "US", sPfPage + 1, pages);
        gb_label(238, 37, t, 0xFFD830, kUiFontSm, true);
        pf_thumbs();
        for (int i = 0; i < sPfThumbN; i++) {
            const int cx = 172 + (i % PF_COLS) * 34, cy = 54 + (i / PF_COLS) * 35;
            const bool on = sPfIdx == sPfPage * PF_PAGE + i;
            gb_panel(cx - 1, cy - 1, 34, 34, 0x0C1450, on ? 0xFFD830 : 0x4A66C8, 1, 255);
            for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) { const u16 c = sPfThumb[i][y * 32 + x]; if (c & 1) gb_blend(cx + x, cy + y, pf_rgb(c), 255); }
        }
    }
    gb_screen_end();
}
// un toque en la pantalla: true si cambio algo (hay que volver a pintar)
static bool pf_tap(int px, int py) {
    if (!sPfPick) {
        if (px >= 8 && px < 158 && py >= 36) { // el retrato o "Change picture": abre las miniaturas en la pagina del elegido
            sPfPick = true; sPfPage = sPfIdx >= 0 ? sPfIdx / PF_PAGE : 0;
            return true;
        }
        if (px >= 170 && px < 306 && py >= 56 && py < 56 + sPfNameN * 28 && (py - 56) % 28 < 24) {
            const int i = (py - 56) / 28;
            if (i == 0 && !strcmp(sPfName, sPfCustom)) { sPfKb = true; return false; } // ya escogido: a escribirlo
            strcpy(sPfName, sPfNames[i]);
            return true;
        }
        if (px >= 170 && px < 306 && py >= 196 && py < 226) { if (pf_dirty()) { pf_save(); return true; } return false; }
        return false;
    }
    const int n = pf_count(sPfSrc), pages = (n + PF_PAGE - 1) / PF_PAGE, jp = pf_count(1) > 0;
    if (py >= 210 && px >= 8 && px < 158 && pages > 0) { sPfPage = (sPfPage + (px < 83 ? pages - 1 : 1)) % pages; return true; }
    if (jp && py >= 176 && py < 202 && px >= 14 && px < 80) { sPfSrc ^= 1; sPfIdx = -1; sPfPage = 0; return true; }
    if (py >= 176 && py < 202 && px >= (jp ? 86 : 50) && px < (jp ? 152 : 116)) { sPfIdx = -1; sPfPick = false; return true; }
    if (px >= 171 && px < 171 + PF_COLS * 34 && py >= 53 && py < 53 + PF_ROWS * 35) {
        const int i = (py - 53) / 35 * PF_COLS + (px - 171) / 34;
        if (i < sPfThumbN) { sPfIdx = sPfPage * PF_PAGE + i; sPfPick = false; return true; }
        return false;
    }
    if (px >= 8 && px < 158 && py >= 36 && py < 172) { sPfPick = false; return true; } // tocar el retrato: cerrar sin cambiar
    return false;
}

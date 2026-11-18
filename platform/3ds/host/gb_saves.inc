// ---------------- Transfer Pak: cartuchos de Game Boy y pantalla de abajo ----------------
// Al arrancar, la pantalla de abajo muestra los dos Transfer Pak (controles 1 y 2) con el cartucho que tiene cada
// uno, y deja elegirlos (A o tocando; X vuelve a buscar y abre la lista), cambiar el formato de imagen (Y: pantalla completa o 4:3) y empezar
// (START). Todo se puede hacer tocando la pantalla. Mientras se juega queda un tablero fijo (se dibuja una vez).
// Un "cartucho" es una partida de Pokemon Rojo, Azul o Amarillo:
//  - un .sav/.srm/.dat de 32 KB en sdmc:/3ds/pokestadium/saves/ (o suelto en sdmc:/3ds/pokestadium/);
//  - un .gb/.gbc de Pokemon con el mismo nombre que la partida da la cabecera (la version) tal cual. Un .gb sin partida
//    no se ofrece: con el cartucho en blanco el juego se atoro en el Laboratorio (probado en el emulador);
//  - la partida de la Consola Virtual, si el juego esta instalado (ARCHIVE_USER_SAVEDATA, sav.dat), sin copiar nada.
// El juego los ve como cartuchos en Transfer Paks (port/os/os_misc.c, osGbpak*): aqui se emula el cartucho, es decir
// la cabecera, el controlador de bancos y su RAM. El control 2 solo "existe" cuando su Transfer Pak tiene cartucho.
// Lo que Stadium escribe (el Laboratorio mueve Pokemon y objetos) se guarda en el mismo archivo; antes de la primera
// escritura queda una copia: <archivo>.bak, o para la Consola Virtual saves/ConsolaVirtual_<version>_original.bak
// (una sola vez) y ..._anterior.bak (como estaba al arrancar). GB Tower sigue fuera del port.
// Lo elegido y el formato se recuerdan en gbpak.cfg. Incluido desde host_3ds.cpp.
#include <3ds/archive.h>

#define GB_DIR "sdmc:/3ds/pokestadium/saves"
#define GB_BASE "sdmc:/3ds/pokestadium"
#define GB_CFG "sdmc:/3ds/pokestadium/gbpak.cfg"
#define GB_RAM_SIZE 0x8000u
#define GB_MAX_FILE 0x40000u
#define GB_MAX_LIST 14
#define GB_SLOTS 2

extern "C" bool gPsAspect43_3DS; // gfx_citro3d (patch_citro3d.py): la imagen del juego en los 320 del centro

enum { GB_VER_NONE = -1, GB_VER_RED = 0, GB_VER_BLUE, GB_VER_YELLOW, GB_VER_GREEN, GB_VER_COUNT };
static const char* const kGbVerName[GB_VER_COUNT] = { "Rojo", "Azul", "Amarillo", "Verde" };
static const char* const kGbVerNameEn[GB_VER_COUNT] = { "Red", "Blue", "Yellow", "Green" }; // lo que se ve en pantalla (el juego va en ingles)
static const char* const kGbVerTitle[GB_VER_COUNT] = { "POKEMON RED", "POKEMON BLUE", "POKEMON YELLOW", "POKEMON GREEN" };
static const u32 kGbVerColor[GB_VER_COUNT] = { 0xD82820, 0x2458E0, 0xF0C418, 0x20A040 };

struct GbEntry {
    u32 vcLow;        // partida de la Consola Virtual: mitad baja del id del titulo (0 = archivo en la SD)
    char path[200];
    char name[64];
    int version;      // GB_VER_* o GB_VER_NONE si no se sabe
    bool romHeader;   // la cabecera sale de un .gb/.gbc junto a la partida
    u8 header[0x150];
};

struct GbSlot {
    bool present;
    GbEntry e;
    u8* orig;          // Consola Virtual: el archivo como estaba al arrancar (para las copias)
    bool vcMounted;
    u8* file;          // el archivo completo; los primeros 32 KB son la RAM del cartucho
    u32 fileSize;
    bool ramOn;
    u8 ramBank;
    bool dirty;
    u64 dirtyTick;
    bool backupDone;
    u32 reads, writes, flushes, flushFails;
};
static GbSlot sGb[GB_SLOTS];

static bool gb_ieq(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return *a == *b;
}
static bool gb_ifind(const char* hay, const char* needle) {
    const size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        for (; i < n; i++) {
            char x = hay[i];
            if (!x) return false;
            if (x >= 'A' && x <= 'Z') x += 32;
            if (x != needle[i]) break;
        }
        if (i == n) return true;
    }
    return false;
}

// Cabecera de un cartucho (0x100-0x14F) para la version dada: logo, nombre, tipo de cartucho y su suma.
static void gb_make_header(u8* h, int ver) {
    static const u8 logo[0x30] = { 0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
                                   0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
                                   0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
                                   0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E };
    memset(h, 0xFF, 0x150);
    h[0x100] = 0x00; h[0x101] = 0xC3; h[0x102] = 0x50; h[0x103] = 0x01;
    memcpy(h + 0x104, logo, sizeof logo);
    memset(h + 0x134, 0, 0x1C);
    memcpy(h + 0x134, kGbVerTitle[ver], strlen(kGbVerTitle[ver]));
    if (ver == GB_VER_YELLOW) h[0x143] = 0x80;          // compatible con Game Boy Color
    h[0x144] = '0'; h[0x145] = '1';                      // fabricante
    h[0x146] = 0x03;                                     // Super Game Boy
    h[0x147] = ver == GB_VER_YELLOW ? 0x1B : 0x13;       // controlador de bancos + RAM + pila
    h[0x148] = 0x05;                                     // ROM de 1 MB
    h[0x149] = 0x03;                                     // RAM de 32 KB
    h[0x14A] = 0x01;                                     // no japones (Stadium lo exige)
    h[0x14B] = 0x33;
    h[0x14C] = 0x00;
    u8 x = 0;
    for (int i = 0x134; i <= 0x14C; i++) x = (u8)(x - h[i] - 1);
    h[0x14D] = x;
    h[0x14E] = 0; h[0x14F] = 0;                          // suma de toda la ROM: Stadium no la revisa
}
static int gb_version_of_title(const u8* h) {
    char t[17];
    memcpy(t, h + 0x134, 16);
    t[15] = 0; // el byte 16 puede ser la marca de Game Boy Color
    for (int v = 0; v < GB_VER_COUNT; v++) if (!strcmp(t, kGbVerTitle[v])) return v;
    return GB_VER_NONE;
}
static int gb_version_of_name(const char* name) {
    if (gb_ifind(name, "yellow") || gb_ifind(name, "amarill")) return GB_VER_YELLOW;
    if (gb_ifind(name, "blue") || gb_ifind(name, "azul")) return GB_VER_BLUE;
    if (gb_ifind(name, "green") || gb_ifind(name, "verde")) return GB_VER_GREEN;
    if (gb_ifind(name, "red") || gb_ifind(name, "roj")) return GB_VER_RED;
    return GB_VER_NONE;
}

// ---- lo recordado entre arranques: el cartucho de cada Transfer Pak, el formato y la version de cada archivo ----
struct GbCfg { bool has; char last[GB_SLOTS][200]; int aspect43; char verPath[GB_MAX_LIST][200]; int ver[GB_MAX_LIST]; int n; };
static void gb_cfg_load(GbCfg* c) {
    memset(c, 0, sizeof *c);
    FILE* f = fopen(GB_CFG, "r");
    if (!f) return;
    char line[260];
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!strncmp(line, "ultimo=", 7)) { snprintf(c->last[0], sizeof c->last[0], "%s", line + 7); c->has = true; }
        else if (!strncmp(line, "ultimo2=", 8)) snprintf(c->last[1], sizeof c->last[1], "%s", line + 8);
        else if (!strncmp(line, "formato=", 8)) c->aspect43 = !strcmp(line + 8, "43");
        else if (!strncmp(line, "version=", 8) && c->n < GB_MAX_LIST && line[8] >= '0' && line[8] < '0' + GB_VER_COUNT && line[9] == ' ') {
            c->ver[c->n] = line[8] - '0';
            snprintf(c->verPath[c->n], sizeof c->verPath[0], "%s", line + 10);
            c->n++;
        }
    }
    fclose(f);
}
static void gb_cfg_set_ver(GbCfg* c, const char* path, int ver) {
    for (int i = 0; i < c->n; i++) if (!strcmp(c->verPath[i], path)) { c->ver[i] = ver; return; }
    if (c->n >= GB_MAX_LIST) { memmove(c->verPath[0], c->verPath[1], sizeof c->verPath[0] * (GB_MAX_LIST - 1)); memmove(&c->ver[0], &c->ver[1], sizeof(int) * (GB_MAX_LIST - 1)); c->n = GB_MAX_LIST - 1; }
    snprintf(c->verPath[c->n], sizeof c->verPath[0], "%s", path);
    c->ver[c->n++] = ver;
}
static void gb_cfg_save(const GbCfg* c) {
    FILE* f = fopen(GB_CFG, "w");
    if (!f) return;
    fprintf(f, "ultimo=%s\nultimo2=%s\nformato=%s\n", c->last[0], c->last[1], c->aspect43 ? "43" : "completa");
    for (int i = 0; i < c->n; i++) fprintf(f, "version=%d %s\n", c->ver[i], c->verPath[i]);
    fclose(f);
}

// ---- Consola Virtual ----
// Titulos de Pokemon Rojo/Azul/Amarillo de la Consola Virtual (id alto 00040000). Los japoneses no: Stadium (EUA)
// no acepta cartuchos japoneses.
struct GbVcTitle { u32 low; int ver; const char* lang; };
static const GbVcTitle kGbVc[] = {
    { 0x00171000, GB_VER_RED, "" },    { 0x00171100, GB_VER_BLUE, "" },    { 0x00171200, GB_VER_YELLOW, "" },
    { 0x00171900, GB_VER_RED, " ES" }, { 0x00171A00, GB_VER_BLUE, " ES" }, { 0x00171B00, GB_VER_YELLOW, " ES" },
    { 0x00171300, GB_VER_RED, " DE" }, { 0x00171400, GB_VER_BLUE, " DE" }, { 0x00171500, GB_VER_YELLOW, " DE" },
    { 0x00171600, GB_VER_RED, " FR" }, { 0x00171700, GB_VER_BLUE, " FR" }, { 0x00171800, GB_VER_YELLOW, " FR" },
    { 0x00171C00, GB_VER_RED, " IT" }, { 0x00171D00, GB_VER_BLUE, " IT" }, { 0x00171E00, GB_VER_YELLOW, " IT" },
};
static const char* const kGbVcDev[GB_SLOTS] = { "gbvc0", "gbvc1" }; // un nombre por Transfer Pak
#define GB_VC_SCAN "gbvcs"
static Result gb_vc_mount(u32 low, const char* dev) {
    static u32 sPath[3];
    sPath[0] = MEDIATYPE_SD; sPath[1] = low; sPath[2] = 0x00040000;
    FS_Path fp = { PATH_BINARY, sizeof sPath, sPath };
    return archiveMount(ARCHIVE_USER_SAVEDATA, fp, dev);
}
static const char* gb_file_path(int slot) { // el archivo de la partida de ese Transfer Pak
    static char vc[GB_SLOTS][24];
    if (!sGb[slot].e.vcLow) return sGb[slot].e.path;
    snprintf(vc[slot], sizeof vc[slot], "%s:/sav.dat", kGbVcDev[slot]);
    return vc[slot];
}
// Las partidas de la Consola Virtual que haya, al principio de la lista
static char sGbVcNote[160]; // para el resumen: que se encontro en la Consola Virtual
static int gb_scan_vc(GbEntry* out, int n) {
    if (host_flag("sinvc") == 1) return n;
    int found = 0;
    u32 codes[3] = { 0, 0, 0 };
    for (const GbVcTitle& t : kGbVc) {
        if (n >= GB_MAX_LIST - 2) break;
        const Result rc = gb_vc_mount(t.low, GB_VC_SCAN);
        if (R_FAILED(rc)) { // no esta instalado (o este programa no tiene permiso)
            if (&t - kGbVc < 3) codes[&t - kGbVc] = (u32)rc;
            continue;
        }
        found++;
        struct stat st;
        const bool ok = stat(GB_VC_SCAN ":/sav.dat", &st) == 0 && st.st_size >= (off_t)GB_RAM_SIZE && st.st_size <= (off_t)GB_MAX_FILE;
        logf("[gb] Consola Virtual %08lx (%s%s): %s\n", (unsigned long)t.low, kGbVerName[t.ver], t.lang,
             ok ? "con partida" : "instalado, sin partida guardada todavia");
        archiveUnmount(GB_VC_SCAN);
        if (!ok) continue;
        GbEntry e = {};
        e.vcLow = t.low;
        snprintf(e.path, sizeof e.path, "vc:%08lx", (unsigned long)t.low);
        snprintf(e.name, sizeof e.name, "Consola Virtual%s", t.lang);
        e.version = t.ver;
        out[n++] = e;
    }
    if (found) snprintf(sGbVcNote, sizeof sGbVcNote, "%d juego(s) instalado(s)", found);
    else snprintf(sGbVcNote, sizeof sGbVcNote, "no se encontro Rojo/Azul/Amarillo (codigos %08lx %08lx %08lx)", (unsigned long)codes[0], (unsigned long)codes[1], (unsigned long)codes[2]);
    return n;
}

// ---- buscar cartuchos en la SD ----
static bool gb_is_save_ext(const char* dot) { return gb_ieq(dot, ".sav") || gb_ieq(dot, ".srm") || gb_ieq(dot, ".dat"); }
static bool gb_read_rom_header(const char* path, u8* h) { // solo ROMs de Pokemon Rojo/Azul/Amarillo
    FILE* rf = fopen(path, "rb");
    if (!rf) return false;
    const size_t got = fread(h, 1, 0x150, rf);
    fclose(rf);
    if (got != 0x150 || gb_version_of_title(h) == GB_VER_NONE) return false;
    u8 x = 0; // la suma de la cabecera: con una cabecera danada el juego se atora al revisar el cartucho
    for (int i = 0x134; i <= 0x14C; i++) x = (u8)(x - h[i] - 1);
    return x == h[0x14D];
}
static int gb_scan_dir(const char* dir, GbEntry* out, int n) {
    DIR* d = opendir(dir);
    if (!d) return n;
    struct dirent* de;
    while (n < GB_MAX_LIST && (de = readdir(d)) != nullptr) { // las partidas
        const char* nm = de->d_name;
        const char* dot = strrchr(nm, '.');
        if (!dot || !gb_is_save_ext(dot)) continue;
        GbEntry e = {};
        if (snprintf(e.path, sizeof e.path, "%s/%s", dir, nm) >= (int)sizeof e.path) continue;
        struct stat st;
        if (stat(e.path, &st) != 0 || st.st_size < (off_t)GB_RAM_SIZE || st.st_size > (off_t)GB_MAX_FILE) continue;
        snprintf(e.name, sizeof e.name, "%s", nm);
        e.version = GB_VER_NONE;
        // un .gb o .gbc con el mismo nombre: de ahi sale la cabecera tal cual
        for (const char* ext : { ".gb", ".gbc", ".GB", ".GBC" }) {
            char rp[200];
            const int base = (int)(strlen(e.path) - strlen(dot));
            if (snprintf(rp, sizeof rp, "%.*s%s", base, e.path, ext) >= (int)sizeof rp) continue;
            if (gb_read_rom_header(rp, e.header)) { e.romHeader = true; e.version = gb_version_of_title(e.header); break; }
        }
        if (e.version == GB_VER_NONE) e.version = gb_version_of_name(nm);
        out[n++] = e;
    }
    closedir(d);
    return n;
}
// Todo lo que hay: primero la Consola Virtual, luego los archivos por nombre
static int gb_scan_all(GbEntry* list, const GbCfg* cfg) {
    const int nVc = gb_scan_vc(list, 0);
    int n = gb_scan_dir(GB_DIR, list, nVc);
    n = gb_scan_dir(GB_BASE, list, n);
    for (int i = nVc + 1; i < n; i++)
        for (int j = i; j > nVc && strcasecmp(list[j - 1].name, list[j].name) > 0; j--) { const GbEntry t = list[j]; list[j] = list[j - 1]; list[j - 1] = t; }
    for (int i = 0; i < n; i++)
        if (list[i].version == GB_VER_NONE)
            for (int j = 0; j < cfg->n; j++) if (!strcmp(cfg->verPath[j], list[i].path)) list[i].version = cfg->ver[j];
    return n;
}

// ---- pantalla de abajo ----
// Dibujado directo en el framebuffer de abajo (BGR8, 240x320 girado). No se usa la consola de libctru: se queda con la
// salida estandar y todo el registro se dibujaria ahi al jugar. Letras y cartuchos son graficos propios con suavizado
// (ui_gfx.inc, generado por tools/port/gen_ui3ds.py); fondo, tableros y botones se dibujan aqui, tambien suavizados.
#include "ui_gfx.inc"
// ---- etiquetas del usuario para los cartuchos ----
// El usuario puede poner su propia imagen en la etiqueta de cada cartucho. En su PC, agregar_portadas.bat pega sus
// imagenes (rojo/azul/amarillo .png/.jpg/.bmp) al final del .3dsx; al arrancar se leen de ahi, se ajustan al tamano
// de la etiqueta y se guardan en sdmc:/3ds/pokestadium/cartuchos/ (se quedan aunque despues llegue otro .3dsx).
// Igual los minis de los Pokemon (mini_NNN.png) y la base (ball.png) del tablero de batalla.
// El programa no trae ninguna imagen de esas: sin ellas, los cartuchos salen con su etiqueta dibujada.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "stb_image.h"
#define GB_LBL_X 9
#define GB_LBL_Y 19
#define GB_LBL_W 44
#define GB_LBL_H 36
#define GB_LBL_DIR "sdmc:/3ds/pokestadium/cartuchos"
static u8 sGbLbl[3][GB_LBL_W * GB_LBL_H * 3];
static bool sGbLblOn[3];
static const char* const kGbLblFile[3] = { "rojo", "azul", "amarillo" };
static const char* sGbSelfPath; // argv[0]
// recorte centrado con la proporcion de la etiqueta y promedio por zonas
static void gb_lbl_scale(const u8* src, int sw, int sh, u8* dst) {
    int cw = sw, ch = sw * GB_LBL_H / GB_LBL_W;
    if (ch > sh) { ch = sh; cw = sh * GB_LBL_W / GB_LBL_H; }
    const int ox = (sw - cw) / 2, oy = (sh - ch) / 2;
    for (int y = 0; y < GB_LBL_H; y++) for (int x = 0; x < GB_LBL_W; x++) {
        const int x0 = ox + x * cw / GB_LBL_W, y0 = oy + y * ch / GB_LBL_H;
        int x1 = ox + (x + 1) * cw / GB_LBL_W, y1 = oy + (y + 1) * ch / GB_LBL_H;
        if (x1 <= x0) x1 = x0 + 1;
        if (y1 <= y0) y1 = y0 + 1;
        u32 acc[3] = { 0, 0, 0 };
        for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) { const u8* p = src + (j * sw + i) * 3; acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; }
        const u32 n = (u32)((x1 - x0) * (y1 - y0));
        for (int k = 0; k < 3; k++) dst[(y * GB_LBL_W + x) * 3 + k] = (u8)(acc[k] / n);
    }
}
// Minis del usuario (una imagen por Pokemon) y la imagen de la base: se guardan ya ajustados en minis.bin, un registro
// de 32x32 RGBA por numero de Pokedex (el 0 es la base). En batalla se leen solo los que hacen falta.
#define GB_MINI_FILE GB_LBL_DIR "/minis.bin"
#define GB_MINI_REC (32 * 32 * 4)
#define GB_MINI_ALT 152 // registro extra: el Pikachu de Yellow (mini_152.png)
static u8 sMiniAltTest; // (solo para probar en el emulador: minialt.txt = N trata a la especie N como el Pikachu de Yellow)
static bool sGbMinisOn;
// recorta lo transparente, ajusta a 32x32 sin agrandar y lo deja centrado y pegado abajo
static void gb_mini_fit(const u8* src, int sw, int sh, u8* dst, int S = 32, bool base = false) {
    int x0 = sw, y0 = sh, x1 = -1, y1 = -1;
    for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++) if (src[(y * sw + x) * 4 + 3] > 40) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    memset(dst, 0, (size_t)S * S * 4);
    if (x1 < x0) return;
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    int dw = bw, dh = bh;
    if (dw > S || dh > S || base) { if (bw >= bh) { dw = S; dh = bh * S / bw; } else { dh = S; dw = bw * S / bh; } }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    const int ox = (S - dw) / 2, oy = base ? (S - dh) / 2 : S - dh;
    for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) {
        const int sx0 = x0 + x * bw / dw, sy0 = y0 + y * bh / dh;
        int sx1 = x0 + (x + 1) * bw / dw, sy1 = y0 + (y + 1) * bh / dh;
        if (sx1 <= sx0) sx1 = sx0 + 1;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        u32 acc[4] = { 0, 0, 0, 0 };
        for (int j = sy0; j < sy1; j++) for (int i = sx0; i < sx1; i++) { const u8* q = src + (j * sw + i) * 4; acc[0] += q[0] * q[3]; acc[1] += q[1] * q[3]; acc[2] += q[2] * q[3]; acc[3] += q[3]; }
        u8* d = dst + ((oy + y) * S + ox + x) * 4;
        const u32 n = (u32)((sx1 - sx0) * (sy1 - sy0));
        if (acc[3]) { d[0] = (u8)(acc[0] / acc[3]); d[1] = (u8)(acc[1] / acc[3]); d[2] = (u8)(acc[2] / acc[3]); }
        d[3] = (u8)(acc[3] / n);
    }
}
// La base se guarda aparte y un poco mas grande que los minis (GB_BASE_PX de lado), para que asome detras de ellos.
// Son tres: 0 la del jugador (ball.png), 1 la del rival (ball_rival.png; sin ella usa la 0) y 2 la de transicion
// (ball_swap.png): la que se ve mientras el Pokemon no ha salido, se cambia o se debilito.
#define GB_BASE_PX 40
#define GB_BASE_N 3
static const char* const kGbBaseFile[GB_BASE_N] = { GB_LBL_DIR "/base.bin", GB_LBL_DIR "/base2.bin", GB_LBL_DIR "/base3.bin" };
static u8 sGbBase[GB_BASE_N][GB_BASE_PX * GB_BASE_PX * 4];
static int sGbBaseOn[GB_BASE_N] = { -1, -1, -1 }; // -1 sin mirar, 0 no hay, 1 cargada
static bool gb_base_store(int n, const u8* rgba, int w, int h) {
    gb_mini_fit(rgba, w, h, sGbBase[n], GB_BASE_PX, true);
    sGbBaseOn[n] = -1;
    FILE* f = fopen(kGbBaseFile[n], "wb");
    if (!f) return false;
    const bool ok = fwrite(sGbBase[n], 1, sizeof sGbBase[n], f) == sizeof sGbBase[n];
    return fclose(f) == 0 && ok;
}
static const u8* gb_base_get(int n) {
    if (sGbBaseOn[n] < 0) {
        sGbBaseOn[n] = 0;
        if (FILE* f = fopen(kGbBaseFile[n], "rb")) { sGbBaseOn[n] = fread(sGbBase[n], 1, sizeof sGbBase[n], f) == sizeof sGbBase[n]; fclose(f); }
    }
    return sGbBaseOn[n] == 1 ? sGbBase[n] : nullptr;
}
static bool gb_mini_store(int rec, const u8* rgba, int w, int h) {
    static u8 out[GB_MINI_REC];
    gb_mini_fit(rgba, w, h, out);
    FILE* f = fopen(GB_MINI_FILE, "r+b");
    if (!f) { // se crea vacio (152 registros)
        f = fopen(GB_MINI_FILE, "w+b");
        static const u8 zero[GB_MINI_REC] = {};
        for (int i = 0; f && i < 152; i++) if (fwrite(zero, 1, sizeof zero, f) != sizeof zero) { fclose(f); f = nullptr; }
    }
    if (!f) return false;
    const bool ok = fseek(f, (long)rec * GB_MINI_REC, SEEK_SET) == 0 && fwrite(out, 1, sizeof out, f) == sizeof out;
    return fclose(f) == 0 && ok;
}
// el registro de un Pokemon (o 0, la base) si el usuario lo puso. Se guardan en memoria 14 (56 KB): alcanzan para los
// dos equipos, y al empezar la batalla se leen todos de una vez (gb_mini_prime), cuando no se nota: leerlos de la SD a
// media batalla detenia el juego la primera vez que salia cada uno. (Tener el archivo entero en memoria, 0.6 MB, le
// quitaba espacio a los modelos retenidos y al cache de assets.)
struct GbMiniSlot { int rec; bool ok; u8 px[GB_MINI_REC]; };
static GbMiniSlot sMiniC[14];
static int sMiniNext;
static bool sMiniInit;
static void gb_mini_read(FILE* f, GbMiniSlot& c, int rec) {
    c.rec = rec;
    c.ok = false;
    if (f && fseek(f, (long)rec * GB_MINI_REC, SEEK_SET) == 0 && fread(c.px, 1, GB_MINI_REC, f) == GB_MINI_REC)
        for (int i = 3; i < GB_MINI_REC && !c.ok; i += 4) c.ok = c.px[i] > 40;
}
static GbMiniSlot* gb_mini_slot(int rec) {
    if (!sMiniInit) { sMiniInit = true; for (GbMiniSlot& c : sMiniC) c.rec = -1; }
    for (GbMiniSlot& c : sMiniC) if (c.rec == rec) return &c;
    return nullptr;
}
static const u8* gb_mini_get(int rec) {
    if (!sGbMinisOn || rec < 0 || rec > GB_MINI_ALT) return nullptr;
    GbMiniSlot* c = gb_mini_slot(rec);
    if (!c) { // no estaba (no deberia pasar en batalla): se lee ahora
        c = &sMiniC[sMiniNext];
        sMiniNext = (sMiniNext + 1) % 14;
        FILE* f = fopen(GB_MINI_FILE, "rb");
        gb_mini_read(f, *c, rec);
        if (f) fclose(f);
    }
    return c->ok ? c->px : nullptr;
}
static void gb_mini_prime(const u8* recs, int n) {
    if (!sGbMinisOn) return;
    FILE* f = nullptr;
    for (int i = 0; i < n; i++) {
        if (recs[i] < 1 || recs[i] > GB_MINI_ALT || gb_mini_slot(recs[i])) continue;
        if (!f && !(f = fopen(GB_MINI_FILE, "rb"))) return;
        gb_mini_read(f, sMiniC[sMiniNext], recs[i]);
        sMiniNext = (sMiniNext + 1) % 14;
    }
    if (f) fclose(f);
}
static u32 gb_le32(const u8* p) { return p[0] | p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
// Lo que agregar_portadas.bat pego al final del .3dsx: [n][largo nombre][nombre][tamano LE][archivo]... [tamano LE]"PSCV"
extern "C" const u8 gPsUserPack[] __attribute__((weak));
extern "C" const u8 gPsUserPackEnd[] __attribute__((weak));
static void gb_lbl_import(void) {
    char self[220] = "";
    if (sGbSelfPath) {
        if (!strncmp(sGbSelfPath, "3dslink:/", 9)) snprintf(self, sizeof self, "sdmc:/3ds/%s", sGbSelfPath + 9);
        else snprintf(self, sizeof self, "%s", sGbSelfPath);
    }
    // Build personalizado: las imagenes van dentro del .elf (PS_USER_DIR al compilar) y mandan sobre todo lo demas.
    // En el .cia, las del usuario van en su RomFS como user.pscv (mismo formato que lo pegado al .3dsx)
    const bool romfsOk = ps_romfs();
    const char* const cand[] = { gPsUserPack ? "mem:user" : "", romfsOk ? "romfs:/user.pscv" : "", self, "sdmc:/3ds/pokestadium.3dsx", "sdmc:/3ds/pokestadium_diag.3dsx", "sdmc:/3ds/pokestadium/pokestadium.3dsx" };
    for (const char* path : cand) {
        if (!path[0]) continue;
        FILE* f = !strcmp(path, "mem:user") ? fmemopen((void*)gPsUserPack, (size_t)(gPsUserPackEnd - gPsUserPack), "rb") : fopen(path, "rb");
        if (!f) continue;
        u8 foot[8];
        u32 size = 0;
        if (fseek(f, -8, SEEK_END) == 0 && fread(foot, 1, 8, f) == 8 && !memcmp(foot + 4, "PSCV", 4)) size = gb_le32(foot);
        if (!size || size > 12u * 1024 * 1024 || fseek(f, -(long)(8 + size), SEEK_END) != 0) { fclose(f); continue; }
        // ya importadas de este mismo archivo: no se repite (abrir tres imagenes grandes tarda unos segundos)
        u8 head[16] = {};
        fread(head, 1, sizeof head, f);
        fseek(f, -(long)(8 + size), SEEK_END);
        char stamp[64] = "", prev[64] = "";
        int k = snprintf(stamp, sizeof stamp, "v3 %lu ", (unsigned long)size); // v3: las tres bases aparte (base*.bin)
        for (int i = 0; i < 16; i++) k += snprintf(stamp + k, sizeof stamp - k, "%02x", head[i]);
        mkdir(GB_LBL_DIR, 0777);
        if (FILE* sf = fopen(GB_LBL_DIR "/importado.txt", "r")) { if (!fgets(prev, sizeof prev, sf)) prev[0] = 0; fclose(sf); }
        if (!strcmp(prev, stamp)) { fclose(f); return; }
        u8 n = 0;
        fread(&n, 1, 1, f);
        logf("[etiquetas] %u imagen(es) pegadas en %s\n", (unsigned)n, path);
        u32 minis = 0;
        for (u32 i = 0; i < n; i++) {
            u8 nl = 0, sz[4];
            char name[64] = "";
            if (fread(&nl, 1, 1, f) != 1 || nl >= sizeof name || fread(name, 1, nl, f) != nl || fread(sz, 1, 4, f) != 4) break;
            name[nl] = 0;
            const u32 fs = gb_le32(sz);
            const int base = !strncmp(name, "ball_rival", 10) ? 1 : !strncmp(name, "ball_swap", 9) ? 2 : !strncmp(name, "ball", 4) ? 0 : -1;
            const int mini = !strncmp(name, "mini_", 5) ? atoi(name + 5) : base == 0 ? 0 : base > 0 ? GB_MINI_ALT + base : -1;
            if (mini >= 0 && mini <= GB_MINI_ALT + 2 && fs > 0 && fs < 512u * 1024) { // un mini o la base
                u8* data = (u8*)malloc(fs);
                int w = 0, h = 0, comp = 0;
                u8* img = data && fread(data, 1, fs, f) == fs ? stbi_load_from_memory(data, (int)fs, &w, &h, &comp, 4) : nullptr;
                free(data);
                if (!img) { logf("[etiquetas] %s: no se pudo abrir\n", name); if (!data) break; continue; }
                if (w <= 256 && h <= 256 && mini <= GB_MINI_ALT && gb_mini_store(mini, img, w, h)) minis++;
                if (base >= 0 && w <= 256 && h <= 256) gb_base_store(base, img, w, h);
                stbi_image_free(img);
                continue;
            }
            const int ver = gb_version_of_name(name);
            if (fs == 0 || fs > 6u * 1024 * 1024 || ver < 0 || ver > 2) { logf("[etiquetas] %s: no se usa (nombre o tamano)\n", name); if (fseek(f, (long)fs, SEEK_CUR) != 0) break; continue; }
            u8* data = (u8*)malloc(fs);
            if (!data || fread(data, 1, fs, f) != fs) { free(data); logf("[etiquetas] %s: no se pudo leer\n", name); break; }
            int w = 0, h = 0, comp = 0;
            u8* img = nullptr;
            if (stbi_info_from_memory(data, (int)fs, &w, &h, &comp) && w >= 8 && h >= 8 && w <= 2048 && h <= 2048) img = stbi_load_from_memory(data, (int)fs, &w, &h, &comp, 3);
            free(data);
            if (!img) { logf("[etiquetas] %s: no se pudo abrir (PNG, JPG o BMP de hasta 2048x2048)\n", name); continue; }
            static u8 out[GB_LBL_W * GB_LBL_H * 3];
            gb_lbl_scale(img, w, h, out);
            stbi_image_free(img);
            char dst[96];
            snprintf(dst, sizeof dst, "%s/%s.rgb", GB_LBL_DIR, kGbLblFile[ver]);
            FILE* o = fopen(dst, "wb");
            const bool ok = o && fwrite(out, 1, sizeof out, o) == sizeof out;
            if (o) fclose(o);
            logf("[etiquetas] %s (%dx%d) -> %s: %s\n", name, w, h, dst, ok ? "guardada" : "NO se pudo guardar");
        }
        if (minis) logf("[etiquetas] minis guardados: %lu (%s)\n", (unsigned long)minis, GB_MINI_FILE);
        if (FILE* sf = fopen(GB_LBL_DIR "/importado.txt", "w")) { fputs(stamp, sf); fclose(sf); }
        fclose(f);
        return;
    }
}
static void gb_lbl_load(void) {
    { struct stat st; sGbMinisOn = stat(GB_MINI_FILE, &st) == 0 && st.st_size >= (off_t)(152 * GB_MINI_REC); }
    for (int n = 0; n < GB_BASE_N; n++) gb_base_get(n);
    for (int v = 0; v < 3; v++) {
        char p[96];
        snprintf(p, sizeof p, "%s/%s.rgb", GB_LBL_DIR, kGbLblFile[v]);
        FILE* f = fopen(p, "rb");
        sGbLblOn[v] = f && fread(sGbLbl[v], 1, sizeof sGbLbl[v], f) == sizeof sGbLbl[v];
        if (f) fclose(f);
    }
}

static u8* sGbFb;
static inline void gb_blend(int x, int y, u32 rgb, int a) { // a: 0-255
    if ((unsigned)x >= 320u || (unsigned)y >= 240u || a <= 0) return;
    u8* p = sGbFb + (x * 240 + (239 - y)) * 3;
    const int b = rgb & 0xFF, g = (rgb >> 8) & 0xFF, r = (rgb >> 16) & 0xFF;
    if (a >= 255) { p[0] = (u8)b; p[1] = (u8)g; p[2] = (u8)r; return; }
    p[0] = (u8)(p[0] + (b - p[0]) * a / 255); p[1] = (u8)(p[1] + (g - p[1]) * a / 255); p[2] = (u8)(p[2] + (r - p[2]) * a / 255);
}
static int gb_tw(const char* t, const GbUiFont& f = kUiFontSm) {
    int w = 0;
    for (; *t; t++) { const unsigned ch = (unsigned char)*t; w += f.w[(ch >= 32 && ch < 127 ? ch : '?') - 32]; }
    return w;
}
static void gb_text(int x0, int y0, const char* t, u32 rgb, const GbUiFont& f = kUiFontSm) {
    for (; *t; t++) {
        const unsigned ch = (unsigned char)*t;
        const int gi = (ch >= 32 && ch < 127 ? ch : '?') - 32, w = f.w[gi];
        const u8* a = f.a + f.off[gi];
        for (int y = 0; y < f.h; y++) for (int x = 0; x < w + 2; x++) { const int v = a[y * (w + 2) + x]; if (v) gb_blend(x0 + x - 1, y0 + y, rgb, v); }
        x0 += w;
    }
}
// texto con sombra; center: x es el centro
static void gb_label(int x, int y, const char* t, u32 rgb, const GbUiFont& f = kUiFontSm, bool center = false) {
    if (center) x -= gb_tw(t, f) / 2;
    gb_text(x + 1, y + 1, t, 0x060C30, f);
    gb_text(x, y, t, rgb, f);
}
// copia el texto recortado a 'maxw' pixeles (con ".." si no cabe)
static void gb_fit(char* dst, size_t n, const char* src, int maxw) {
    snprintf(dst, n, "%s", src);
    size_t len = strlen(dst);
    while (len > 3 && gb_tw(dst) > maxw) { len--; dst[len] = 0; dst[len - 1] = '.'; dst[len - 2] = '.'; }
}
static bool gb_screen_begin(void) {
    u16 w = 0, h = 0;
    sGbFb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
    return sGbFb && w == 240 && h == 320;
}
static bool sGbNoWait; // en juego: se presenta sin esperar al retrazo
static void gb_screen_end(void) {
    gfxFlushBuffers();
    gfxScreenSwapBuffers(GFX_BOTTOM, false);
    if (!sGbNoWait) { gspWaitForVBlank(); return; }
    // en juego: la pantalla de abajo tiene dos paginas; la otra se deja igual a la que se acaba de mostrar, para que
    // la animacion de los minis pueda redibujar solo su pedazo en cualquiera de las dos
    u16 w = 0, h = 0;
    u8* back = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
    if (back && back != sGbFb && w == 240 && h == 320) memcpy(back, sGbFb, 240 * 320 * 3);
}
// tablero de esquinas redondas (suavizadas) con borde; alpha: transparencia del relleno sobre el fondo
static void gb_panel(int x0, int y0, int w, int h, u32 fill, u32 border, int bw = 1, int alpha = 255, int r = 7) {
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        const int dx = x < w - 1 - x ? x : w - 1 - x, dy = y < h - 1 - y ? y : h - 1 - y;
        if (dx >= r || dy >= r) { // fuera de las esquinas no hay nada que suavizar (casi todos los puntos)
            if (dx >= bw && dy >= bw) gb_blend(x0 + x, y0 + y, fill, alpha); else gb_blend(x0 + x, y0 + y, border, 255);
            continue;
        }
        float cov = 1.0f, in = (dx >= bw && dy >= bw) ? 1.0f : 0.0f;
        if (dx < r && dy < r) {
            const float d = sqrtf((r - 0.5f - dx) * (r - 0.5f - dx) + (r - 0.5f - dy) * (r - 0.5f - dy));
            cov = r - d + 0.5f;
            in = (r - bw) - d + 0.5f;
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            if (in < 0.0f) in = 0.0f;
            if (in > 1.0f) in = 1.0f;
        }
        if (in > 0.0f) gb_blend(x0 + x, y0 + y, fill, (int)(alpha * cov * in));
        if (in < 1.0f) gb_blend(x0 + x, y0 + y, border, (int)(255 * cov * (1.0f - in)));
    }
}
static void gb_disc(int cx, int cy, float r, u32 rgb) {
    for (int y = -(int)r - 1; y <= (int)r + 1; y++) for (int x = -(int)r - 1; x <= (int)r + 1; x++) {
        const float c = r - sqrtf((float)(x * x + y * y)) + 0.5f;
        if (c > 0.0f) gb_blend(cx + x, cy + y, rgb, (int)(255 * (c > 1.0f ? 1.0f : c)));
    }
}
// fondo: degradado azul con bandas diagonales suaves, y la barra del titulo
static void gb_bg(const char* right) {
    static u8 sRow[240][3], sTri[112], sOff[240]; // color de cada renglon, la onda triangular y su corrimiento
    static bool sInit;
    if (!sInit) {
        sInit = true;
        for (int y = 0; y < 240; y++) { sRow[y][0] = (u8)(96 + y * 96 / 239); sRow[y][1] = (u8)(20 + y * 64 / 239); sRow[y][2] = (u8)(10 + y * 22 / 239); sOff[y] = (u8)((y * 3 / 2) % 112); }
        for (int t = 0; t < 112; t++) sTri[t] = (u8)((t < 56 ? t : 111 - t) * 14 / 55);
    }
    for (int x = 0; x < 320; x++) { // el buffer va por columnas: y = 0 es el ultimo punto de cada una
        u8* p = sGbFb + (x * 240 + 239) * 3;
        const int xm = x % 112;
        for (int y = 0; y < 240; y++, p -= 3) {
            int t = xm + sOff[y];
            if (t >= 112) t -= 112;
            const int s = sTri[t];
            p[0] = (u8)(sRow[y][0] + s); p[1] = (u8)(sRow[y][1] + s); p[2] = (u8)(sRow[y][2] + s);
        }
    }
    for (int y = 0; y < 26; y++) for (int x = 0; x < 320; x++) gb_blend(x, y, 0x060C40, 215);
    for (int x = 0; x < 320; x++) { gb_blend(x, 26, 0xFFD020, 255); gb_blend(x, 27, 0xFFD020, 120); }
    gb_label(9, 2, "POKEMON STADIUM", 0xFFD830, kUiFontLg);
    gb_label(311 - gb_tw(right), 7, right, 0xC8D8FF);
}
// cambia el titulo de la barra (se llama despues de gb_bg)
static void gb_rect_title(const char* title) {
    for (int y = 0; y < 26; y++) for (int x = 0; x < 200; x++) gb_blend(x, y, 0x070D42, 255);
    gb_label(9, 2, title, 0xFFD830, kUiFontLg);
}
// boton: tablero con la tecla en un circulo de color y el texto
static void gb_button(int x, int y, int w, int h, const char* key, u32 keyColor, const char* text, u32 fill, u32 border) {
    gb_panel(x, y, w, h, fill, border, 1, 235);
    const bool wide = key[1] != 0;
    const int kw = wide ? gb_tw(key) + 12 : 18, total = kw + 7 + gb_tw(text), sx = x + (w - total) / 2, cy = y + h / 2;
    if (wide) gb_panel(sx, cy - 9, kw, 18, keyColor, 0xFFFFFF, 1, 255, 8);
    else { gb_disc(sx + 9, cy, 9.0f, 0xFFFFFF); gb_disc(sx + 9, cy, 8.0f, keyColor); }
    gb_text(sx + (kw - gb_tw(key)) / 2, cy - 7, key, 0xFFFFFF);
    gb_label(sx + kw + 7, cy - 7, text, 0xFFFFFF);
}
// cartucho: 0-3 version, 4 sin version, 5 vacio
static void gb_cart(int x0, int y0, int version, bool empty) {
    const u8* s = kUiCart[empty ? 5 : (version >= 0 && version < 4 ? version : 4)];
    for (int y = 0; y < GB_CART_H; y++) for (int x = 0; x < GB_CART_W; x++, s += 4)
        gb_blend(x0 + x, y0 + y, (u32)(s[0] << 16 | s[1] << 8 | s[2]), empty ? s[3] * 110 / 255 : s[3]);
    if (empty || version < 0 || version > 2 || !sGbLblOn[version]) return;
    const u8* p = sGbLbl[version]; // la etiqueta del usuario, con las esquinas redondeadas
    for (int y = 0; y < GB_LBL_H; y++) for (int x = 0; x < GB_LBL_W; x++, p += 3) {
        const int dx = x < GB_LBL_W - 1 - x ? x : GB_LBL_W - 1 - x, dy = y < GB_LBL_H - 1 - y ? y : GB_LBL_H - 1 - y;
        if (dx + dy < 2) continue;
        gb_blend(x0 + GB_LBL_X + x, y0 + GB_LBL_Y + y, (u32)(p[0] << 16 | p[1] << 8 | p[2]), dx + dy == 2 ? 128 : 255);
    }
}
// el nombre que se muestra (por dentro la Consola Virtual conserva su nombre: de el salen los de las copias)
static const char* gb_shown_name(const GbEntry& e) {
    static char t[64];
    if (!e.vcLow) return e.name;
    snprintf(t, sizeof t, "Virtual Console%s", e.name + strlen("Consola Virtual"));
    return t;
}
// un Transfer Pak con su cartucho (o vacio)
#define GB_CARD_X(s) (8 + (s) * 156)
#define GB_CARD_Y 31
#define GB_CARD_W 148
#define GB_CARD_H 124
static void gb_card(int slot, const GbEntry* e, bool selected) {
    const int x = GB_CARD_X(slot), y = GB_CARD_Y, cx = x + GB_CARD_W / 2;
    gb_panel(x, y, GB_CARD_W, GB_CARD_H, selected ? 0x1C2C90 : 0x101A66, selected ? 0xFFD020 : 0x6088F0, selected ? 2 : 1, 225);
    char t[80];
    snprintf(t, sizeof t, "TRANSFER PAK %d", slot + 1);
    gb_label(cx, y + 4, t, selected ? 0xFFD830 : 0xC8D8FF, kUiFontSm, true);
    gb_cart(cx - GB_CART_W / 2, y + 15, e ? e->version : -1, !e);
    if (!e) {
        gb_label(cx, y + 89, "No Game Pak", 0xFFFFFF, kUiFontSm, true);
        gb_label(cx, y + 102, slot ? "Controller 2" : "Controller 1", 0x90A8F0, kUiFontSm, true);
        return;
    }
    if (e->version >= 0) snprintf(t, sizeof t, "Pokemon %s", kGbVerNameEn[e->version]); else snprintf(t, sizeof t, "Version not set");
    gb_label(cx, y + 84, t, 0xFFFFFF, kUiFontSm, true);
    gb_fit(t, sizeof t, gb_shown_name(*e), GB_CARD_W - 10);
    gb_label(cx, y + 95, t, 0xC8D8FF, kUiFontSm, true);
    gb_label(cx, y + 106, e->vcLow ? "Virtual Console save" : e->romHeader ? "Save .sav + ROM .gb" : "Save file on SD", 0x80E8A0, kUiFontSm, true);
}
// la pantalla principal. left >= 0: segundos para empezar solo; ingame: tablero fijo mientras se juega
#define GB_FMT_Y 159
static bool sGbRoamHint; // hay Pokemon para la pantalla libre: se anuncia la tecla
#define GB_BTN_Y 188
static void gb_draw_main(const GbEntry* list, const int* pick, int sel, bool aspect43, int left, bool ingame) {
    if (!gb_screen_begin()) return;
    gb_bg("Transfer Pak");
    for (int s = 0; s < GB_SLOTS; s++) gb_card(s, pick[s] >= 0 ? &list[pick[s]] : nullptr, !ingame && s == sel);
    const char* fmt = aspect43 ? "Display: 4:3 (original)" : "Display: full screen";
    if (ingame) {
        gb_panel(8, GB_FMT_Y, 304, 74, 0x101A66, 0x6088F0, 1, 225);
        gb_label(160, GB_FMT_Y + 9, fmt, 0xFFFFFF, kUiFontSm, true);
        gb_label(160, GB_FMT_Y + 29, sGbRoamHint ? "L: your Pokemon" : "Playing on the top screen", 0xC8D8FF, kUiFontSm, true);
        gb_label(160, GB_FMT_Y + 49, "START + SELECT: quit", 0xFFD830, kUiFontSm, true);
    } else {
        gb_button(8, GB_FMT_Y, 304, 25, "Y", 0x20A050, fmt, 0x1C2C90, 0x6088F0);
        gb_button(8, GB_BTN_Y, 148, 46, "X", 0x2C60E0, "Change Game Pak", 0x1C2C90, 0x6088F0);
        char t[24];
        if (left >= 0) snprintf(t, sizeof t, "Play (%d)", left); else snprintf(t, sizeof t, "Play");
        gb_button(164, GB_BTN_Y, 148, 46, "START", 0x0C7028, t, 0x189040, 0xA8F4C0);
    }
    gb_screen_end();
}
// Lista en un tablero encima del fondo. Regresa el indice elegido o -1 (B o "Volver"). Cruz o tocar un renglon.
static int gb_list(const char* title, const char* const* items, const int* versions, int count, int def) {
    const int rowH = 18, rows = 9, y0 = 52;
    int cur = def < 0 || def >= count ? 0 : def, top = 0;
    bool dirty = true;
    while (aptMainLoop()) {
        hidScanInput();
        const u32 k = hidKeysDown();
        if (k & (KEY_DUP | KEY_CPAD_UP)) { cur = (cur + count - 1) % count; dirty = true; }
        if (k & (KEY_DDOWN | KEY_CPAD_DOWN)) { cur = (cur + 1) % count; dirty = true; }
        if (k & KEY_A) return cur;
        if (k & KEY_B) return -1;
        if (k & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            if (tp.py >= 214) return tp.px < 160 ? cur : -1;               // botones "Elegir" y "Volver"
            if (tp.py >= y0 && tp.px >= 12 && tp.px < 308) {
                const int i = top + (tp.py - y0) / rowH;
                if (i < count && (tp.py - y0) / rowH < rows) return i;
            }
        }
        if (cur < top) top = cur;
        if (cur >= top + rows) top = cur - rows + 1;
        if (!dirty) { gspWaitForVBlank(); continue; }
        dirty = false;
        if (!gb_screen_begin()) return cur; // sin pantalla de abajo: lo que estaba elegido
        gb_bg("Transfer Pak");
        gb_panel(8, 31, 304, 205, 0x101A66, 0x6088F0, 1, 225);
        gb_label(18, 35, title, 0xFFD830);
        for (int r = 0; r < rows && top + r < count; r++) {
            const int i = top + r, y = y0 + r * rowH;
            if (i == cur) gb_panel(12, y, 296, rowH - 1, 0x2C48C8, 0xFFD020, 1, 255, 6);
            if (versions && versions[i] >= 0) { gb_panel(18, y + 3, 12, 12, kGbVerColor[versions[i]], 0xFFFFFF, 1, 255, 3); }
            char t[80];
            gb_fit(t, sizeof t, items[i], 262);
            gb_label(38, y + 2, t, i == cur ? 0xFFFFFF : 0xD0DCFF);
        }
        if (top > 0) gb_text(298, y0 - 14, "^", 0xFFD830);
        if (top + rows < count) gb_text(298, y0 + rows * rowH - 16, "v", 0xFFD830);
        gb_button(12, 215, 144, 19, "A", 0xD03030, "Select", 0x1C2C90, 0x6088F0);
        gb_button(164, 215, 144, 19, "B", 0xC89018, "Back", 0x1C2C90, 0x6088F0);
        gb_screen_end();
    }
    return -1;
}

static bool gb_load(int slot, const GbEntry& e) {
    GbSlot& g = sGb[slot];
    g.e = e;
    if (e.vcLow) {
        const Result rc = gb_vc_mount(e.vcLow, kGbVcDev[slot]);
        if (R_FAILED(rc)) { logf("[gb] no pude abrir la partida de la Consola Virtual (%08lx)\n", (unsigned long)rc); return false; }
        g.vcMounted = true;
    }
    u8* buf = nullptr;
    long size = GB_RAM_SIZE;
    {
        FILE* f = fopen(gb_file_path(slot), "rb");
        if (!f) { logf("[gb] no pude abrir %s\n", e.path); return false; }
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size < (long)GB_RAM_SIZE || size > (long)GB_MAX_FILE) { fclose(f); logf("[gb] %s no mide lo de una partida (%ld bytes)\n", e.path, size); return false; }
        buf = (u8*)malloc((size_t)size);
        if (!buf) { fclose(f); logf("[gb] sin memoria para la partida\n"); return false; }
        const size_t got = fread(buf, 1, (size_t)size, f);
        fclose(f);
        if (got != (size_t)size) { free(buf); logf("[gb] lectura incompleta de %s\n", e.path); return false; }
    }
    if (!g.e.romHeader) gb_make_header(g.e.header, e.version);
    if (e.vcLow) { g.orig = (u8*)malloc((size_t)size); if (g.orig) memcpy(g.orig, buf, (size_t)size); }
    g.file = buf;
    g.fileSize = (u32)size;
    g.present = true;
    {   // la suma de la partida, solo para avisar en el registro si el archivo no parece de Pokemon (no se rechaza)
        u8 sum = 0;
        for (u32 i = 0x2598; i < 0x3523; i++) sum = (u8)(sum + buf[i]);
        const bool ok = (u8)~sum == buf[0x3523];
        logf("[gb] control %d: %s (%s%s), %lu bytes, suma de la partida %s\n", slot + 1, e.name, kGbVerName[e.version],
             e.romHeader ? ", cabecera de la ROM" : "", (unsigned long)g.fileSize,
             ok ? "correcta" : "NO coincide (archivo danado o de otro juego?)");
    }
    return true;
}

static GbEntry sGbUiList[GB_MAX_LIST]; // lo que muestra el tablero fijo (para volver a dibujarlo)
static int sGbUiPick[GB_SLOTS];
static bool sGbUiAspect, sGbUiOn;
// Al arrancar (ya con la pantalla lista): la pantalla de los Transfer Pak.
static u32 sGbBootHeld; // botones presionados al arrancar, antes de esta pantalla (L, R, X cambian rutas del host)
// ---- logo del autor del port en la pantalla de arriba, mientras abajo se eligen los cartuchos ----
// Fondo negro. Se pinta directo en las dos paginas de la pantalla (aun no corre ningun cuadro del juego) y se queda
// ahi hasta que termina la pantalla de los cartuchos; entonces se deja en negro (show = false).
#include "splash_logo.inc"
static void gb_top_logo(bool show) {
    int w = 0, h = 0, comp = 0;
    u8* img = show ? stbi_load_from_memory(kSplashJpg, (int)sizeof kSplashJpg, &w, &h, &comp, 3) : nullptr;
    if (show && (!img || w != 240 || h != 240)) { if (img) stbi_image_free(img); return; }
    const GSPGPU_FramebufferFormat fmt = gfxGetScreenFormat(GFX_TOP);
    const int bpp = fmt == GSP_RGBA8_OES ? 4 : fmt == GSP_BGR8_OES ? 3 : fmt == GSP_RGB565_OES ? 2 : 0;
    for (int page = 0; bpp && page < 2; page++) { // las dos paginas
        for (int side = 0; side < 2; side++) {
            if (side == 1 && !gfxIs3D()) break;
            u16 fw = 0, fh = 0;
            u8* fb = gfxGetFramebuffer(GFX_TOP, side ? GFX_RIGHT : GFX_LEFT, &fw, &fh);
            if (!fb || fw != 240) continue;
            const int cols = fh, step = fh >= 800 ? 2 : 1; // 400 columnas (800 en modo ancho)
            for (int x = 0; x < cols; x++) {
                const int ix = x / step - 80; // el logo, de 240, al centro de los 400
                u8* p = fb + (size_t)x * 240 * bpp;
                for (int y = 239; y >= 0; y--, p += bpp) { // cada columna va de abajo hacia arriba
                    int r = 0, g = 0, b = 0;
                    if (img && ix >= 0 && ix < 240) { const u8* q = img + (y * 240 + ix) * 3; r = q[0]; g = q[1]; b = q[2]; }
                    if (bpp == 3) { p[0] = (u8)b; p[1] = (u8)g; p[2] = (u8)r; }
                    else if (bpp == 4) { p[0] = 255; p[1] = (u8)b; p[2] = (u8)g; p[3] = (u8)r; }
                    else { const u16 v = (u16)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); p[0] = (u8)v; p[1] = (u8)(v >> 8); }
                }
            }
        }
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_TOP, gfxIs3D());
        gspWaitForVBlank();
    }
    if (img) stbi_image_free(img);
}
static bool sGbTopLogo;
static void gb_boot_menu(void) {
    hidScanInput();
    sGbBootHeld = hidKeysHeld();
    {   // el logo no sale en las pruebas del emulador que se saltan esta pantalla (sinmenu.txt, gbauto.txt): asi no les cambia el tiempo
        const u32 au = host_flag("gbauto");
        if (!((au != 0xFFFFFFFFu && au != 0) || host_flag("sinmenu") == 1)) { gb_top_logo(true); sGbTopLogo = true; }
    }
    mkdir(GB_DIR, 0777);
    gb_lbl_import();
    gb_lbl_load();
    GbEntry* const list = sGbUiList;
    static GbCfg cfg;
    gb_cfg_load(&cfg);
    int n = gb_scan_all(list, &cfg);
    int pick[GB_SLOTS];
    for (int s = 0; s < GB_SLOTS; s++) {
        pick[s] = -1;
        for (int i = 0; i < n; i++) if (cfg.last[s][0] && !strcmp(cfg.last[s], list[i].path)) pick[s] = i;
    }
    if (pick[1] >= 0 && pick[1] == pick[0]) pick[1] = -1;
    bool aspect43 = cfg.aspect43 != 0;

    // Solo para las pruebas en el emulador: gbauto.txt = 1 pone el primer cartucho en el Transfer Pak 1 (2: tambien el
    // segundo en el 2) y sinmenu.txt = 1 salta esta pantalla (queda lo recordado)
    const u32 autoPick = host_flag("gbauto");
    const bool noMenu = (autoPick != 0xFFFFFFFFu && autoPick != 0) || host_flag("sinmenu") == 1;
    if (autoPick == 1 || autoPick == 2) {
        pick[0] = n > 0 ? 0 : -1;
        pick[1] = autoPick == 2 && n > 1 ? 1 : -1;
        for (int s = 0; s < GB_SLOTS; s++) if (pick[s] >= 0 && list[pick[s]].version == GB_VER_NONE) list[pick[s]].version = GB_VER_RED;
    }
    if (!noMenu) {
        int sel = 0, shown = -2;
        bool dirty = true, counting = cfg.has, armed = false;
        const u64 t0 = svcGetSystemTick();
        const u32 kAny = KEY_A | KEY_B | KEY_X | KEY_Y | KEY_START | KEY_SELECT | KEY_L | KEY_R | KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_TOUCH;
        while (aptMainLoop()) {
            hidScanInput();
            u32 k = hidKeysDown();
            // los botones con que se lanzo el programa no cuentan: primero hay que soltarlos
            if (!armed) { if (!(hidKeysHeld() & kAny)) armed = true; k = 0; }
            int open = -1; // Transfer Pak cuyo cartucho se va a elegir
            if (k & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                k &= ~(u32)(KEY_A | KEY_X | KEY_Y | KEY_START);
                if (tp.py >= GB_CARD_Y && tp.py < GB_CARD_Y + GB_CARD_H) {
                    for (int s = 0; s < GB_SLOTS; s++) if (tp.px >= GB_CARD_X(s) && tp.px < GB_CARD_X(s) + GB_CARD_W) { sel = s; open = s; }
                } else if (tp.py >= GB_FMT_Y && tp.py < GB_FMT_Y + 26) k |= KEY_Y;
                else if (tp.py >= GB_BTN_Y && tp.py < GB_BTN_Y + 44) k |= tp.px < 160 ? KEY_X : KEY_START;
                dirty = true;
            }
            if (k & kAny) { if (counting) dirty = true; counting = false; }
            if (k & (KEY_DLEFT | KEY_DRIGHT | KEY_L | KEY_R)) { sel ^= 1; dirty = true; }
            if (k & KEY_A) open = sel;
            if (k & KEY_Y) { aspect43 = !aspect43; dirty = true; }
            if (k & KEY_X) { // cambiar cartucho: se vuelve a buscar (lo elegido se conserva si sigue ahi) y se abre la lista
                char keep[GB_SLOTS][200];
                for (int s = 0; s < GB_SLOTS; s++) snprintf(keep[s], sizeof keep[s], "%s", pick[s] >= 0 ? list[pick[s]].path : "");
                n = gb_scan_all(list, &cfg);
                for (int s = 0; s < GB_SLOTS; s++) {
                    pick[s] = -1;
                    for (int i = 0; i < n; i++) if (keep[s][0] && !strcmp(keep[s], list[i].path)) pick[s] = i;
                }
                logf("[gb] busqueda de cartuchos: %d\n", n);
                dirty = true;
                open = sel; // y se elige el del Transfer Pak marcado
            }
            if (open >= 0) {
                static char labels[GB_MAX_LIST + 1][52];
                const char* items[GB_MAX_LIST + 1];
                int vers[GB_MAX_LIST + 1];
                for (int i = 0; i < n; i++) {
                    if (list[i].version != GB_VER_NONE) snprintf(labels[i], sizeof labels[i], "%.30s  (%s)", gb_shown_name(list[i]), kGbVerNameEn[list[i].version]);
                    else snprintf(labels[i], sizeof labels[i], "%.42s", gb_shown_name(list[i]));
                    items[i] = labels[i];
                    vers[i] = list[i].version;
                }
                snprintf(labels[n], sizeof labels[n], "No Game Pak");
                items[n] = labels[n];
                vers[n] = -1;
                char title[48];
                snprintf(title, sizeof title, "Game Pak for Transfer Pak %d:", open + 1);
                int r = gb_list(title, items, vers, n + 1, pick[open] >= 0 ? pick[open] : n);
                if (r >= 0 && r < n && list[r].version == GB_VER_NONE) { // un .sav sin nombre de version: preguntar
                    static const char* const vq[3] = { "Pokemon Red", "Pokemon Blue", "Pokemon Yellow" };
                    static const int vv[3] = { GB_VER_RED, GB_VER_BLUE, GB_VER_YELLOW };
                    const int v = gb_list("Which version is this save?", vq, vv, 3, 0);
                    if (v >= 0) { list[r].version = v; gb_cfg_set_ver(&cfg, list[r].path, v); } else r = -1;
                }
                if (r == n) pick[open] = -1;
                else if (r >= 0) { pick[open] = r; if (pick[open ^ 1] == r) pick[open ^ 1] = -1; } // un cartucho no va en los dos
                dirty = true;
            }
            if (k & KEY_START) break;
            int left = -1;
            if (counting) {
                const u32 ms = (u32)((svcGetSystemTick() - t0) * 1000ull / 268111856ull);
                if (ms >= 5000u) break;
                left = 5 - (int)(ms / 1000u);
            }
            if (!dirty && left == shown) { gspWaitForVBlank(); continue; }
            dirty = false;
            shown = left;
            gb_draw_main(list, pick, sel, aspect43, left, false);
        }
    }

    for (int s = 0; s < GB_SLOTS; s++) {
        if (pick[s] >= 0 && !gb_load(s, list[pick[s]])) {
            if (sGb[s].vcMounted) { archiveUnmount(kGbVcDev[s]); sGb[s].vcMounted = false; }
            pick[s] = -1;
        }
        if (pick[s] < 0) logf("[gb] Transfer Pak %d sin cartucho\n", s + 1);
        snprintf(cfg.last[s], sizeof cfg.last[s], "%s", pick[s] >= 0 ? list[pick[s]].path : "-");
    }
    cfg.aspect43 = aspect43;
    gPsAspect43_3DS = aspect43;
    logf("[gb] formato de imagen: %s\n", aspect43 ? "4:3" : "pantalla completa");
    if (!noMenu) {
        gb_cfg_save(&cfg);
        for (int i = 0; i < 2; i++) gb_draw_main(list, pick, -1, aspect43, -1, true); // el tablero fijo, en los dos buffers
    }
    for (int s = 0; s < GB_SLOTS; s++) sGbUiPick[s] = pick[s];
    sGbUiAspect = aspect43;
    sGbUiOn = host_flag("sintablero") != 1;
    sMiniAltTest = (u8)host_flag("minialt"); // el tablero de batalla (sintablero.txt lo apaga para comparar)
}

// ---- tablero de batalla (pantalla de abajo mientras hay batalla) ----
// El juego pone gPsBatOn mientras corre la batalla (fragment62_2EA8E0.c). De D_84390010[lado] se lee el Pokemon
// activo (README, "Pantalla de batalla"). Solo se redibuja cuando cambia algo de lo que se muestra.
#include "moves_gen1.inc"
#if defined(BLOB_BAT_ON) && defined(BLOB_BATTLE_MON)
struct GbBat {
    u32 menu; u8 species[2]; u16 hp[2], maxhp[2]; u8 move[4], pp[4];
    // el equipo de cada lado (D_84390010[lado]->unk_724: unk_002 cuantos, unk_01C[] de 0x54: especie, +2 vida, +0x26 vida maxima)
    u8 cnt[2], act[2], tsp[2][6]; u16 thp[2][6], tmax[2][6];
    // el Pikachu de Yellow: el juego lo marca al cargarlo del cartucho (unk_52 & 0x80, func_82304BEC); usa su propio mini
    u8 talt[2][6], alt[2];
    u8 seen[6], nseen, view; // del rival: indices en el orden en que han salido; view: 0 ataques, 1 equipos
    u8 press; // 0 nada, 1 boton tocado, 2 tocado pero no se puede (solo para que se redibuje)
    u8 ready; // el juego esta esperando la orden del jugador (si no, los botones salen apagados y no se manda nada)
    u8 runq;  // el juego pregunta si de verdad se quiere huir (START): abajo van Yes y No
    u8 out[2]; // el Pokemon de cada lado ya esta a la vista (si no, va la base de transicion)
};
// ---- el menu del juego para el jugador 1, tal como esta en este momento ----
// unk_10: 0 menu principal, 1 ataques, 2 cambio, 3 orden ya dada, 4 pregunta de huir. unk_8C4.unk_00: hay un aviso
// en pantalla (se quita con cualquier boton). gPsBatTick avanza en cada cuadro en que el juego atiende el menu
// principal o el de ataques: si avanzo hace poco, el juego esta esperando la orden.
struct GbBatSt { bool ok, fresh, fainted; u32 u10, u04, msg; u8 u2F; };
static u32 sStTickVal;
static u64 sStTickAt;
static GbBatSt gb_bat_state(void) {
    GbBatSt st = {};
    const u32 p = BLOB_BATTLE_MON ? intro_be(BLOB_BATTLE_MON) : 0;
    if (!BLOB_BAT_ON || intro_be(BLOB_BAT_ON) == 0 || p < BLOB_BASE || p >= BLOB_BASE + BLOB_SIZE - 0x950) return st;
    const u64 now = svcGetSystemTick();
    const u32 t = intro_be(BLOB_BAT_TICK);
    if (t != sStTickVal) { sStTickVal = t; sStTickAt = now; }
    st.ok = true;
    st.fresh = sStTickAt != 0 && now - sStTickAt < 268111856ull * 6 / 10;
    st.u04 = intro_be(p + 0x654 + 4);
    st.u10 = intro_be(p + 0x654 + 0x10);
    st.u2F = *(const u8*)(p + 0x654 + 0x2F);
    st.msg = intro_be(p + 0x8C4);
    const u8* m = (const u8*)(p + 0x654 + 0x38);
    st.fainted = m[0x0C] == 0 && m[0x0D] == 0;
    return st;
}
static bool gb_bat_ready(const GbBatSt& st) { return st.ok && st.u10 != 3 && (st.u10 == 2 || st.u10 == 4 || st.fresh); }
// quien esta a la vista: al empezar y tras debilitarse uno, hasta que el juego vuelve a pedir la orden (ya salieron
// los dos); en un cambio a media ronda, 2.5 s despues del cambio (o al pedir la orden, lo que pase primero)
static u8 sOut[2];
static s8 sOutAct[2] = { -1, -1 };
static bool sOutWaitMenu[2];
static u64 sOutAt[2];
// animacion del VS: al que le baja la vida lo empujan y cae de espaldas mientras el otro se le va encima, y ahi se queda
// tirado hasta que le toca atacar (gPsAtkStart: se levanta con un brinco justo antes) o hasta que el juego vuelve a
// pedir la orden. El que arranca un movimiento estando de pie da un brinco de impulso.
static u64 sHitAt[2], sLungeAt[2], sUpAt[2], sAtkAt[2];
static bool sDown[2], sAtkInit;
static u32 sAtkSeen[2];
static void gb_anim_attack(int k, u64 now) {
    logf("[tablero] t=%lu ms: ataca el lado %d%s\n", (unsigned long)(now / (268111856ull / 1000)), k, sDown[k] ? " (se levanta antes)" : "");
    sDown[k] = false; // si estaba tirado se levanta; de pie, toma impulso: en los dos casos es un brinco
    sUpAt[k] = now;
}
static void gb_anim_hit(int k, u64 now) { sHitAt[k] = now; sDown[k] = true; if (sOut[1 - k]) sLungeAt[1 - k] = now; /* el otro se le va encima en ese momento */ logf("[tablero] t=%lu ms: cae el lado %d\n", (unsigned long)(now / (268111856ull / 1000)), k); }
static void gb_bat_out_reset(void) { sOut[0] = sOut[1] = 0; sOutAct[0] = sOutAct[1] = -1; sStTickAt = 0; sHitAt[0] = sHitAt[1] = sLungeAt[0] = sLungeAt[1] = sUpAt[0] = sUpAt[1] = sAtkAt[0] = sAtkAt[1] = 0; sDown[0] = sDown[1] = false; sAtkInit = false; }
static u8 sBatSeen[6], sBatNSeen, sBatView;
// lo ultimo que se toco: se ilumina 0.25 s para que se note el toque
static int sPressX, sPressY, sPressW, sPressH;
static bool sPressBad;
static u64 sPressUntil;
static u32 sBatTouches;
static bool gb_bat_read(GbBat* b) {
    memset(b, 0, sizeof *b);
    for (int k = 0; k < 2; k++) {
        const u32 p = intro_be(BLOB_BATTLE_MON + 4 * k);
        if (p < BLOB_BASE || p >= BLOB_BASE + BLOB_SIZE - 0x950) return false;
        const u8* m = (const u8*)(p + 0x654 + 0x38);
        if (k == 0) b->menu = intro_be(p + 0x654 + 4);
        b->species[k] = m[0x0B];
        b->hp[k] = (u16)(m[0x0C] << 8 | m[0x0D]);
        b->maxhp[k] = (u16)(m[0x28] << 8 | m[0x29]);
        if (k == 0) for (int i = 0; i < 4; i++) { b->move[i] = m[0x1F + i]; b->pp[i] = m[0x32 + i] & 0x3F; }
        if (b->species[k] == 0 || b->species[k] > 151 || b->maxhp[k] == 0) return false;
        const u32 t = intro_be(p + 0x724);
        if (t < BLOB_BASE || t >= BLOB_BASE + BLOB_SIZE - 0x220) return false;
        const u8* tm = (const u8*)t;
        b->cnt[k] = tm[2] > 6 ? 6 : tm[2];
        const u32 act = intro_be(p + 0x654 + 8);
        b->act[k] = act < 6 ? (u8)act : 0;
        for (int j = 0; j < 6; j++) {
            const u8* m2 = tm + 0x1C + j * 0x54;
            b->tsp[k][j] = m2[0];
            b->thp[k][j] = (u16)(m2[2] << 8 | m2[3]);
            b->tmax[k][j] = (u16)(m2[0x26] << 8 | m2[0x27]);
            b->talt[k][j] = (m2[0] == 25 && (m2[0x52] & 0x80)) || (sMiniAltTest && m2[0] == sMiniAltTest);
        }
        // Quien esta en el campo es el lugar activo del equipo (unk_08), no el dato del que pelea (unk_38): ese cambia
        // desde que se ELIGE al siguiente (antes de que salga), mientras que el lugar cambia cuando de verdad entra. Asi no
        // se adelanta el Pokemon elegido (ni el del rival). La vida del equipo ademas baja junto con la barra que se ve;
        // la del que pelea ya trae el resultado desde antes de la animacion. Los ataques si salen del que pelea.
        {
            const int a = b->act[k];
            if (k == 0) { // los ataques son del que pelea: mientras el elegido aun no entra, se dejan los del que sigue en el campo
                static u8 sKeepMove[4], sKeepPp[4];
                if (b->tsp[k][a] == b->species[k]) { memcpy(sKeepMove, b->move, 4); memcpy(sKeepPp, b->pp, 4); }
                else if (sKeepMove[0]) { memcpy(b->move, sKeepMove, 4); memcpy(b->pp, sKeepPp, 4); }
            }
            if (b->tsp[k][a] >= 1 && b->tsp[k][a] <= 151) { b->species[k] = b->tsp[k][a]; b->hp[k] = b->thp[k][a]; b->maxhp[k] = b->tmax[k][a]; b->alt[k] = b->talt[k][a]; }
        }
        {   // le bajo la vida (la del equipo baja junto con la barra que se ve): una vez por golpe
            static u16 sRawHp[2];
            static u8 sRawSp[2];
            static s8 sRawAct[2] = { -1, -1 };
            const u64 nowT = svcGetSystemTick();
            static u64 sDropAt[2]; // la ultima vez que se vio bajar: mientras la barra se vacia baja en cada lectura
            const bool drop = sRawAct[k] == (s8)b->act[k] && sRawSp[k] == b->species[k] && b->hp[k] < sRawHp[k];
            const bool first = drop && (sDropAt[k] == 0 || nowT - sDropAt[k] > 268111856ull * 6 / 10);
            if (drop) sDropAt[k] = nowT;
            if (first && sOut[k] && b->hp[k] > 0) gb_anim_hit(k, nowT);
            if (sRawAct[k] != (s8)b->act[k] || b->hp[k] == 0) sDown[k] = false; // otro Pokemon, o ya se debilito
            {   // arranco un movimiento (una vez por movimiento: el contador puede subir varias veces seguidas)
                const u32 c = intro_be(BLOB_ATK_START + 4 * (u32)k);
                if (!sAtkInit) sAtkSeen[k] = c;
                if (c != sAtkSeen[k]) {
                    sAtkSeen[k] = c;
                    if (sOut[k] && b->hp[k] > 0 && (sAtkAt[k] == 0 || nowT - sAtkAt[k] > 268111856ull)) { sAtkAt[k] = nowT; gb_anim_attack(k, nowT); }
                }
                if (k == 1) sAtkInit = true;
                if (k == 1) {   // la barra de vida del HUD se pidio con mas vida que la maxima (el juego ya la recorta)
                    static u32 sHpBarSeen, sHpBarLogs;
                    const u32 n = intro_be(BLOB_HPBAR);
                    if (n != sHpBarSeen) {
                        if (sHpBarLogs++ < 12) logf("[hud] barra de vida fuera de rango (vida %ld de %ld), %lu veces: se dibuja recortada a su ancho\n",
                                                   (long)(s32)intro_be(BLOB_HPBAR + 4), (long)(s32)intro_be(BLOB_HPBAR + 8), (unsigned long)n);
                        sHpBarSeen = n;
                    }
                }
            }
            // tirado: se levanta solo cuando el juego vuelve a pedir la orden (o a los 12 s, por si acaso)
            if (sDown[k] && ((sStTickAt != 0 && nowT - sStTickAt < 268111856ull * 6 / 10 && nowT - sHitAt[k] > 268111856ull) || nowT - sHitAt[k] > 268111856ull * 12)) { sDown[k] = false; sUpAt[k] = nowT; logf("[tablero] t=%lu ms: se levanta el lado %d (el juego ya pide la orden)\n", (unsigned long)(nowT / (268111856ull / 1000)), k); }
            sRawHp[k] = b->hp[k]; sRawSp[k] = b->species[k]; sRawAct[k] = (s8)b->act[k];
        }
        {   // a la vista o todavia no: al empezar, hasta que el juego pide la orden; al entrar otro (cambia el lugar
            // activo), 2.5 s o hasta que el juego pide la orden, lo que pase primero
            const u64 now = svcGetSystemTick();
            const bool fresh = sStTickAt != 0 && now - sStTickAt < 268111856ull * 6 / 10;
            if (sOutAct[k] != (s8)b->act[k]) { sOutWaitMenu[k] = sOutAct[k] < 0; sOut[k] = 0; sOutAct[k] = (s8)b->act[k]; sOutAt[k] = now; }
            if (!sOut[k] && b->hp[k] > 0 && (fresh || (!sOutWaitMenu[k] && now - sOutAt[k] > 268111856ull * 5 / 2))) sOut[k] = 1;
            b->out[k] = sOut[k];
        }
        // Solo se muestra el nivel de vida (bien, media, poca, debilitado): se guarda asi para no redibujar con cada punto
        for (int j = 0; j <= 6; j++) {
            u16& h = j < 6 ? b->thp[k][j] : b->hp[k];
            u16& mx = j < 6 ? b->tmax[k][j] : b->maxhp[k];
            if (mx == 0) mx = 1;
            h = h == 0 ? 0 : h * 2 > mx ? 100 : h * 5 > mx ? 40 : 10;
            mx = 100;
        }
    }
    // los del rival que ya salieron, en orden
    bool known = !b->out[1]; // el que aun no sale no se muestra tampoco en la pantalla de equipos
    for (int i = 0; i < sBatNSeen; i++) known = known || sBatSeen[i] == b->act[1];
    if (!known && sBatNSeen < 6) sBatSeen[sBatNSeen++] = b->act[1];
    memcpy(b->seen, sBatSeen, 6);
    b->press = 0; // lo tocado ya no obliga a redibujar todo: se ilumina solo su pedazo (gb_press_step)
    b->nseen = sBatNSeen;
    // debilitado: toca elegir en la pantalla de equipos; al entrar el siguiente se regresa a los ataques
    static bool sForced;
    if (b->hp[0] == 0) { sBatView = 1; sForced = true; }
    else if (sForced) { sForced = false; sBatView = 0; }
    b->view = sBatView;
    const GbBatSt st = gb_bat_state();
    b->ready = gb_bat_ready(st);
    b->runq = st.ok && st.u10 == 4;
    return true;
}
static const GbTypeInfo& gb_type_of_move(int mv) {
    const char c = mv >= 1 && mv <= 165 ? kMoveType[mv] : 'N';
    for (const GbTypeInfo& t : kTypeInfo) if (t.code == c) return t;
    return kTypeInfo[0];
}
static u32 gb_shade(u32 c, int pct) {
    u32 o = 0;
    for (int sh = 0; sh < 24; sh += 8) { u32 v = ((c >> sh) & 0xFF) * (u32)pct / 100; if (v > 255) v = 255; o |= v << sh; }
    return o;
}
// dibuja un registro de 32x32. nivel: 0 color completo, 1 algo descolorido, 2 muy descolorido, 3 gris oscuro
// rot: 0 derecho, 1 un cuarto de vuelta (la cabeza a la derecha), 2 un cuarto al otro lado, 3 de cabeza
static void gb_mini_draw(const u8* px, int x0, int y0, int level, int S = 32, bool mirror = false, int rot = 0) {
    static const int kGray[4] = { 0, 110, 190, 256 };
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++, px += 4) {
        if (px[3] < 8) continue;
        int r = px[0], g = px[1], b = px[2];
        if (level) {
            int gr = (r * 77 + g * 150 + b * 29) >> 8;
            if (level == 3) gr = gr * 150 / 256;
            r += (gr - r) * kGray[level] / 256; g += (gr - g) * kGray[level] / 256; b += (gr - b) * kGray[level] / 256;
        }
        const int mx = mirror ? S - 1 - x : x;
        gb_blend(x0 + (rot == 1 ? S - 1 - y : rot == 2 ? y : rot == 3 ? S - 1 - mx : mx), y0 + (rot == 1 ? mx : rot == 2 ? S - 1 - mx : rot == 3 ? S - 1 - y : y), (u32)(r << 16 | g << 8 | b), px[3]);
    }
}
static bool sBatGrayKo; // en la pantalla de equipos el debilitado sale en gris (en el VS, solo la base)
// marcador de un Pokemon (hasta que el usuario ponga sus minis): base redonda y su numero; el color sigue la vida
static int sMiniOx, sMiniOy, sMiniRot; // desplazamiento y giro del mini para la animacion (la base se queda en su lugar)
// hidden: el Pokemon no esta a la vista (no ha salido, lo estan cambiando, se debilito o el rival no lo ha sacado):
// va la base de transicion sola. El mini del jugador va en espejo para que los dos queden frente a frente.
// part: 1 solo lo que no se mueve (la base, o la de transicion), 2 solo el mini, 3 todo
static void gb_bat_mini(int cx, int cy, int species, int hp, int maxhp, bool alt = false, bool rival = false, bool hidden = false, int part = 3) {
    const u8* own = gb_base_get(rival && gb_base_get(1) ? 1 : 0);
    if (hidden || species <= 0 || (hp <= 0 && !sBatGrayKo)) {
        if (const u8* sw = gb_base_get(2)) { if (part & 2) gb_mini_draw(sw, cx - GB_BASE_PX / 2, cy - GB_BASE_PX / 2, 0, GB_BASE_PX, false, sMiniRot); return; }
        if (!(part & 1)) return;
        if (own) gb_mini_draw(own, cx - GB_BASE_PX / 2, cy - GB_BASE_PX / 2, 0, GB_BASE_PX, rival);
        else if (const u8* ball = gb_mini_get(0)) gb_mini_draw(ball, cx - 16, cy - 22, 0);
        else gb_panel(cx - 22, cy + 8, 44, 12, 0x081050, 0x7896F0, 1, 255, 6);
        return;
    }
    // la base va detras y es mas grande que el mini, que queda centrado delante; la del rival, en espejo
    if (!(part & 1)) {}
    else if (own) gb_mini_draw(own, cx - GB_BASE_PX / 2, cy - GB_BASE_PX / 2, 0, GB_BASE_PX, rival);
    else if (const u8* ball = gb_mini_get(0)) gb_mini_draw(ball, cx - 16, cy - 22, 0); // importada con una version anterior
    else gb_panel(cx - 22, cy + 8, 44, 12, 0x081050, 0x7896F0, 1, 255, 6);
    if (!(part & 2)) return;
    const u8* mini = alt ? gb_mini_get(GB_MINI_ALT) : nullptr; // sin mini propio, el Pikachu de Yellow usa el normal
    if (!mini) mini = gb_mini_get(species);
    if (mini) { gb_mini_draw(mini, cx - 16 + sMiniOx, cy - 16 + sMiniOy, hp <= 0 ? 3 : hp * 2 > maxhp ? 0 : hp * 5 > maxhp ? 1 : 2, 32, !rival, sMiniRot); return; }
    const u32 c = hp <= 0 ? 0x78787E : hp * 2 > maxhp ? 0xF8C828 : hp * 5 > maxhp ? 0xBEB078 : 0xB0A0A0;
    gb_panel(cx - 15, cy - 22, 30, 30, c, 0x141840, 1, 255, 8);
    char t[8];
    snprintf(t, sizeof t, "#%d", species);
    gb_text(cx - gb_tw(t) / 2, cy - 14, t, 0x1E1E32);
}
#define GB_TEAM_Y(j) (70 + ((j) % 3) * 42)
// columna de un lugar: con 3 o menos van al centro del tablero; con mas, en dos columnas
#define GB_TEAM_X(c0, n, j) ((n) > 3 ? (c0) - 30 + ((j) / 3) * 60 : (c0))
static void gb_press_draw(void) {
    if (svcGetSystemTick() >= sPressUntil) return;
    gb_panel(sPressX, sPressY, sPressW, sPressH, sPressBad ? 0xFF4040 : 0xFFFFFF, sPressBad ? 0xFF6060 : 0xFFFFFF, 3, sPressBad ? 60 : 90);
}
// ---- animacion de los minis en la franja del VS ----
// Saltan segun la vida (completa: rapido; media: a la mitad; poca: lento) y, al bajarle la vida a uno, ese se hace
// para atras y el otro para adelante. Solo se mueve el mini (la base no). Para no redibujar todo el tablero se guarda
// el fondo de las dos celdas y en cada paso (100 ms a lo mas) se repone el fondo (ya con la base) y se pinta el mini: unos 2 mil
// puntos, contra los 77 mil y las letras de un dibujo completo.
#define GB_VS_W 60
#define GB_VS_H 44
#define GB_VS_Y 32
static const int kVsCx[2] = { 84, 236 };
static u8 sVsBg[2][GB_VS_W * GB_VS_H * 3];
static bool sVsBgOk;
static int sAnimDx[2], sAnimDy[2], sAnimRot[2], sAnimShown[2][3];
static u32 sAnimFrames, sFullDraws;
static u64 sAnimTicks, sFullTicks, sBatShownTicks;
static void gb_vs_copy(bool save) {
    for (int k = 0; k < 2; k++) for (int x = 0; x < GB_VS_W; x++) {
        u8* col = sGbFb + ((kVsCx[k] - GB_VS_W / 2 + x) * 240 + (239 - (GB_VS_Y + GB_VS_H - 1))) * 3;
        u8* keep = sVsBg[k] + x * GB_VS_H * 3;
        if (save) memcpy(keep, col, GB_VS_H * 3); else memcpy(col, keep, GB_VS_H * 3);
    }
    if (save) sVsBgOk = true;
}
static void gb_vs_minis(const GbBat& b, int part) { // part 1: las bases (van en el fondo guardado); 2: los minis
    for (int k = 0; k < 2; k++) {
        if (part == 1) { gb_bat_mini(kVsCx[k], 56, b.species[k], b.hp[k], b.maxhp[k], b.alt[k], k == 1, !b.out[k], 1); continue; }
        sMiniOx = sAnimDx[k]; sMiniOy = sAnimDy[k]; sMiniRot = sAnimRot[k];
        gb_bat_mini(kVsCx[k], 56, b.species[k], b.hp[k], b.maxhp[k], b.alt[k], k == 1, !b.out[k], 2);
        sAnimShown[k][0] = sAnimDx[k]; sAnimShown[k][1] = sAnimDy[k]; sAnimShown[k][2] = sAnimRot[k];
    }
    sMiniOx = sMiniOy = sMiniRot = 0;
}
static void gb_bat_anim_calc(const GbBat& b, u64 now) {
    const u64 kMs = 268111856ull / 1000;
    const u32 tk = (u32)(now / (100 * kMs));
    static const s8 kHop[6] = { 0, -3, -5, -3, 0, 0 };
    for (int k = 0; k < 2; k++) {
        int dx = 0, dy = 0, rot = 0;
        if (b.out[k] && b.hp[k] > 0) {
            const u32 div = b.hp[k] >= 100 ? 1 : b.hp[k] >= 40 ? 2 : 4; // la vida ya viene en niveles: 100, 40, 10
            dy = kHop[(tk / div + (u32)k * 3) % 6];                      // el rival salta a contratiempo
            const int dir = k == 0 ? 1 : -1;                            // hacia el centro
            const u64 q = (u64)tk * 100 * kMs;                          // el tiempo, en pasos de 100 ms
            if (sLungeAt[k] && q >= sLungeAt[k] && q - sLungeAt[k] < 400 * kMs) { const int t = (int)((q - sLungeAt[k]) / kMs); dx += dir * (t < 120 ? 9 * t / 120 : 9 * (400 - t) / 280); }
            // el que recibe el golpe: lo empujan y cae de espaldas; se queda tirado hasta que le toca (gb_anim_attack) o
            // el juego vuelve a pedir la orden, y se levanta con un brinco
            if (sDown[k]) {
                if (q < sHitAt[k] + 100 * kMs) { dx -= dir * 5; dy = 0; }
                else { dx = -dir * 4; dy = 4; rot = k == 0 ? 2 : 1; }
            } else if (sUpAt[k] && q >= sUpAt[k] && q - sUpAt[k] < 300 * kMs) dy = -5;
        }
        else { static const u8 kSpin[4] = { 0, 1, 3, 2 }; rot = kSpin[(tk / 2) % 4]; } // no esta a la vista: la base de transicion gira
        sAnimDx[k] = dx; sAnimDy[k] = dy; sAnimRot[k] = rot;
    }
}
// un paso de la animacion: solo si algo se movio y solo las dos celdas
static void gb_bat_anim(const GbBat& b, u64 now) {
    static u32 sLastTk;
    const u32 tk = (u32)(now / (100 * (268111856ull / 1000)));
    if (tk == sLastTk || !sVsBgOk) return;
    sLastTk = tk;
    {   // (solo para probar en el emulador: golpeprueba.txt hace que cada 3 s uno le pegue al otro, alternando)
        static int sTest = -1;
        if (sTest < 0) sTest = host_flag("golpeprueba") == 1;
        if (sTest && b.out[0] && b.out[1]) { // cada 4 s ataca uno: se le va encima al otro y medio segundo despues el otro cae
            const int k = (tk / 40) & 1;
            if (tk % 40 == 0) gb_anim_attack(k, now);
            if (tk % 40 == 5) gb_anim_hit(1 - k, now);
        }
    }
    gb_bat_anim_calc(b, now);
    if (sAnimDx[0] == sAnimShown[0][0] && sAnimDy[0] == sAnimShown[0][1] && sAnimRot[0] == sAnimShown[0][2] && sAnimDx[1] == sAnimShown[1][0] && sAnimDy[1] == sAnimShown[1][1] && sAnimRot[1] == sAnimShown[1][2]) return;
    if (!gb_screen_begin()) return;
    gb_vs_copy(false);
    gb_vs_minis(b, 2);
    for (int k = 0; k < 2; k++) GSPGPU_FlushDataCache(sGbFb + (kVsCx[k] - GB_VS_W / 2) * 240 * 3, GB_VS_W * 240 * 3); // solo esas columnas
    gfxScreenSwapBuffers(GFX_BOTTOM, false);
    sAnimFrames++;
    sAnimTicks += svcGetSystemTick() - now;
}
// ---- lo tocado se ilumina redibujando solo su pedazo (un dibujo completo cuesta decenas de veces mas) ----
static u8 sPressKeep[304 * 50 * 3];
static int sPressKx, sPressKy, sPressKw, sPressKh;
static bool sPressKbad, sPressDrawn, sPressBaked; // Baked: lo pinto un dibujo completo (no hay fondo guardado)
static bool sBatNow; // algo cambio por un toque: se relee y redibuja ya, sin esperar los 100 ms
static void gb_rect_io(u8* fb, u8* keep, int x0, int y0, int w, int h, bool save) {
    for (int x = 0; x < w; x++) {
        u8* col = fb + ((x0 + x) * 240 + (239 - (y0 + h - 1))) * 3;
        if (save) memcpy(keep + x * h * 3, col, h * 3); else memcpy(col, keep + x * h * 3, h * 3);
    }
}
// muestra la pagina recien dibujada y deja ese pedazo igual en la otra
static void gb_present_rect(int x0, int y0, int w, int h) {
    u8* front = sGbFb;
    GSPGPU_FlushDataCache(front, 240 * 320 * 3);
    gfxScreenSwapBuffers(GFX_BOTTOM, false);
    u16 fw = 0, fh = 0;
    u8* back = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fw, &fh);
    if (!back || back == front || fw != 240 || fh != 320) return;
    for (int x = 0; x < w; x++) { const int o = ((x0 + x) * 240 + (239 - (y0 + h - 1))) * 3; memcpy(back + o, front + o, h * 3); }
}
static void gb_press_mark(u64 now) { // despues de un dibujo completo: lo iluminado (si lo hay) quedo pintado en el
    sPressDrawn = sPressBaked = now < sPressUntil;
    sPressKx = sPressX; sPressKy = sPressY; sPressKw = sPressW; sPressKh = sPressH; sPressKbad = sPressBad;
}
static void gb_press_step(u64 now, bool* forceFull) {
    const bool on = now < sPressUntil && sPressW > 0 && sPressW <= 304 && sPressH > 0 && sPressH <= 50;
    const bool same = sPressKx == sPressX && sPressKy == sPressY && sPressKw == sPressW && sPressKh == sPressH && sPressKbad == sPressBad;
    if (sPressDrawn && (!on || !same)) { // se quita
        if (sPressBaked) { *forceFull = true; return; }
        if (!gb_screen_begin()) return;
        gb_rect_io(sGbFb, sPressKeep, sPressKx, sPressKy, sPressKw, sPressKh, false);
        gb_present_rect(sPressKx, sPressKy, sPressKw, sPressKh);
        sPressDrawn = false;
    }
    if (on && !sPressDrawn) { // se pone
        if (!gb_screen_begin()) return;
        gb_rect_io(sGbFb, sPressKeep, sPressX, sPressY, sPressW, sPressH, true);
        gb_press_draw();
        gb_present_rect(sPressX, sPressY, sPressW, sPressH);
        gb_press_mark(now);
        sPressBaked = false;
    }
}
// ---- lo que no cambia de un dibujo al siguiente se guarda ya pintado ----
// El fondo, el titulo, los tableros y los botones fijos de cada pantalla (batalla o equipos) se pintan una vez y se
// guardan (una pagina entera, de la memoria lineal); cada dibujo completo empieza copiandola. Igual los cuatro botones
// de ataque, que solo cambian cuando cambia el ataque o sus PP. Sin memoria, se pinta todo como antes.
static u8* sLayer;
static int sLayerKind; // 0 nada, 1 batalla, 2 equipos: una sola pagina guardada; al cambiar de pantalla se vuelve a pintar
static bool sLayerTried;
static bool gb_layer_get(int kind) { // true: ya quedo copiada en la pagina
    if (!sLayer || sLayerKind != kind) return false;
    memcpy(sGbFb, sLayer, 240 * 320 * 3);
    return true;
}
static void gb_layer_put(int kind) {
    if (!sLayerTried) { sLayerTried = true; sLayer = (u8*)linearAlloc(240 * 320 * 3); }
    if (!sLayer) return;
    memcpy(sLayer, sGbFb, 240 * 320 * 3);
    sLayerKind = kind;
}
struct GbMvCache { u8* px; bool ok; u8 mv, pp; };
static GbMvCache sMvC[4];
// apaga un rectangulo (lo deja a 3/8 de brillo): cuatro bytes a la vez, sin mezclar punto por punto
static void gb_dim_rect(int x0, int y0, int w, int h) {
    for (int x = 0; x < w; x++) {
        u8* p = sGbFb + ((x0 + x) * 240 + (239 - (y0 + h - 1))) * 3;
        int n = h * 3;
        for (; n >= 4; n -= 4, p += 4) { u32 v; memcpy(&v, p, 4); v = ((v >> 2) & 0x3F3F3F3Fu) + ((v >> 3) & 0x1F1F1F1Fu); memcpy(p, &v, 4); }
        for (; n > 0; n--, p++) *p = (u8)((*p >> 2) + (*p >> 3));
    }
}
static void gb_team_static(void) { // lo fijo de la pantalla de equipos
    gb_bg("");
    gb_rect_title("TEAMS");
    gb_panel(8, 31, 130, 150, 0x101A66, 0x6088F0, 1, 225);
    gb_panel(182, 31, 130, 150, 0x101A66, 0x6088F0, 1, 225);
    gb_label(73, 34, "YOUR TEAM", 0xFFD830, kUiFontSm, true);
    gb_label(247, 34, "RIVAL", 0xFFD830, kUiFontSm, true);
    gb_label(160, 92, "VS", 0xFFD830, kUiFontLg, true);
}
static void gb_draw_team(const GbBat& b) {
    sVsBgOk = false;
    if (!gb_screen_begin()) return;
    if (!gb_layer_get(2)) { gb_team_static(); gb_layer_put(2); }
    { const char* right = !b.ready ? "Wait..." : b.hp[0] == 0 ? "Choose a Pokemon" : "Tap to switch"; gb_label(311 - gb_tw(right), 7, right, 0xC8D8FF); }
    sBatGrayKo = true;
    for (int j = 0; j < b.cnt[0]; j++) {
        const int x = GB_TEAM_X(73, b.cnt[0], j);
        if (j == b.act[0] && b.hp[0] > 0) gb_panel(x - 29, GB_TEAM_Y(j) - 22, 58, 44, 0x1C2C90, 0xFFD020, 2, 255);
        gb_bat_mini(x, GB_TEAM_Y(j), b.tsp[0][j], b.thp[0][j], b.tmax[0][j], b.talt[0][j]);
    }
    const int nr = b.cnt[1] < 3 ? 3 : b.cnt[1];
    for (int j = 0; j < nr; j++) {
        const int i = j < b.nseen ? b.seen[j] : -1, x = GB_TEAM_X(247, nr, j);
        if (i >= 0 && i == b.act[1] && b.hp[1] > 0) gb_panel(x - 29, GB_TEAM_Y(j) - 22, 58, 44, 0x1C2C90, 0xFFD020, 2, 255);
        gb_bat_mini(x, GB_TEAM_Y(j), i >= 0 ? b.tsp[1][i] : 0, i >= 0 ? b.thp[1][i] : 0, i >= 0 ? b.tmax[1][i] : 1, i >= 0 && b.talt[1][i], true, i < 0);
    }
    sBatGrayKo = false;
    if (b.hp[0] == 0) { gb_panel(8, 186, 304, 46, 0x1C2C90, 0x6088F0, 1, 235); gb_label(160, 202, b.ready ? "Tap who goes next" : "Wait...", 0xFFFFFF, kUiFontSm, true); }
    else gb_button(8, 186, 304, 46, "<", 0xC89018, "Back to moves", 0x1C2C90, 0x6088F0);
    gb_press_draw();
    gb_screen_end();
}
static void gb_draw_battle(const GbBat& b) {
    if (b.view == 1) { gb_draw_team(b); return; }
    if (!gb_screen_begin()) return;
    const char* right = b.runq ? "Run?" : !b.ready ? "Wait..." : b.hp[0] == 0 ? "Choose a Pokemon" : "Tap a move";
    if (!gb_layer_get(1)) { // la capa fija: fondo, titulo, la franja del VS y los dos botones de abajo
        gb_bg("");
        gb_rect_title("BATTLE");
        gb_panel(8, 31, 304, 46, 0x101A66, 0x6088F0, 1, 225);
        gb_label(160, 43, "VS", 0xFFD830, kUiFontLg, true);
        gb_button(8, 182, 148, 50, "B", 0xC89018, "Pokemon", 0x1C2C90, 0x6088F0);
        gb_button(164, 182, 148, 50, "START", 0x802020, "Run", 0x782424, 0xFFA096);
        gb_layer_put(1);
    }
    gb_label(311 - gb_tw(right), 7, right, 0xC8D8FF);
    gb_vs_minis(b, 1);
    gb_vs_copy(true); // el fondo de las dos celdas ya con su base, para la animacion
    gb_bat_anim_calc(b, svcGetSystemTick());
    gb_vs_minis(b, 2);
    if (b.runq) { // el juego pregunta si se huye (arriba, con la cruz y A): aqui se contesta tocando
        gb_panel(8, 81, 304, 96, 0x101A66, 0x6088F0, 1, 225);
        gb_label(160, 118, "Run from this battle?", 0xFFFFFF, kUiFontLg, true);
        gb_panel(6, 180, 308, 54, 0x2452B8, 0x2452B8, 1, 255, 2); // tapa los botones fijos
        gb_button(8, 182, 148, 50, "<", 0xC89018, "No, go back", 0x1C2C90, 0x6088F0);
        gb_button(164, 182, 148, 50, "!", 0x802020, "Yes, run", 0x782424, 0xFFA096);
        gb_press_draw();
        gb_screen_end();
        return;
    }
    for (int i = 0; i < 4; i++) {
        const int x = 8 + (i & 1) * 156, y = 81 + (i >> 1) * 50, mv = b.move[i] >= 1 && b.move[i] <= 165 ? b.move[i] : 0;
        GbMvCache& c = sMvC[i];
        if (c.px && c.ok && c.mv == mv && c.pp == b.pp[i]) { gb_rect_io(sGbFb, c.px, x, y, 148, 46, false); continue; }
        if (!mv) gb_panel(x, y, 148, 46, 0x1A2060, 0x40508C, 1, 200);
        else {
            const GbTypeInfo& t = gb_type_of_move(mv);
            const bool off = b.pp[i] == 0;
            gb_panel(x, y, 148, 46, off ? 0x343850 : gb_shade(t.color, 78), off ? 0x6E6E82 : gb_shade(t.color, 135), 2, 240);
            char nm[40];
            gb_fit(nm, sizeof nm, kMoveName[mv], 132);
            gb_label(x + 9, y + 5, nm, off ? 0x9696A5 : 0xFFFFFF);
            const int tw = gb_tw(t.name) + 12;
            gb_panel(x + 8, y + 25, tw, 15, off ? 0x787887 : 0xFFFFFF, off ? 0x787887 : 0xFFFFFF, 1, 255, 7);
            gb_text(x + 14, y + 25, t.name, off ? 0x3C3C46 : gb_shade(t.color, 60));
            char pp[12];
            snprintf(pp, sizeof pp, "PP %d", b.pp[i]);
            gb_label(x + 140 - gb_tw(pp), y + 25, pp, off ? 0x9696A5 : 0xFFFFFF);
        }
        if (!c.px) c.px = (u8*)linearAlloc(148 * 46 * 3);
        if (c.px) { gb_rect_io(sGbFb, c.px, x, y, 148, 46, true); c.ok = true; c.mv = (u8)mv; c.pp = b.pp[i]; }
    }
    if (!b.ready) gb_dim_rect(6, 79, 308, 155); // el juego no esta esperando la orden: todo apagado
    gb_press_draw();
    gb_screen_end();
}
// ---- tocar: la eleccion se manda al juego como botones del control 1 ----
// Cada toque es un encargo (un ataque, un cambio, huir...) que se cumple paso a paso mirando el menu del juego en
// cada lectura del control: se aprieta UN boton, se sostiene hasta que el juego reacciona (o 250 ms) y se suelta; el
// siguiente paso sale del estado en que quedo el juego, no de un reloj. Asi no hay botones repetidos: solo se vuelve a
// apretar si el juego sigue exactamente donde estaba (no vio el boton). Lo que no se pudo queda dicho en el registro.
//   ataque:  [aviso: Z] [menu de cambio abierto: L] menu principal: A -> ataques: C de su casilla -> orden dada
//   cambio:  [aviso: Z] [ataques abierto: L] menu principal: B -> cambio: boton de su lugar -> orden dada
//            (func_8430E72C: hasta 3, C arriba/derecha/abajo; de 4 a 6, C arriba, C izquierda, B, C derecha, C abajo, A)
//   huir:    menu principal: START -> pregunta (unk_10 = 4). Si: cruz derecha (unk_2F = 1) y A. No: cruz izquierda y A.
enum { GJ_NONE, GJ_MOVE, GJ_SWITCH, GJ_RUN, GJ_RUN_YES, GJ_RUN_NO };
static int sJob, sJobSlot, sJobEdges;
static bool sJobBig, sJobSel; // equipo de 4 a 6; ya se mando el boton que elige (lo que siga es la respuesta del juego)
static u16 sJobBtn;
static u32 sPadReads, sJobFrom, sJobRead0;
static u64 sJobStart;
static u32 sStallN, sStallMaxMs, sStallJob; // vueltas del juego de mas de 0.4 s durante la batalla (y con una orden en curso)
static u64 sPadAt;
static GbBatSt sJobAt; // como estaba el juego al apretar
static u32 sJobDone, sJobRefused, sJobLost, sTapBusy; // para el resumen
static bool sBatReadyNow;
static const char* const kJobName[] = { "", "ataque", "cambio", "huir", "huir: si", "huir: no" };
static void gb_job_end(const char* how, u32* counter) {
    logf("[tablero] %s %d: %s (%d botones, %lu vueltas, %lu ms)\n", kJobName[sJob], sJobSlot + 1, how, sJobEdges, (unsigned long)(sPadReads - sJobRead0),
         (unsigned long)((svcGetSystemTick() - sJobStart) / (268111856ull / 1000)));
    if (counter) (*counter)++;
    if (counter && counter != &sJobDone) { sPressBad = true; sPressUntil = svcGetSystemTick() + 268111856ull * 4 / 10; } // se nota en rojo
    sJob = GJ_NONE;
    sJobBtn = 0;
}
static void gb_job_start(int job, int slot) {
    sJob = job; sJobSlot = slot; sJobEdges = 0; sJobSel = false; sJobBtn = 0;
    sJobStart = svcGetSystemTick(); sJobFrom = sPadReads - 2; sJobRead0 = sPadReads;
}
static u16 gb_bat_buttons(void) {
    const u64 now = svcGetSystemTick(), kMs = 268111856ull / 1000;
    sPadReads++;
    if (!BLOB_BAT_ON || !BLOB_BATTLE_MON) return 0;
    const GbBatSt st = gb_bat_state();
    sBatReadyNow = gb_bat_ready(st);
    if (st.ok && sPadAt && now - sPadAt > 400 * kMs) { // el juego tardo mucho en dar esta vuelta (carga algo, por ejemplo al abrir un menu)
        const u32 ms = (u32)((now - sPadAt) / kMs);
        sStallN++;
        if (ms > sStallMaxMs) sStallMaxMs = ms;
        if (sJob != GJ_NONE) sStallJob++;
        logf("[tablero] el juego se detuvo %lu ms (menu %lu, %s)\n", (unsigned long)ms, (unsigned long)st.u10, sJob != GJ_NONE ? kJobName[sJob] : "sin orden en curso");
    }
    sPadAt = st.ok ? now : 0;
    if (sJob == GJ_NONE) return 0;
    if (!st.ok) { gb_job_end("ya no hay batalla", nullptr); return 0; }
    // Todo se cuenta en lecturas del control: el juego lee una vez por vuelta (Cont_ReadInputs) y ahi mismo ve el boton
    // nuevo. Si el juego se detiene a cargar algo no hay lecturas, asi que el boton lo espera apretado: no se pierde.
    if (sJobBtn) { // sostenido: dos lecturas al menos; se suelta cuando el juego reacciono, o a las ocho
        const bool moved = st.u10 != sJobAt.u10 || st.msg != sJobAt.msg || st.u2F != sJobAt.u2F;
        const u32 held = sPadReads - sJobFrom;
        if (held < 2 || (!moved && held < 8)) return sJobBtn;
        sJobBtn = 0; sJobFrom = sPadReads;
        return 0;
    }
    if (sPadReads - sJobFrom < 2) return 0; // suelto dos lecturas
    u16 want = 0;
    const bool pickMenu = sJob == GJ_MOVE ? st.u10 == 1 : st.u10 == 2;
    switch (sJob) {
    case GJ_MOVE:
    case GJ_SWITCH:
        if (st.u10 == 3) { gb_job_end("hecho", &sJobDone); return 0; }
        if (st.msg) { if (sJobSel) { gb_job_end("el juego no lo permite (aviso arriba)", &sJobRefused); return 0; } want = 0x2000; break; }
        if (pickMenu) {
            static const u16 kC[4] = { 0x0008, 0x0001, 0x0004, 0x0002 };                    // C arriba, derecha, abajo, izquierda
            static const u16 kS[3] = { 0x0008, 0x0001, 0x0004 };                            // C arriba, derecha, abajo
            static const u16 kS6[6] = { 0x0008, 0x0002, 0x4000, 0x0001, 0x0004, 0x8000 };   // C arriba, C izq., B, C der., C abajo, A
            want = sJob == GJ_MOVE ? kC[sJobSlot & 3] : sJobBig ? kS6[sJobSlot % 6] : kS[sJobSlot % 3];
            sJobSel = true;
        } else if (st.u10 == 1 || st.u10 == 2) { // esta abierto el otro menu: se cierra con L (el de cambio obligado no se puede)
            if (st.u10 == 2 && st.fainted) { gb_job_end("hay que elegir otro Pokemon", &sJobRefused); return 0; }
            want = 0x0020;
        } else if (st.u10 == 4) { gb_job_end("esta abierta la pregunta de huir", &sJobRefused); return 0; }
        else if (st.fresh) want = sJob == GJ_MOVE ? 0x8000 : 0x4000;
        break;
    case GJ_RUN:
        if (st.u10 == 4) { gb_job_end("hecho", &sJobDone); return 0; }
        if (st.u10 == 3) { gb_job_end("la orden ya estaba dada", &sJobRefused); return 0; }
        if (st.msg) want = 0x2000;
        else if (st.u10 == 1 || (st.u10 == 2 && !st.fainted)) want = 0x0020;
        else if (st.u10 == 0 && st.fresh) want = 0x1000;
        break;
    case GJ_RUN_YES:
    case GJ_RUN_NO: {
        const u8 wantYes = sJob == GJ_RUN_YES;
        if (st.u10 != 4 || sJobSel) { gb_job_end("hecho", &sJobDone); return 0; }
        if (st.u2F != wantYes) want = wantYes ? 0x0100 : 0x0200; // cruz derecha: si; cruz izquierda: no
        else { want = 0x8000; sJobSel = true; }
        break; }
    }
    // se deja a las 150 vueltas del juego (unos 5 s de juego), o a los 15 s de reloj
    if (sPadReads - sJobRead0 > 150 || now - sJobStart > 15000 * kMs || sJobEdges >= 10) { gb_job_end(want ? "el juego no respondio" : "el juego no llego a pedir la orden", &sJobLost); return 0; }
    if (!want) return 0; // todavia no toca (el juego aun no pide la orden): se espera
    sJobEdges++; sJobBtn = want; sJobAt = st; sJobFrom = sPadReads;
    return want;
}
struct GbTap { int px, py; };
static void gb_bat_tap(const GbBat& b, GbTap tp) {
    sBatTouches++;
    const auto press = [](int x, int y, int w, int h, bool bad) { sPressX = x; sPressY = y; sPressW = w; sPressH = h; sPressBad = bad; sPressUntil = svcGetSystemTick() + 268111856ull / 4; };
    // se manda algo al juego solo si esta esperando la orden y no hay otro encargo a medias
    const bool can = b.ready && sBatReadyNow && sJob == GJ_NONE;
    const auto busy = [&]() { if (!can) { sTapBusy++; logf("[tablero] toque sin efecto: %s\n", sJob != GJ_NONE ? "aun se estaba mandando el anterior" : "el juego no estaba pidiendo la orden"); } return !can; };
    if (b.runq && b.view == 0) { // la pregunta de huir
        if (tp.py < 182) return;
        press(tp.px < 160 ? 8 : 164, 182, 148, 50, !can);
        if (!busy()) gb_job_start(tp.px < 160 ? GJ_RUN_NO : GJ_RUN_YES, 0);
    } else if (b.view == 1) {
        if (tp.py >= 186) { if (b.hp[0] > 0) { sBatView = 0; sBatNow = true; } } // cambia la pantalla: eso ya es la respuesta
        else if (tp.px < 140) {
            for (int j = 0; j < b.cnt[0]; j++) {
                const int x = GB_TEAM_X(73, b.cnt[0], j);
                if (tp.px < x - 29 || tp.px >= x + 29 || tp.py < GB_TEAM_Y(j) - 21 || tp.py >= GB_TEAM_Y(j) + 21) continue;
                const bool ok = b.thp[0][j] > 0 && (j != b.act[0] || b.hp[0] == 0);
                press(x - 29, GB_TEAM_Y(j) - 22, 58, 44, !ok || !can);
                if (ok && !busy()) { gb_job_start(GJ_SWITCH, j); sJobBig = b.cnt[0] > 3; if (b.hp[0] > 0) { sBatView = 0; sBatNow = true; } }
            }
        }
    } else if (tp.py >= 81 && tp.py < 181) {
        const int i = (tp.px >= 160 ? 1 : 0) + (tp.py >= 131 ? 2 : 0);
        const bool ok = b.hp[0] > 0 && b.move[i] >= 1 && b.move[i] <= 165 && b.pp[i] > 0; // debilitado: toca elegir otro
        press(8 + (i & 1) * 156, 81 + (i >> 1) * 50, 148, 46, !ok || !can);
        if (ok && !busy()) gb_job_start(GJ_MOVE, i);
    } else if (tp.py >= 182) {
        if (tp.px < 160) { // Pokemon: la pantalla de equipos; como todo lo demas, solo cuando el juego pide la orden
            if (b.ready) { sBatView = 1; sBatNow = true; }
            else { press(8, 182, 148, 50, true); sTapBusy++; logf("[tablero] toque sin efecto: el juego no estaba pidiendo la orden\n"); }
        }
        else { press(164, 182, 148, 50, !can); if (!busy()) gb_job_start(GJ_RUN, 0); }
    }
}
#include "gb_roam.inc"
#include "gb_sel.inc"
static void gb_bat_touch(const GbBat& b) {
    static bool sWas;
    const bool down = (hidKeysHeld() & KEY_TOUCH) != 0;
    if (down && !sWas) {
        touchPosition tp;
        hidTouchRead(&tp);
        gb_bat_tap(b, GbTap{ tp.px, tp.py });
    }
    sWas = down;
}
// cada vuelta del host (barato: una lectura cada 100 ms; se dibuja solo si cambio algo)
static void gb_bat_tick(void) {
    static u64 sNext;
    static bool sShown;
    static GbBat sPrev;
    if (!sGbUiOn || !BLOB_BAT_ON || !BLOB_BATTLE_MON) return;
    const u64 now = svcGetSystemTick();
    if (sShown) gb_bat_touch(sPrev);
    else {
        static bool sSelWas;
        const bool sel = gb_sel_tick(now); // la eleccion de Pokemon antes de la batalla tapa a la pantalla libre
        if (sel != sSelWas) { sSelWas = sel; gb_roam_reset(); }
        if (!sel) gb_roam_tick(now);
    }
    {   // (solo para probar en el emulador, que no tiene pantalla tactil: toque.txt = N toca el ataque N cada 3 s)
        static int sAuto = -2;
        static u64 sAutoT;
        // toque.txt = 8: una batalla completa: primero cambia al Pokemon 2, luego ataca con el 2 y, si se debilita, saca al siguiente
        static bool sAutoSw;
        static int kAutoN; // el cambio voluntario va despues de dos ataques (ya en turno normal)
        if (sAuto == -2) { const u32 v = host_flag("toque"); sAuto = v >= 1 && v <= 9 ? (int)v - 1 : -1; }
        if (sAuto >= 0 && sShown && sJob == GJ_NONE && now - sAutoT > 268111856ull * 3) {
            // se toca por coordenadas, igual que con el dedo: ataque 2; con 8, tambien abre "Pokemon" y cambia
            sAutoT = now;
            const GbBat& q = sPrev;
            int alive = -1;
            for (int j = 0; j < q.cnt[0]; j++) if (alive < 0 && q.thp[0][j] > 0 && (j != q.act[0] || q.hp[0] == 0)) alive = j;
            // toque.txt = 9: lo mismo que 8 pero toca aunque el juego no este listo (para ver los rechazos) y primero
            // abre la pregunta de huir y contesta que no
            static int sAutoRun;
            if (sAuto == 7 && !q.ready) { sAutoT = now - 268111856ull * 5 / 2; }
            else if (q.runq && q.view == 0) { gb_bat_tap(q, GbTap{ 80, 206 }); sAutoRun = 2; }
            else if (sAuto == 8 && sAutoRun == 0 && q.ready && q.view == 0) { sAutoRun = 1; gb_bat_tap(q, GbTap{ 240, 206 }); }
            else if (q.view == 1) {
                if (alive >= 0 && (q.hp[0] == 0 || !sAutoSw)) { sAutoSw = true; gb_bat_tap(q, GbTap{ GB_TEAM_X(73, q.cnt[0], alive), GB_TEAM_Y(alive) }); }
                else gb_bat_tap(q, GbTap{ 160, 208 });
            } else if (sAuto >= 7 && !sAutoSw && kAutoN++ >= 2) gb_bat_tap(q, GbTap{ 80, 206 });
            else gb_bat_tap(q, GbTap{ sAuto >= 7 ? 236 : 8 + (sAuto & 1) * 156 + 74, sAuto >= 7 ? 104 : 81 + ((sAuto & 3) >> 1) * 50 + 23 });
        }
    }
    static u64 sShownAt;
    bool force = false;
    if (sShown) { sBatShownTicks += now - sShownAt; gb_press_step(now, &force); if (sPrev.view == 0 && !force) gb_bat_anim(sPrev, now); }
    sShownAt = now;
    if (sBatNow) { sBatNow = false; sNext = 0; }
    if (now < sNext && !force) return;
    sNext = now + 268111856ull / 10;
    // hay batalla mientras el juego lo diga (gPsBatOn); si un dato no se puede leer un momento, se deja lo que habia
    const bool on = BLOB_BAT_ON && intro_be(BLOB_BAT_ON) != 0;
    GbBat b;
    sGbNoWait = true;
    if (on && gb_bat_read(&b) && (!sShown || force || memcmp(&b, &sPrev, sizeof b) != 0)) { if (!sShown) { // empieza la batalla: los minis de los dos equipos, de una vez
            u8 recs[14]; int n = 0;
            for (int k = 0; k < 2; k++) for (int j = 0; j < b.cnt[k] && n < 14; j++) { recs[n++] = b.tsp[k][j]; if (b.talt[k][j] && n < 14) recs[n++] = GB_MINI_ALT; }
            gb_mini_prime(recs, n);
        }
        if (sShown && (b.view != sPrev.view || b.runq != sPrev.runq)) sPressUntil = 0; /* cambio de pantalla: lo iluminado ya no va */ sPrev = b; sShown = true; const u64 t0 = svcGetSystemTick(); gb_draw_battle(b); gb_press_mark(t0); sFullDraws++; sFullTicks += svcGetSystemTick() - t0; }
    else if (!on && sShown) { sShown = false; sVsBgOk = false; sJob = GJ_NONE; sJobBtn = 0; sBatNSeen = 0; sBatView = 0; gb_bat_out_reset(); gb_roam_reset(); }
    sGbNoWait = false;
}
#else
static void gb_bat_tick(void) {}
static u16 gb_bat_buttons(void) { return 0; }
#endif

// ---- guardar ----
static bool gb_write_file(const char* path, const u8* data, u32 size) {
    FILE* f = fopen(path, "wb");
    bool ok = f && fwrite(data, 1, size, f) == size;
    if (f && fclose(f) != 0) ok = false;
    if (!ok) remove(path);
    return ok;
}
// Consola Virtual: copias en la SD y luego se escribe en los datos guardados del juego y se confirma
static bool gb_flush_vc(int slot) {
    GbSlot& g = sGb[slot];
    const char* file = gb_file_path(slot);
    if (!g.backupDone) {
        const char* v = kGbVerName[g.e.version];
        const char* lang = g.e.name + strlen("Consola Virtual");
        char first[220], prev[220];
        snprintf(first, sizeof first, "%s/ConsolaVirtual_%s%s%s_original.bak", GB_DIR, v, *lang ? "_" : "", *lang ? lang + 1 : "");
        snprintf(prev, sizeof prev, "%s/ConsolaVirtual_%s%s%s_anterior.bak", GB_DIR, v, *lang ? "_" : "", *lang ? lang + 1 : "");
        if (!g.orig) { logf("[gb] sin la copia en memoria de la partida original: NO se guarda\n"); return false; }
        struct stat st;
        if (stat(first, &st) != 0 && !gb_write_file(first, g.orig, g.fileSize)) { logf("[gb] no pude hacer la copia %s: NO se guarda la partida\n", first); return false; }
        if (!gb_write_file(prev, g.orig, g.fileSize)) { logf("[gb] no pude hacer la copia %s: NO se guarda la partida\n", prev); return false; }
        logf("[gb] copias de la partida de la Consola Virtual: %s y %s\n", first, prev);
        g.backupDone = true;
    }
    FILE* f = fopen(file, "r+b");
    bool ok = f && fwrite(g.file, 1, g.fileSize, f) == g.fileSize;
    if (f && fclose(f) != 0) ok = false;
    const Result rc = ok ? archiveCommitSaveData(kGbVcDev[slot]) : -1;
    if (ok && R_FAILED(rc)) { ok = false; logf("[gb] no se pudo confirmar el guardado en la Consola Virtual (%08lx)\n", (unsigned long)rc); }
    if (ok) { // se vuelve a leer para comprobar
        static u8 chk[4096];
        FILE* r = fopen(file, "rb");
        u32 off = 0;
        size_t got;
        while (r && (got = fread(chk, 1, sizeof chk, r)) > 0) {
            if (off + got > g.fileSize || memcmp(chk, g.file + off, got) != 0) { ok = false; break; }
            off += (u32)got;
        }
        if (!r || off != g.fileSize) ok = false;
        if (r) fclose(r);
        if (!ok) logf("[gb] la partida de la Consola Virtual no quedo igual a lo escrito\n");
    }
    return ok;
}
static void gb_flush_slot(int slot, const char* why) {
    GbSlot& g = sGb[slot];
    if (!g.present || !g.dirty) return;
    STAGE(ST_SAVE);
    RecursiveLock_Lock(&sFileLock);
    if (g.e.vcLow) {
        const bool okVc = gb_flush_vc(slot);
        RecursiveLock_Unlock(&sFileLock);
        if (okVc) { g.dirty = false; g.flushes++; logf("[gb] control %d: partida guardada en la Consola Virtual (%s)\n", slot + 1, why); }
        else { g.flushFails++; g.dirtyTick = svcGetSystemTick(); if (g.flushFails > 3) g.dirty = false; }
        return;
    }
    char tmp[220], bak[220];
    snprintf(tmp, sizeof tmp, "%s.tmp", g.e.path);
    snprintf(bak, sizeof bak, "%s.bak", g.e.path);
    bool ok = true;
    if (!g.backupDone) {
        // copia del archivo tal como estaba, una sola vez en la vida del archivo
        struct stat st;
        if (stat(bak, &st) != 0) {
            FILE* src = fopen(g.e.path, "rb");
            FILE* dst = src ? fopen(bak, "wb") : nullptr;
            if (src && dst) {
                static u8 chunk[4096];
                size_t got;
                while ((got = fread(chunk, 1, sizeof chunk, src)) > 0) if (fwrite(chunk, 1, got, dst) != got) { ok = false; break; }
            } else ok = false;
            if (src) fclose(src);
            if (dst && fclose(dst) != 0) ok = false;
            if (!ok) { remove(bak); logf("[gb] no pude hacer la copia %s: NO se guarda la partida\n", bak); }
            else logf("[gb] copia de la partida original: %s\n", bak);
        }
        if (ok) g.backupDone = true;
    }
    if (ok) {
        FILE* f = fopen(tmp, "wb");
        ok = f && fwrite(g.file, 1, g.fileSize, f) == g.fileSize;
        if (f && fclose(f) != 0) ok = false;
        if (ok) { remove(g.e.path); ok = rename(tmp, g.e.path) == 0; }
        else remove(tmp);
    }
    RecursiveLock_Unlock(&sFileLock);
    if (ok) { g.dirty = false; g.flushes++; logf("[gb] control %d: partida guardada (%s)\n", slot + 1, why); }
    else { g.flushFails++; g.dirtyTick = svcGetSystemTick(); if (g.flushFails <= 3) logf("[gb] control %d: no pude guardar la partida (%s)\n", slot + 1, why); else g.dirty = false; }
}
static void gb_flush(const char* why) { for (int s = 0; s < GB_SLOTS; s++) gb_flush_slot(s, why); }
// cada cuadro: si el juego dejo de escribir hace 2 s, se guarda
static void gb_tick(void) {
    for (int s = 0; s < GB_SLOTS; s++)
        if (sGb[s].dirty && svcGetSystemTick() - sGb[s].dirtyTick > 2ull * 268111856ull) gb_flush_slot(s, "al terminar de escribir");
}
static void gb_close(void) { // al salir, ya con todo guardado
    for (int s = 0; s < GB_SLOTS; s++) if (sGb[s].vcMounted) { archiveUnmount(kGbVcDev[s]); sGb[s].vcMounted = false; }
}
static void gb_summary(void) {
    if (sGbVcNote[0]) logf("[gb] Consola Virtual: %s\n", sGbVcNote);
    if (sFullDraws) {
        const u64 kUs = 268111856ull / 1000000;
        const u32 sec = (u32)(sBatShownTicks / 268111856ull);
        logf("[tablero] dibujo: %lu completos de %lu us; animacion %lu pasos de %lu us (%lu ms por segundo en %lu s de batalla)\n",
             (unsigned long)sFullDraws, (unsigned long)(sFullTicks / kUs / sFullDraws), (unsigned long)sAnimFrames,
             (unsigned long)(sAnimFrames ? sAnimTicks / kUs / sAnimFrames : 0),
             (unsigned long)(sec ? (sAnimTicks + sFullTicks) / kUs / 1000 / sec : 0), (unsigned long)sec);
    }
    logf("[tablero] pausas del juego en batalla de mas de 0.4 s: %lu (la mas larga %lu ms; %lu con una orden en curso)\n", (unsigned long)sStallN, (unsigned long)sStallMaxMs, (unsigned long)sStallJob);
    logf("[tablero] toques en batalla: %lu | ordenes cumplidas %lu, sin efecto por tocar antes de tiempo %lu, negadas por el juego %lu, sin respuesta del juego %lu\n",
         (unsigned long)sBatTouches, (unsigned long)sJobDone, (unsigned long)sTapBusy, (unsigned long)sJobRefused, (unsigned long)sJobLost);
    {
        const u64 kUs = 268111856ull / 1000000ull;
        const u32 sec = (u32)(sGrShownTicks / 268111856ull);
        logf("[eleccion] toques %lu | botones mandados %lu, sin efecto %lu | dibujos %lu\n", (unsigned long)sSelTaps, (unsigned long)sSelSent, (unsigned long)sSelBad, (unsigned long)sSelFulls);
    logf("[libre] %d Pokemon | %lu pasos de %lu us (%lu ms por segundo en %lu s a la vista), %lu dibujos completos | agarrados %lu, aventados %lu, choques %lu\n",
             sGrN, (unsigned long)sGrSteps, (unsigned long)(sGrSteps ? sGrStepTicks / kUs / sGrSteps : 0), (unsigned long)(sec ? sGrStepTicks / kUs / 1000 / sec : 0),
             (unsigned long)sec, (unsigned long)sGrFulls, (unsigned long)sGrGrabs, (unsigned long)sGrThrows, (unsigned long)sGrBumps);
    }
    for (int s = 0; s < GB_SLOTS; s++) {
        const GbSlot& g = sGb[s];
        if (!g.present) continue;
        logf("[gb] control %d: %s (%s): %lu lecturas, %lu escrituras, guardada %lu veces%s\n", s + 1, g.e.name, kGbVerName[g.e.version], (unsigned long)g.reads,
             (unsigned long)g.writes, (unsigned long)g.flushes, g.flushFails ? " (HUBO FALLAS AL GUARDAR)" : "");
    }
}

// ---- lo que ve el juego ----
extern "C" u32 host_gb_present(u32 ch) { return ch < GB_SLOTS && sGb[ch].present; }
extern "C" void host_gb_power(u32 ch, u32 on) {
    if (ch >= GB_SLOTS || !sGb[ch].present) return;
    if (!on) { sGb[ch].ramOn = false; sGb[ch].ramBank = 0; gb_flush_slot((int)ch, "al apagar el cartucho"); } // sin corriente el controlador de bancos se reinicia
}
// Lee o escribe 'size' bytes desde la direccion 'addr' del Game Boy. chw = control | (escribir << 8).
extern "C" u32 host_gb_rw(u32 chw, u32 addr, u8* buf, u32 size) {
    const u32 ch = chw & 0xFF;
    if (ch >= GB_SLOTS || !sGb[ch].present) return 0;
    GbSlot& g = sGb[ch];
    const bool write = (chw & 0x100) != 0;
    if (write) g.writes++; else g.reads++;
    for (u32 i = 0; i < size; i++) {
        const u32 a = (addr + i) & 0xFFFF;
        if (write) {
            const u8 v = buf[i];
            if (a < 0x2000) g.ramOn = (v & 0x0F) == 0x0A;
            else if (a < 0x4000) { /* banco de ROM: no hay ROM */ }
            else if (a < 0x6000) g.ramBank = v & 0x0F;
            else if (a < 0x8000) { /* modo de bancos o reloj */ }
            else if (a >= 0xA000 && a < 0xC000 && g.ramOn && g.ramBank < 4) {
                const u32 off = (u32)g.ramBank * 0x2000u + (a - 0xA000);
                if (g.file[off] != v) {
                    g.file[off] = v;
                    // los primeros 32 bytes son donde Stadium prueba que el cartucho responde: eso solo no amerita guardar
                    // (gbprueba.txt, solo para probar el guardado en el emulador: ahi tambien cuenta)
                    static int sTest = -1;
                    if (sTest < 0) sTest = host_flag("gbprueba") == 1;
                    if (off >= 0x20 || sTest) { g.dirty = true; g.dirtyTick = svcGetSystemTick(); }
                }
            }
        } else {
            u8 v = 0xFF;
            if (a < 0x150) v = g.e.header[a];
            else if (a >= 0xA000 && a < 0xC000 && g.ramOn && g.ramBank < 4) v = g.file[(u32)g.ramBank * 0x2000u + (a - 0xA000)];
            buf[i] = v;
        }
    }
    return 1;
}

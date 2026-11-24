#include "2E110.h"
#include "src/3FB0.h"
#include "src/memory.h"

#define D_800AF740_NUM_FILES 42

static BinArchive* D_800AF740;
static char** D_800AF744;

void func_8002D510(void) {
    s32 i;

    D_800AF740 = func_800044F4(textdata_ROM_START, NULL, 1, 0);
    D_800AF744 = main_pool_alloc(D_800AF740_NUM_FILES * sizeof(char*), 0);

    for (i = 0; i < D_800AF740_NUM_FILES; i++) {
        D_800AF744[i] = NULL;
    }
}

#ifdef PORT
/* Idioma: si el usuario puso un paquete (lang_xx.bin, armado en su PC con tools/lang/make_lang.py) y lo eligio en su
   perfil, las tablas de texto salen de ahi. Mismo formato que las del juego. El host avisa los cambios con gPsLangGen. */
extern u32 ps_lang_read(u32 off, void* dst, u32 size);
s32 gPsLangGen;
static u8 sLang[0x18000];
static s32 sLangGen, sLangOk;
static u32 sLangN;
static __attribute__((unused)) char** ps_lang_table(s32 file_number) {
    u32 off;
    if (sLangGen != gPsLangGen) {
        u32 end = 0, i, pos;
        sLangGen = gPsLangGen;
        sLangOk = 0;
        if (ps_lang_read(0, sLang, 8) == 8 && sLang[0] == 'P' && sLang[1] == 'S' && sLang[2] == 'L' && sLang[3] == 'G' &&
            (sLangN = *(u32*)(sLang + 4)) <= 64 && ps_lang_read(8, sLang + 8, 8 * sLangN) == 8 * sLangN) {
            end = 8 + 8 * sLangN;
            for (i = 0; i < sLangN; i++) { u32 o = *(u32*)(sLang + 8 + 8 * i), z = *(u32*)(sLang + 12 + 8 * i); if (o != 0 && o + z > end) end = o + z; }
            if (end <= sizeof(sLang)) {
                sLangOk = 1;
                for (pos = 8 + 8 * sLangN; pos < end && sLangOk; pos += 0x4000) { u32 n = end - pos < 0x4000 ? end - pos : 0x4000; if (ps_lang_read(pos, sLang + pos, n) != n) sLangOk = 0; }
            }
        }
    }
    if (!sLangOk || file_number < 0 || (u32)file_number >= sLangN) return NULL;
    off = *(u32*)(sLang + 8 + 8 * file_number);
    return off != 0 ? (char**)(sLang + off) : NULL;
}
#endif

char** func_8002D5AC(s32 file_number) {
    return (char**)func_8000484C(D_800AF740, file_number);
}

void func_8002D5D4(u32 arg0, u32 arg1) {
    if ((arg0 >= 0x14) && (arg0 < 0x2A)) {
        D_800AF744[arg0] = (char*)arg1;
    }
}

void func_8002D600(u32 arg0, u32 arg1) {
    if ((arg0 != 0) && (arg0 < 0xA)) {
        D_800AF744[arg0] = (char*)arg1;
    }
}

void func_8002D628(char* arg0, u32 arg1, s8* arg2) {
    u32 i;
    s32 var_s2;
    s32 temp_v0;
    char sp58[12];
    s8* var_s4;
    char* sp54;
    u32 var_s3;

    var_s2 = 0;
    var_s3 = 0;
    sp54 = var_s4;
    var_s4 = sp54;
    i = 0;

    while (i < arg1 - 1u) {
        switch (var_s2) {
            case 0:
                temp_v0 = *arg2++;
                if (temp_v0 == '\x00') {
                    goto end;
                }

                if (temp_v0 == '#') {
                    temp_v0 = *arg2++;

                    temp_v0 -= '0';
                    temp_v0 = (*arg2++ + (temp_v0 * 10)) - '0';
                    if ((temp_v0 > 0) && (temp_v0 < 10)) {
                        sprintf(sp58, "%d", D_800AF744[temp_v0]);
                        var_s4 = sp58;
                        var_s2 = 1;
                        continue;
                    }

                    if ((temp_v0 >= 0x14) && (temp_v0 < 0x2A)) {
                        if (D_800AF744[temp_v0] != NULL) {
                            var_s4 = D_800AF744[temp_v0];
                            var_s2 = 1;
                        }
                        continue;
                    }
                } else {
                    *arg0++ = temp_v0;
                    var_s3++;
                    i++;
                }
                break;

            case 1:
                temp_v0 = *var_s4++;
                if (temp_v0 == '\x00') {
                    var_s2 = 0;
                } else {
                    *arg0++ = temp_v0;
                    var_s3++;
                    i++;
                }
                break;
        }
    }

end:
    *arg0++ = '\x00';
}

char* func_8002D7C0(char* arg0, s32 arg1, char** arg2, u32 file_number) {
    char* sp1C;
    char* sp18 = (u32)arg2 + (u32)arg2[file_number + 1];

    if (arg0 == NULL) {
        sp1C = sp18;
    } else {
        sp1C = arg0;
        func_8002D628(arg0, arg1, sp18);
    }

    return sp1C;
}

s32 func_8002D80C(s8* arg0) {
    s32 last_chr = 0;
    s32 line_count = 0;
    s32 chr = *arg0++;

    while (chr != 0) {
        last_chr = chr;
        if (chr == '\n') {
            line_count++;
        }
        chr = *arg0++;
    }

    if (last_chr != '\n') {
        line_count++;
    }

    return line_count;
}

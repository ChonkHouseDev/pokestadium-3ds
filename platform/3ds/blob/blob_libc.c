/* libc minima y ayudantes de compilador para el bloque del juego (ARM big-endian BE8, 3DS).
 * Ojo: aqui no se puede usar '/' ni '%' sobre enteros (GCC llamaria a estas mismas funciones). */
typedef unsigned int u32; typedef int s32; typedef unsigned long long u64; typedef long long s64;
typedef unsigned char u8; typedef __SIZE_TYPE__ size_t;

/* Copias por palabras cuando se puede (el 3DS viejo es lento: DMA, audio y JPEG copian mucho) */
void* memcpy(void* d, const void* s, size_t n) {
    u8* a = d; const u8* b = s;
    if ((((u32)a ^ (u32)b) & 3) == 0) {
        while (n && ((u32)a & 3)) { *a++ = *b++; n--; }
        u32* wa = (u32*)a; const u32* wb = (const u32*)b;
        while (n >= 16) { u32 x0 = wb[0], x1 = wb[1], x2 = wb[2], x3 = wb[3]; wa[0] = x0; wa[1] = x1; wa[2] = x2; wa[3] = x3; wa += 4; wb += 4; n -= 16; }
        while (n >= 4) { *wa++ = *wb++; n -= 4; }
        a = (u8*)wa; b = (const u8*)wb;
    }
    while (n--) *a++ = *b++;
    return d;
}
void* memmove(void* d, const void* s, size_t n) {
    u8* a = d; const u8* b = s;
    if (a < b || a >= b + n) return memcpy(d, s, n);
    if (0) { } else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
void* memset(void* d, int c, size_t n) {
    u8* a = d;
    while (n && ((u32)a & 3)) { *a++ = (u8)c; n--; }
    u32 w = (u8)c; w |= w << 8; w |= w << 16;
    u32* wa = (u32*)a;
    while (n >= 16) { wa[0] = w; wa[1] = w; wa[2] = w; wa[3] = w; wa += 4; n -= 16; }
    while (n >= 4) { *wa++ = w; n -= 4; }
    a = (u8*)wa;
    while (n--) *a++ = (u8)c;
    return d;
}
int memcmp(const void* x, const void* y, size_t n) { const u8* a = x; const u8* b = y; for (; n; n--, a++, b++) if (*a != *b) return *a - *b; return 0; }
void bzero(void* d, int n) { memset(d, 0, n); }
int bcmp(const void* a, const void* b, int n) { return memcmp(a, b, n); }
void bcopy(const void* s, void* d, int n) { memmove(d, s, n); }
size_t strlen(const char* s) { const char* p = s; while (*p) p++; return p - s; }
char* strchr(const char* s, int c) { for (;; s++) { if (*s == (char)c) return (char*)s; if (!*s) return 0; } }
float sqrtf(float x) { float r; __asm__("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x)); return r; }
int abs_asm(int x) { return x < 0 ? -x : x; }

/* ---- division entera ---- */
static u32 udiv32(u32 n, u32 d, u32* rem) {
    if (d == 0) { if (rem) *rem = n; return 0; }
    if (n < d) { if (rem) *rem = n; return 0; }
    /* division larga solo sobre los bits que hacen falta (antes eran siempre 32 vueltas) */
    int shift = __builtin_clz(d) - __builtin_clz(n);
    u32 dd = d << shift, q = 0;
    for (int i = shift; i >= 0; i--, dd >>= 1) { q <<= 1; if (n >= dd) { n -= dd; q |= 1; } }
    if (rem) *rem = n;
    return q;
}
u32 __aeabi_uidiv(u32 n, u32 d) { return udiv32(n, d, 0); }
s32 __aeabi_idiv(s32 n, s32 d) {
    int neg = (n < 0) ^ (d < 0);
    u32 q = udiv32(n < 0 ? -(u32)n : (u32)n, d < 0 ? -(u32)d : (u32)d, 0);
    return neg ? -(s32)q : (s32)q;
}
/* uidivmod/idivmod devuelven {cociente r0, residuo r1}: en BE un u64 va r0=alta, r1=baja */
u64 __aeabi_uidivmod(u32 n, u32 d) { u32 r; u32 q = udiv32(n, d, &r); return ((u64)q << 32) | r; }
u64 __aeabi_idivmod(s32 n, s32 d) {
    u32 r; u32 q = udiv32(n < 0 ? -(u32)n : (u32)n, d < 0 ? -(u32)d : (u32)d, &r);
    s32 qs = ((n < 0) ^ (d < 0)) ? -(s32)q : (s32)q;
    s32 rs = n < 0 ? -(s32)r : (s32)r;
    return ((u64)(u32)qs << 32) | (u32)rs;
}
static int clz64(u64 x) { u32 h = (u32)(x >> 32); return h ? __builtin_clz(h) : 32 + __builtin_clz((u32)x); }
static u64 udiv64(u64 n, u64 d, u64* rem) {
    if (d == 0) { if (rem) *rem = n; return 0; }
    if (n < d) { if (rem) *rem = n; return 0; }
    if ((n >> 32) == 0) { u32 r; u32 q = udiv32((u32)n, (u32)d, &r); if (rem) *rem = r; return q; }
    /* era el 37% del tiempo del juego: 64 vueltas siempre. Ahora solo los bits de diferencia */
    int shift = clz64(d) - clz64(n);
    u64 dd = d << shift, q = 0;
    for (int i = shift; i >= 0; i--, dd >>= 1) { q <<= 1; if (n >= dd) { n -= dd; q |= 1; } }
    if (rem) *rem = n;
    return q;
}
void ps_uldivmod(u64 n, u64 d, u64* q, u64* r) { *q = udiv64(n, d, r); }
void ps_ldivmod(s64 n, s64 d, s64* q, s64* r) {
    u64 ur; u64 uq = udiv64(n < 0 ? -(u64)n : (u64)n, d < 0 ? -(u64)d : (u64)d, &ur);
    *q = ((n < 0) ^ (d < 0)) ? -(s64)uq : (s64)uq;
    *r = n < 0 ? -(s64)ur : (s64)ur;
}

/* ---- conversiones 64 bits <-> flotante (usan VFP para la parte de 32 bits) ---- */
#define TWO32 4294967296.0
double __aeabi_ul2d(u64 x) { return (double)(u32)(x >> 32) * TWO32 + (double)(u32)x; }
double __aeabi_l2d(s64 x) { return x < 0 ? -__aeabi_ul2d(-(u64)x) : __aeabi_ul2d((u64)x); }
float __aeabi_ul2f(u64 x) { return (float)__aeabi_ul2d(x); }
float __aeabi_l2f(s64 x) { return (float)__aeabi_l2d(x); }
u64 __aeabi_d2ulz(double d) {
    if (!(d > 0.0)) return 0;
    if (d >= 18446744073709551616.0) return ~0ull;
    u32 hi = (u32)(d * (1.0 / TWO32));
    double rest = d - (double)hi * TWO32;
    if (rest < 0) { hi--; rest += TWO32; }
    return ((u64)hi << 32) | (u32)rest;
}
s64 __aeabi_d2lz(double d) { return d < 0 ? -(s64)__aeabi_d2ulz(-d) : (s64)__aeabi_d2ulz(d); }
u64 __aeabi_f2ulz(float f) { return __aeabi_d2ulz(f); }
s64 __aeabi_f2lz(float f) { return __aeabi_d2lz(f); }

/* desplazamientos de 64 bits con cantidad variable (GCC los delega a estas funciones) */
typedef union { u64 v; struct { u32 hi, lo; } w; } U64BE; /* big-endian: palabra alta primero */
u64 __aeabi_llsl(u64 x, int n) {
    U64BE a = { x }, r;
    if (n >= 32) { r.w.hi = a.w.lo << (n - 32); r.w.lo = 0; }
    else if (n == 0) return x;
    else { r.w.hi = (a.w.hi << n) | (a.w.lo >> (32 - n)); r.w.lo = a.w.lo << n; }
    return r.v;
}
u64 __aeabi_llsr(u64 x, int n) {
    U64BE a = { x }, r;
    if (n >= 32) { r.w.lo = a.w.hi >> (n - 32); r.w.hi = 0; }
    else if (n == 0) return x;
    else { r.w.lo = (a.w.lo >> n) | (a.w.hi << (32 - n)); r.w.hi = a.w.hi >> n; }
    return r.v;
}
s64 __aeabi_lasr(s64 x, int n) {
    U64BE a = { (u64)x }, r;
    if (n >= 32) { r.w.lo = (u32)((s32)a.w.hi >> (n - 32)); r.w.hi = (u32)((s32)a.w.hi >> 31); }
    else if (n == 0) return x;
    else { r.w.lo = (a.w.lo >> n) | (a.w.hi << (32 - n)); r.w.hi = (u32)((s32)a.w.hi >> n); }
    return (s64)r.v;
}
u64 __aeabi_lmul(u64 a, u64 b) {
    u32 al = (u32)a, ah = (u32)(a >> 32), bl = (u32)b, bh = (u32)(b >> 32);
    u64 lo = (u64)al * bl;   /* umull: en linea */
    return lo + ((u64)(al * bh + ah * bl) << 32);
}
int __aeabi_lcmp(s64 a, s64 b) { return a < b ? -1 : a > b; }
int __aeabi_ulcmp(u64 a, u64 b) { return a < b ? -1 : a > b; }

int strcmp(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }

/* ---- printf minimo para los mensajes del port (sin '/' ni '%' sobre enteros) ---- */
typedef __builtin_va_list va_list;
#define va_arg __builtin_va_arg
static char* fmt_u(char* o, char* end, u32 v, u32 base, int width, char pad, int upper) {
    char tmp[12]; int n = 0;
    const char* dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        u32 q = 0, r = v;
        if (base == 16) { q = v >> 4; r = v & 15; } else { q = udiv32(v, 10, &r); }
        tmp[n++] = dig[r]; v = q;
    } while (v && n < 12);
    while (n < width && n < 12) tmp[n++] = pad;
    while (n && o < end) *o++ = tmp[--n];
    return o;
}
int vsnprintf(char* out, size_t cap, const char* f, va_list ap) {
    char* o = out; char* end = out + (cap ? cap - 1 : 0);
    for (; *f && o < end; f++) {
        if (*f != '%') { *o++ = *f; continue; }
        f++; char pad = ' '; int w = 0;
        if (*f == '0') { pad = '0'; f++; }
        while (*f >= '0' && *f <= '9') { w = w * 10 + (*f - '0'); f++; }
        while (*f == 'l' || *f == 'z') f++;
        switch (*f) {
            case 'd': case 'i': { s32 v = va_arg(ap, s32); if (v < 0 && o < end) { *o++ = '-'; v = -v; } o = fmt_u(o, end, (u32)v, 10, w, pad, 0); break; }
            case 'u': o = fmt_u(o, end, va_arg(ap, u32), 10, w, pad, 0); break;
            case 'x': o = fmt_u(o, end, va_arg(ap, u32), 16, w, pad, 0); break;
            case 'X': o = fmt_u(o, end, va_arg(ap, u32), 16, w, pad, 1); break;
            case 'p': o = fmt_u(o, end, (u32)va_arg(ap, void*), 16, 8, '0', 0); break;
            case 's': { const char* s = va_arg(ap, const char*); while (s && *s && o < end) *o++ = *s++; break; }
            case 'c': *o++ = (char)va_arg(ap, int); break;
            case '%': *o++ = '%'; break;
            default: if (o < end) *o++ = '?'; break;
        }
    }
    if (cap) *o = 0;
    return (int)(o - out);
}
float fabsf(float x) { return __builtin_fabsf(x); }

int snprintf(char* out, size_t cap, const char* f, ...) {
    va_list ap;
    __builtin_va_start(ap, f);
    int n = vsnprintf(out, cap, f, ap);
    __builtin_va_end(ap);
    return n;
}

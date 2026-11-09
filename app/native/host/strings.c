/* Guest string access: UTF-16LE and the Russian installation's CP1251 ANSI.
 * Explicit CP1252 helpers remain available for callers specifying that page. */
#include "host.h"

/* Windows-1252 0x80..0x9F; 0 marks the five undefined bytes, which
 * MultiByteToWideChar maps to the same-valued C1 control. */
static const uint16_t cp1252_high[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

uint16_t void_cp1252_to_u16(uint8_t c)
{
    return (c >= 0x80 && c < 0xA0) ? cp1252_high[c - 0x80] : c;
}

int void_u16_to_cp1252(uint16_t u, uint8_t *out)
{
    if (u < 0x80 || (u >= 0xA0 && u < 0x100)) { *out = (uint8_t)u; return 1; }
    for (int i = 0; i < 32; ++i) if (cp1252_high[i] == u) { *out = (uint8_t)(0x80 + i); return 1; }
    *out = '?';
    return 0;
}

static const uint16_t cp1251_high[128] = {
    0x0402,0x0403,0x201A,0x0453,0x201E,0x2026,0x2020,0x2021,0x20AC,0x2030,0x0409,0x2039,0x040A,0x040C,0x040B,0x040F,
    0x0452,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x0098,0x2122,0x0459,0x203A,0x045A,0x045C,0x045B,0x045F,
    0x00A0,0x040E,0x045E,0x0408,0x00A4,0x0490,0x00A6,0x00A7,0x0401,0x00A9,0x0404,0x00AB,0x00AC,0x00AD,0x00AE,0x0407,
    0x00B0,0x00B1,0x0406,0x0456,0x0491,0x00B5,0x00B6,0x00B7,0x0451,0x2116,0x0454,0x00BB,0x0458,0x0405,0x0455,0x0457,
    0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,0x0416,0x0417,0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,0x041F,
    0x0420,0x0421,0x0422,0x0423,0x0424,0x0425,0x0426,0x0427,0x0428,0x0429,0x042A,0x042B,0x042C,0x042D,0x042E,0x042F,
    0x0430,0x0431,0x0432,0x0433,0x0434,0x0435,0x0436,0x0437,0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,0x043F,
    0x0440,0x0441,0x0442,0x0443,0x0444,0x0445,0x0446,0x0447,0x0448,0x0449,0x044A,0x044B,0x044C,0x044D,0x044E,0x044F,
};

typedef struct { uint16_t key, value; } cp932_pair;
#include "cp932_table.inc"

static uint32_t ansi_codepage = 1251;

void void_ansi_set_codepage(uint32_t codepage)
{
    if (codepage == 932 || codepage == 1251) ansi_codepage = codepage;
}

uint32_t void_ansi_codepage(void) { return ansi_codepage; }

static uint32_t cp932_decode(const uint8_t *s, size_t remaining, size_t *used)
{
    *used = 1;
    if (s[0] < 0x80) return s[0];
    if (s[0] >= 0xA1 && s[0] <= 0xDF) return 0xFF61u + (s[0] - 0xA1);
    int lead = (s[0] >= 0x81 && s[0] <= 0x9F) || (s[0] >= 0xE0 && s[0] <= 0xFC);
    if (lead && remaining >= 2) {
        uint16_t key = (uint16_t)((s[0] << 8) | s[1]);
        uint32_t lo = 0, hi = cp932_pair_count;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            if (cp932_pairs[mid].key < key) lo = mid + 1;
            else hi = mid;
        }
        if (lo < cp932_pair_count && cp932_pairs[lo].key == key) {
            *used = 2;
            return cp932_pairs[lo].value;
        }
    }
    return cp932_single[s[0] - 0x80];
}

static int cp932_write(uint32_t cp, uint8_t *out)
{
    if (cp < 0x80) { out[0] = (uint8_t)cp; return 1; }
    if (cp >= 0xFF61 && cp <= 0xFF9F) { out[0] = (uint8_t)(0xA1 + cp - 0xFF61); return 1; }
    for (uint32_t i = 0; i < cp932_pair_count; ++i) if (cp932_pairs[i].value == cp) {
        out[0] = (uint8_t)(cp932_pairs[i].key >> 8);
        out[1] = (uint8_t)cp932_pairs[i].key;
        return 2;
    }
    for (uint8_t value = 0x80; value; ++value) if (cp932_single[value - 0x80] == cp && !(value >= 0xA1 && value <= 0xDF)) {
        out[0] = value;
        return 1;
    }
    out[0] = '?';
    return 0;
}

static int ansi_write(uint32_t cp, uint8_t *out)
{
    if (ansi_codepage == 932) return cp932_write(cp, out);
    return void_u16_to_ansi(cp > 0xFFFF ? 0xFFFD : (uint16_t)cp, out);
}

uint16_t void_ansi_to_u16(uint8_t c)
{
    if (ansi_codepage == 932) {
        size_t used;
        return (uint16_t)cp932_decode(&c, 1, &used);
    }
    return c < 0x80 ? c : cp1251_high[c - 0x80];
}

int void_u16_to_ansi(uint16_t u, uint8_t *out)
{
    if (u < 0x80) { *out = (uint8_t)u; return 1; }
    if (ansi_codepage == 932) {
        int written = cp932_write(u, out);
        if (written == 1) return 1;
        out[0] = '?';
        return 0;
    }
    for (int i = 0; i < 128; ++i) if (cp1251_high[i] == u) { *out = (uint8_t)(0x80 + i); return 1; }
    *out = '?';
    return 0;
}

uint8_t void_ansi_lower(uint8_t c)
{
    if (ansi_codepage == 932) return c >= 'A' && c <= 'Z' ? (uint8_t)(c + 32) : c;
    uint16_t u = void_ansi_to_u16(c);
    if (u >= 'A' && u <= 'Z') u += 32;
    else if (u >= 0x0410 && u <= 0x042F) u += 32;
    else if (u >= 0x0400 && u <= 0x040F) u += 80;
    else if (u == 0x0490) u = 0x0491;
    uint8_t out;
    void_u16_to_ansi(u, &out);
    return out;
}

size_t void_utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F)); return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Decode one UTF-8 code point; invalid bytes decode as U+FFFD. */
static uint32_t utf8_next(const unsigned char **p)
{
    const unsigned char *s = *p;
    uint32_t c = *s++;
    if (c >= 0x80) {
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
        if (extra < 0) { *p = s; return 0xFFFD; }
        c &= 0x3Fu >> extra;
        for (int i = 0; i < extra; ++i) {
            if ((*s & 0xC0) != 0x80) { *p = s; return 0xFFFD; }
            c = (c << 6) | (*s++ & 0x3F);
        }
    }
    *p = s;
    return c;
}

size_t void_gstrlen(uint32_t va)
{
    return va ? strlen((const char *)GPTR(va)) : 0;
}

size_t void_gwcslen(uint32_t va)
{
    size_t n = 0;
    if (!va) return 0;
    while (MEM16(va + 2u * (uint32_t)n)) ++n;
    return n;
}

char *void_wstr_utf8(uint32_t va)
{
    if (!va) return NULL;
    size_t n = void_gwcslen(va);
    char *out = (char *)malloc(n * 3 + 1), *w = out;
    for (size_t i = 0; i < n; ++i) {
        uint32_t c = MEM16(va + 2u * (uint32_t)i);
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) {
            uint32_t low = MEM16(va + 2u * (uint32_t)(i + 1));
            if (low >= 0xDC00 && low < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00); ++i; }
        }
        w += void_utf8_encode(c, w);
    }
    *w = 0;
    return out;
}

char *void_astr_utf8(uint32_t va)
{
    if (!va) return NULL;
    size_t n = void_gstrlen(va);
    const uint8_t *s = (const uint8_t *)GPTR(va);
    char *out = (char *)malloc(n * 3 + 1), *w = out;
    if (ansi_codepage == 932) {
        for (size_t i = 0; i < n;) {
            size_t used;
            uint32_t cp = cp932_decode(s + i, n - i, &used);
            i += used;
            w += void_utf8_encode(cp, w);
        }
    } else {
        for (size_t i = 0; i < n; ++i) w += void_utf8_encode(void_ansi_to_u16(s[i]), w);
    }
    *w = 0;
    return out;
}

uint32_t void_utf8_to_gw(const char *s, uint32_t dst, uint32_t cap)
{
    uint32_t need = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) { uint32_t c = utf8_next(&p); need += c >= 0x10000 ? 2 : 1; }
    if (need + 1 > cap) return need + 1;
    uint32_t i = 0;
    p = (const unsigned char *)s;
    while (*p) {
        uint32_t c = utf8_next(&p);
        if (c >= 0x10000) {
            c -= 0x10000;
            MEM16(dst + 2 * i++) = (uint16_t)(0xD800 + (c >> 10));
            MEM16(dst + 2 * i++) = (uint16_t)(0xDC00 + (c & 0x3FF));
        } else {
            MEM16(dst + 2 * i++) = (uint16_t)c;
        }
    }
    MEM16(dst + 2 * i) = 0;
    return i;
}

uint32_t void_utf8_to_ga(const char *s, uint32_t dst, uint32_t cap)
{
    uint32_t need = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        uint8_t encoded[2];
        int written = ansi_write(cp > 0xFFFF ? 0xFFFD : cp, encoded);
        need += written > 0 ? (uint32_t)written : 1u;
    }
    if (need + 1 > cap) return need + 1;
    uint32_t i = 0;
    p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        uint8_t encoded[2];
        int written = ansi_write(cp > 0xFFFF ? 0xFFFD : cp, encoded);
        if (written <= 0) written = 1;
        for (int k = 0; k < written; ++k) MEM8(dst + i++) = encoded[k];
    }
    MEM8(dst + i) = 0;
    return i;
}

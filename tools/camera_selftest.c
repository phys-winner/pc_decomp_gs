/* Exercise the production camera transition math without a guest VM or GL context. */
#include "../app/native/host/grief_script.c"
#include <assert.h>

int main(void)
{
    uint8_t closure[100] = {0xFA, 0xFA, 'R', 'I', 'Q', 'S', 1, 0, 0, 0,
        'T', 'R', 'A', 'P', 0x10, 0, 0, 8};
    const char *name = "conv_tmp/sq_tmp_bulletBoss.nut";
    uint32_t length = (uint32_t)strlen(name);
    memcpy(closure + 18, &length, 4);
    memcpy(closure + 22, name, length);
    assert(closure_source(closure, sizeof closure, name));
    assert(!closure_source(closure, 22 + length - 1, name));
    closure[22] = 'x';
    memcpy(closure + 60, name, length);
    assert(!closure_source(closure, sizeof closure, name));
    uint8_t script[48] = {0};
    uint32_t value;
    const uint8_t allocation[] = {0x20, 3, 0, 0, 2, 3, 0, 0, 0x58, 2, 0, 0, 2, 4, 0, 0};
    memcpy(script, allocation, sizeof allocation);
    memcpy(script + 16, allocation, sizeof allocation); script[21] = 0;
    memcpy(script + 32, allocation, sizeof allocation);
    for (unsigned i = 0; i < sizeof script; ++i) script[i] ^= 0xAF;
    assert(render_widths(script, sizeof script, 0xAF, 1050) == 2);
    for (unsigned i = 0; i < sizeof script; ++i) script[i] ^= 0xAF;
    memcpy(&value, script, 4); assert(value == 1050);
    memcpy(&value, script + 16, 4); assert(value == 800);
    memcpy(&value, script + 32, 4); assert(value == 1050);
    assert(script[5] == 3 && script[37] == 3 && script[8] == 0x58);
    const uint8_t loading_original[] = {
        0x20, 3, 0, 0, 2, 2, 0, 0, 0x44, 3, 0, 0, 2, 2, 0, 0,
        0x88, 2, 0, 0, 2, 2, 0, 0, 0x88, 2, 0, 0, 2, 2, 0, 0,
        0x88, 2, 0, 0, 2, 3, 0, 0
    };
    uint8_t loading[sizeof loading_original], unchanged[sizeof loading_original];
    for (unsigned key = 0; key <= 0xAF; key += 0xAF) {
        for (unsigned i = 0; i < sizeof loading; ++i) loading[i] = loading_original[i] ^ key;
        memcpy(unchanged, loading, sizeof loading);
        assert(!loading_widths(loading, 24, key, 2134));
        assert(!memcmp(loading, unchanged, sizeof loading));
        loading[29] ^= 1;
        memcpy(unchanged, loading, sizeof loading);
        assert(!loading_widths(loading, sizeof loading, key, 2134));
        assert(!memcmp(loading, unchanged, sizeof loading));
        loading[29] ^= 1;
        assert(loading_widths(loading, sizeof loading, key, 2134) == 4);
        for (unsigned i = 0; i < sizeof loading; ++i) loading[i] ^= key;
        memcpy(&value, loading, 4); assert(value == 2134);
        memcpy(&value, loading + 8, 4); assert(value == 2170);
        memcpy(&value, loading + 16, 4); assert(value == 1982);
        memcpy(&value, loading + 24, 4); assert(value == 1982);
        assert(!memcmp(loading + 32, loading_original + 32, 8));
    }
    float camera[77] = {0};
    camera[232 / 4] = 2400;
    camera[248 / 4] = 12;
    camera[236 / 4] = 600;
    camera[244 / 4] = 472;
    camera_viewport(camera, 1050);
    assert(camera[240 / 4] == 525 && camera[256 / 4] == 1050);
    assert(camera[292 / 4] == 1875 && camera[300 / 4] == 2925);
    assert(camera[216 / 4] == -1887);
    assert(camera[236 / 4] == 600 && camera[244 / 4] == 472);
    camera_viewport(camera, 800);
    assert(camera[240 / 4] == 400 && camera[256 / 4] == 800);
    assert(camera[292 / 4] == 2000 && camera[300 / 4] == 2800);
    assert(camera[216 / 4] == -2012);
    puts("PASS: encrypted render/loading widths, atomic four-callback patch, camera center, world bounds, translation and original 800-wide restoration");
    return 0;
}

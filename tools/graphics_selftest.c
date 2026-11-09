/* Test production quad geometry without a GL context. */
#include "../app/native/host/grief_script.c"
#include "../app/native/host/widescreen_layout.h"
#include <assert.h>

static int paused, result_overlay, victory_state = -1, lan, images, frees, uploads;
static uint8_t uploaded[4];
static d3d_texture art_storage[2];
int void_grief_menu_pause_visible(void) { return paused; }
int void_grief_menu_result_visible(void) { return result_overlay; }
int void_grief_widescreen_state(int *alpha) { if (alpha) *alpha = 1000; return victory_state; }
int void_grief_lan_active(void) { return lan; }
uint8_t *void_grief_widescreen_image(unsigned id, uint32_t *width, uint32_t *height)
{
    ++images;
    *width = 1000; *height = 600;
    uint8_t *pixels = calloc(1000u * 600u, 4);
    pixels[0] = (uint8_t)id; pixels[1] = 20; pixels[2] = 30; pixels[3] = 255;
    return pixels;
}
d3d_texture *d3d_texture_new(d3d_device *dev, uint32_t rtype, uint32_t w, uint32_t h, uint32_t d,
    uint32_t levels, uint32_t usage, uint32_t format, uint32_t pool)
{
    (void)dev; (void)d;
    assert(rtype == D3DRTYPE_TEXTURE && levels == 1 && usage == 0 && format == 21 && pool == D3DPOOL_DEFAULT);
    d3d_texture *t = &art_storage[images & 1];
    memset(t, 0, sizeof *t);
    t->rtype = rtype; t->width = w; t->height = h; t->format = format;
    return t;
}
void d3d_texture_upload_level(d3d_texture *t, uint32_t face, uint32_t level, const uint8_t *data, uint32_t pitch)
{
    assert(t && !face && !level && pitch == t->width * 4u);
    memcpy(uploaded, data, 4);
    ++uploads;
}
void d3d_texture_free(d3d_texture *t) { assert(t); ++frees; }

static void quad(uint8_t out[112], float left, float right, float top, float bottom)
{
    memset(out, 0, 112);
    for (unsigned i = 0; i < 4; ++i) {
        float x = (i & 1) ? right : left, y = (i & 2) ? bottom : top;
        float u = (i & 1) ? 1.0f : 0.0f, v = (i & 2) ? 1.0f : 0.0f;
        memcpy(out + i * 28, &x, 4); memcpy(out + i * 28 + 4, &y, 4);
        memcpy(out + i * 28 + 20, &u, 4); memcpy(out + i * 28 + 24, &v, 4);
    }
}
static float x_at(const uint8_t *data, unsigned i)
{ float x; memcpy(&x, data + i * 28, 4); return x; }
static float u_at(const uint8_t *data, unsigned i)
{ float u; memcpy(&u, data + i * 28 + 20, 4); return u; }
static int near(float a, float b) { return fabsf(a - b) < 0.0005f; }

int main(void)
{
    float left, right;
    widescreen_source_span(2400, 600, 2400, 600, &left, &right);
    assert(left == 0 && right == 2400);
    widescreen_source_span(1600, 900, 2400, 600, &left, &right);
    assert(fabsf((right - left) - 1600.0f / 1.5f) < 0.001f);
    assert((left + right) * 0.5f == 1200);
    /* At the centered 800-pixel scene's edges both crops meet exactly. */
    assert(fabsf((left + (right - left) * 0.125f) - 800) < 0.001f);
    assert(fabsf((left + (right - left) * 0.875f) - 1600) < 0.001f);
    widescreen_source_span(6000, 600, 2400, 600, &left, &right);
    assert(left == -1800 && right == 4200);
    d3d_device device = {0};
    d3d_surface surface = {0};
    d3d_texture texture = {0};
    device.rt[0] = device.backbuffer = &surface;
    surface.width = 1100; surface.height = 600;
    uint8_t original[112], output[112];
    quad(original, -0.5f, 799.5f, -0.5f, 599.5f);
    assert(void_grief_quad_adjust(&device, original, 28, output));
    assert(x_at(output, 0) == -0.5f && x_at(output, 1) == 1099.5f);
    assert(x_at(original, 1) == 799.5f);
    device.st.textures[0] = &texture;
    texture.width = texture.height = 1;
    assert(void_grief_quad_adjust(&device, original, 28, output));
    texture.width = 1100; texture.height = 600; texture.usage = D3DUSAGE_RENDERTARGET;
    assert(void_grief_quad_adjust(&device, original, 28, output));
    texture.usage = 0;
    assert(!void_grief_quad_adjust(&device, original, 28, output));
    quad(original, 700, 740, 200, 240);
    assert(!void_grief_quad_adjust(&device, original, 28, output));
    assert(void_grief_hud_offset(&device, 700) == 0.0f);
    paused = 1;
    ++device.frame;
    assert(!void_grief_quad_adjust(&device, original, 28, output));
    uint8_t dimmer[112];
    quad(dimmer, -10, 810, -10, 610);
    texture.width = texture.height = 1;
    assert(void_grief_quad_adjust(&device, dimmer, 28, output));
    texture.width = 1100; texture.height = 600;
    assert(void_grief_quad_adjust(&device, original, 28, output));
    assert(x_at(output, 0) == 850 && x_at(output, 1) == 890);
    device.backbuffer = NULL;
    assert(!void_grief_quad_adjust(&device, original, 28, output));
    device.backbuffer = &surface;
    ++device.frame;
    assert(!void_grief_quad_adjust(&device, original, 28, output));
    /* A victory result centers its 800-pixel layer after the full-width dimmer, like pause. */
    paused = 0; result_overlay = 1;
    ++device.frame;
    assert(!void_grief_quad_adjust(&device, original, 28, output));
    texture.width = texture.height = 1;
    assert(void_grief_quad_adjust(&device, dimmer, 28, output));
    texture.width = 1100; texture.height = 600;
    assert(void_grief_quad_adjust(&device, original, 28, output));
    assert(x_at(output, 0) == 850 && x_at(output, 1) == 890);
    result_overlay = 0;
    ++device.frame;

    /* Victory backgrounds: an 800x600 picture centered in a 1334-wide scene target. */
    d3d_surface scene = {0};
    scene.width = 1334; scene.height = 600;
    device.rt[0] = &scene;
    texture.rtype = D3DRTYPE_TEXTURE; texture.width = 800; texture.height = 600; texture.usage = 0;
    uint8_t picture[112], victory[112];
    quad(picture, 267, 1067, 0, 600);
    assert(!void_grief_victory_quad(&device, picture, 28, victory));
    victory_state = 2; lan = 1;
    assert(!void_grief_victory_quad(&device, picture, 28, victory));
    lan = 0;
    assert(!void_grief_victory_quad(&device, picture, 24, victory));
    widescreen_mounted = 0;
    /* Without the bank the original picture continues mirrored past its edges. */
    assert(void_grief_victory_quad(&device, picture, 28, victory) == &texture && images == 0);
    assert(x_at(victory, 0) == 0 && x_at(victory, 1) == 1334 && x_at(victory, 2) == 0 && x_at(victory, 3) == 1334);
    assert(near(u_at(victory, 0), -267.0f / 800) && near(u_at(victory, 1), 1067.0f / 800));
    assert(!memcmp(victory + 4, picture + 4, 16) && !memcmp(victory + 24, picture + 24, 4));
    void_grief_victory_release();
    widescreen_mounted = 1;
    d3d_texture *art = void_grief_victory_quad(&device, picture, 28, victory);
    assert(art && art->width == 1000 && art->height == 600 && images == 1 && uploads == 1);
    /* The bank image arrives as RGBA and returns to A8R8G8B8 memory order. */
    assert(uploaded[0] == 30 && uploaded[1] == 20 && uploaded[2] == 2 && uploaded[3] == 255);
    /* The panorama's central 800 pixels coincide with the picture. */
    assert(near(u_at(victory, 0), (100.0f - 267) / 1000) && near(u_at(victory, 1), (100.0f + 1067) / 1000));
    assert(near(u_at(victory, 2), u_at(victory, 0)) && near(u_at(victory, 3), u_at(victory, 1)));
    /* A mirrored sprite maps the panorama the other way round. */
    uint8_t flipped[112];
    memcpy(flipped, picture, 112);
    for (unsigned i = 0; i < 4; ++i) { float u = 1.0f - u_at(picture, i); memcpy(flipped + i * 28 + 20, &u, 4); }
    assert(void_grief_victory_quad(&device, flipped, 28, victory) == art && images == 1);
    assert(near(u_at(victory, 0), (900.0f + 267) / 1000) && near(u_at(victory, 1), (900.0f - 1067) / 1000));
    /* Other sprites, rotated quads and narrower targets keep their own draw. */
    uint8_t small[112];
    quad(small, 267, 667, 0, 600);
    assert(!void_grief_victory_quad(&device, small, 28, victory));
    uint8_t rotated[112];
    memcpy(rotated, picture, 112);
    float skew = 300;
    memcpy(rotated + 28, &skew, 4);
    assert(!void_grief_victory_quad(&device, rotated, 28, victory));
    texture.width = 1;
    assert(!void_grief_victory_quad(&device, picture, 28, victory));
    texture.width = 800;
    scene.width = 800;
    assert(!void_grief_victory_quad(&device, picture, 28, victory));
    scene.width = 1334;
    /* Another victory replaces the cached art; leaving victories releases it once. */
    victory_state = 3;
    assert(void_grief_victory_quad(&device, picture, 28, victory) && images == 2 && frees == 1);
    void_grief_victory_release();
    void_grief_victory_release();
    assert(frees == 2);
    puts("PASS: full-width fades/composites, centered pause and victory result layers, immutable vertices, world sprites and HUD stay in place; victory picture spans the widened target with aligned bank art or mirrored original; panorama crop preserves 800-pixel center and outer-edge tiling covers arbitrary widths");
    return 0;
}

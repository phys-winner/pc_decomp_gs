#include "host.h"
#include "grief_menu.h"
#include "grief_widescreen.h"
#include "d3d9_internal.h"
#include <math.h>
#include "../../../generated/port_scripts.inc"

#define COMPILE_BUFFER 0x0052E270u
#define CAMERA_INIT 0x0044DBD0u
#define RENDER_CONFIG 0x004C59F0u

static void_fn compile_buffer, camera_init, render_config;
static int widescreen_mount_attempted, widescreen_mounted;
static void mount_widescreen(void);
int void_grief_lan_active(void);
int void_grief_lan_local_player(void);
int void_grief_lan_participants(void);

static void port_menu_call(void)
{
    void_regs saved;
    void_regs_save(&saved);
    uint32_t vm = ARG(0), scratch = void_galloc(24);
    uint32_t args[] = { vm, 2, scratch };
    int valid = (int32_t)void_call_guest(0x0052BA90u, 3, args, 0) >= 0;
    int command = (int)MEM32(scratch);
    args[1] = 3; args[2] = scratch + 4;
    valid &= (int32_t)void_call_guest(command == 3 ? 0x0052BB90u : 0x0052BA90u, 3, args, 0) >= 0;
    int void_grief_menu_script(int command, int value);
    int result = 0;
    if (valid && command == 3) void_log("[script] %.1024s", (const char *)GPTR(MEM32(scratch + 4)));
    else if (valid && command == 9) result = void_grief_widescreen_mounted();
    else if (valid && command == 11) result = void_grief_lan_local_player();
    else if (valid && command == 12) result = void_grief_lan_participants();
    else if (valid && command == 4) {
        for (unsigned i = 0; i < 4; ++i) {
            args[1] = 4 + i; args[2] = scratch + 8 + 4 * i;
            valid &= (int32_t)void_call_guest(0x0052BA90u, 3, args, 0) >= 0;
        }
        void void_grief_menu_rect(int id, const int rect[4]);
        if (valid) void_grief_menu_rect((int)MEM32(scratch + 4), (const int *)GPTR(scratch + 8));
    }
    else if (valid) {
        result = void_grief_menu_script(command, (int)MEM32(scratch + 4));
    }
    void_gfree(scratch);
    uint32_t output[] = { vm, (uint32_t)result };
    void_call_guest(0x0052B7B0u, 2, output, 0);
    void_regs_load(&saved);
    g_eax = 1;
    g_esp += 4;
}

static void register_port_menu(uint32_t vm)
{
    static uint32_t registered_vm;
    if (registered_vm == vm) return;
    void_regs saved;
    void_regs_save(&saved);
    uint32_t name = void_gstrdup("PortMenu"), top = void_call_guest(0x0052BC80u, 1, &vm, 0);
    void_call_guest(0x0052B930u, 1, &vm, 0);
    uint32_t push[] = { vm, name, 0xFFFFFFFFu };
    void_call_guest(0x0052B740u, 3, push, 0);
    uint32_t closure[] = { vm, void_thunk(port_menu_call, "PortMenu"), 0 };
    void_call_guest(0x0052EA10u, 3, closure, 0);
    uint32_t slot[] = { vm, (uint32_t)-3, 0 };
    if ((int32_t)void_call_guest(0x0052DBC0u, 3, slot, 0) < 0) void_fatal("Cannot register PortMenu");
    uint32_t pop[] = { vm, 1 };
    void_call_guest(0x0052BC90u, 2, pop, 0);
    if (void_call_guest(0x0052BC80u, 1, &vm, 0) != top) void_fatal("PortMenu registration stack imbalance");
    void_gfree(name);
    void void_grief_tv_register(uint32_t vm);
    void_grief_tv_register(vm);
    registered_vm = vm;
    void_regs_load(&saved);
}

void void_grief_render_size(uint32_t *width, uint32_t *height)
{
    int sw = 1280, sh = 720;
    void_android_display_size(&sw, &sh);
    uint32_t w = sh > 0 ? (600u * (uint32_t)sw + (uint32_t)sh / 2u) / (uint32_t)sh : 1300;
    *width = w < 800 ? 800 : (w + 1u) & ~1u;
    *height = 600;
}

static int source_contains(const uint8_t *source, uint32_t size, const char *needle)
{
    size_t n = strlen(needle);
    if (!source || size < n) return 0;
    for (uint32_t i = 0; i + n <= size; ++i)
        if (!memcmp(source + i, needle, n)) return 1;
    return 0;
}

static int closure_source(const uint8_t *source, uint32_t size, const char *name)
{
    static const uint8_t header[] = {0xFA, 0xFA, 'R', 'I', 'Q', 'S', 1, 0, 0, 0,
        'T', 'R', 'A', 'P', 0x10, 0, 0, 8};
    if (size < 22 || memcmp(source, header, sizeof header)) return 0;
    uint32_t length;
    memcpy(&length, source + 18, 4);
    return length <= size - 22 && length == strlen(name) && !memcmp(source + 22, name, length);
}

static void camera_viewport(float *camera, uint32_t width)
{
    camera[240 / 4] = width * 0.5f;
    camera[256 / 4] = (float)width;
    camera[292 / 4] = camera[232 / 4] - camera[240 / 4];
    camera[300 / 4] = camera[292 / 4] + width;
    camera[216 / 4] = -camera[292 / 4] - camera[248 / 4];
}

static void camera_apply(void)
{
    if (!void_vm_readable(0x006A9A84u, 4)) return;
    uint32_t base = MEM32(0x006A9A84u);
    if (!base || !void_vm_readable(base + 604 + 300, 4)) return;
    uint32_t object = base + 604;
    uint32_t width = 800, height;
    /* GSOnline simulation uses camera bounds; both peers need the original 800-wide camera. */
    if (void_grief_menu_wide() && !void_grief_lan_active()) void_grief_render_size(&width, &height);
    /* Keep bounds and transform coherent when switching scenes away from world x=0. */
    camera_viewport((float *)GPTR(object), width);
    void_log("[viewport] native Camera2D width %u object %08X active %d", width, object, void_grief_menu_wide());
}

void void_grief_camera_scene(void)
{
    camera_apply();
}

static void grief_camera_init(void)
{
    uint32_t caller = g_cur_func;
    camera_init();
    camera_apply();
    g_cur_func = caller;
}

static unsigned render_widths(uint8_t *source, uint32_t size, uint8_t key, uint32_t width)
{
    static const uint8_t allocation[] = {0x20, 3, 0, 0, 2, 3, 0, 0, 0x58, 2, 0, 0, 2, 4, 0, 0};
    unsigned patched = 0;
    for (uint32_t i = 0; i + sizeof allocation <= size; ++i) {
        unsigned j = 0;
        while (j < sizeof allocation && (source[i + j] ^ key) == allocation[j]) ++j;
        if (j != sizeof allocation) continue;
        uint32_t value = width ^ ((uint32_t)key * 0x01010101u);
        memcpy(source + i, &value, 4);
        ++patched;
    }
    return patched;
}

static unsigned loading_widths(uint8_t *source, uint32_t size, uint8_t key, uint32_t width)
{
    const uint32_t old[] = {800, 836, 648}, values[] = {width, width + 36, width - 152};
    const unsigned expected[] = {1, 1, 2};
    unsigned counts[3] = {0}, kinds[4], total = 0;
    uint32_t offsets[4], mask = (uint32_t)key * 0x01010101u;
    if (width < 800) return 0;
    for (uint32_t i = 0; i + 8 <= size; ++i) {
        if ((source[i + 4] ^ key) != 2 || (source[i + 5] ^ key) != 2 ||
            (source[i + 6] ^ key) || (source[i + 7] ^ key)) continue;
        uint32_t value;
        memcpy(&value, source + i, 4);
        for (unsigned kind = 0; kind < 3; ++kind) if ((value ^ mask) == old[kind]) {
            if (++counts[kind] > expected[kind] || total == 4) return 0;
            offsets[total] = i;
            kinds[total++] = kind;
        }
    }
    if (total != 4 || counts[0] != 1 || counts[1] != 1 || counts[2] != 2) return 0;
    /* Patch the original cached animation callbacks only after all four match. */
    for (unsigned i = 0; i < total; ++i) {
        uint32_t value = values[kinds[i]] ^ mask;
        memcpy(source + offsets[i], &value, 4);
    }
    return total;
}

void void_grief_camera_stream(uint32_t address, uint32_t size)
{
    if (size < 64 || size > 65536) return;
    uint8_t key = MEM8(address) ^ 0xFAu, decoded[64];
    for (unsigned i = 0; i < sizeof decoded; ++i) decoded[i] = MEM8(address + i) ^ key;
    if (source_contains(decoded, sizeof decoded, "conv_tmp/sq_tmp_NowLoading.global.nut")) {
        uint32_t width, height;
        void_grief_render_size(&width, &height);
        unsigned patched = loading_widths(GPTR(address), size, key, width);
        void_log("[viewport] loading width %u, %u/4 positions", width, patched);
        return;
    }
    if (source_contains(decoded, sizeof decoded, "conv_tmp/sq_tmp_render.nut")) {
        uint32_t width, height;
        void_grief_render_size(&width, &height);
        /* The engine's render-target size must match the expanded D3D backing texture. */
        unsigned patched = render_widths(GPTR(address), size, key, width);
        void_log("[viewport] render targets width %u, %u/2 allocations", width, patched);
        return;
    }
    if (!source_contains(decoded, sizeof decoded, "conv_tmp/sq_tmp_camera.nut")) return;
    uint32_t width = 800, height, mask = (uint32_t)key * 0x01010101u;
    if (void_grief_menu_wide() && !void_grief_lan_active()) void_grief_render_size(&width, &height);
    unsigned patched = 0;
    for (uint32_t i = 0; i + 8 <= size; ++i) {
        if ((MEM32(address + i) ^ mask) == 400 && (MEM8(address + i + 4) ^ key) == 2 &&
            (MEM8(address + i + 6) ^ key) == 0 && (MEM8(address + i + 7) ^ key) == 0) {
            MEM32(address + i) = (width / 2) ^ mask;
            ++patched;
        }
    }
    void_log("[viewport] Camera2D stream width %u, %u/5 centers", width, patched);
}

float void_grief_hud_offset(const d3d_device *dev, float left)
{
    (void)dev; (void)left;
    return 0.0f;
}

int void_grief_quad_adjust(const d3d_device *dev, const void *vertices, uint32_t stride, uint8_t out[112])
{
    static const d3d_device *overlay_device;
    static uint32_t overlay_frame;
    static int pause_overlay;
    if (overlay_device != dev || overlay_frame != dev->frame) {
        overlay_device = dev;
        overlay_frame = dev->frame;
        pause_overlay = 0;
    }
    const d3d_surface *target = dev->rt[0];
    if (!vertices || stride != 28 || !target || target->width <= 800 || target->height != 600) return 0;
    float x[4], y[4], u[4], v[4], minx = INFINITY, maxx = -INFINITY, miny = INFINITY, maxy = -INFINITY;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *at = (const uint8_t *)vertices + i * stride;
        memcpy(&x[i], at, 4); memcpy(&y[i], at + 4, 4);
        memcpy(&u[i], at + 20, 4); memcpy(&v[i], at + 24, 4);
        minx = fminf(minx, x[i]); maxx = fmaxf(maxx, x[i]);
        miny = fminf(miny, y[i]); maxy = fmaxf(maxy, y[i]);
    }
    const d3d_texture *texture = (const d3d_texture *)dev->st.textures[0];
    int composite = texture && (texture->usage & D3DUSAGE_RENDERTARGET) &&
        texture->width == target->width && texture->height == target->height;
    float min_u = u[0], max_u = u[0], min_v = v[0], max_v = v[0];
    for (unsigned i = 1; i < 4; ++i) {
        min_u = fminf(min_u, u[i]); max_u = fmaxf(max_u, u[i]);
        min_v = fminf(min_v, v[i]); max_v = fmaxf(max_v, v[i]);
    }
    int solid = !texture || ((max_u - min_u) * texture->width <= 4.01f &&
        (max_v - min_v) * texture->height <= 4.01f);
    int full = minx <= 0.5f && minx >= -11 && maxx >= 799 && maxx <= 821 && miny <= 0.5f && maxy >= 599;
    int void_grief_menu_pause_visible(void);
    int void_grief_menu_result_visible(void);
    /* The dimmer begins the pause or victory result layer; world sprites and HUD were drawn before it. */
    if (full && solid && target == dev->backbuffer &&
        (void_grief_menu_pause_visible() || void_grief_menu_result_visible())) pause_overlay = 1;
    int center = !full && target == dev->backbuffer && pause_overlay;
    if (!(full && (solid || composite)) && !center) return 0;
    memcpy(out, vertices, 112);
    float extra = (float)(target->width - 800);
    for (unsigned i = 0; i < 4; ++i) {
        float adjusted = center ? x[i] + extra * 0.5f : x[i] > 400 ? x[i] + extra : x[i];
        memcpy(out + i * stride, &adjusted, 4);
        if (composite && !center) {
            float tex_u = x[i] > 400 ? 1.0f : 0.0f;
            memcpy(out + i * stride + 20, &tex_u, 4);
        }
    }
    return 1;
}

static d3d_texture *victory_art;
static const d3d_device *victory_device;
static int victory_id = -1;

void void_grief_victory_release(void)
{
    if (victory_art) d3d_texture_free(victory_art);
    victory_art = NULL;
    victory_device = NULL;
    victory_id = -1;
}

/* The bank panorama of a victory; its central 800x600 crop is the background sprite itself. */
static d3d_texture *victory_texture(d3d_device *dev, int id)
{
    if (victory_device == dev && victory_id == id) return victory_art;
    void_grief_victory_release();
    victory_device = dev;
    victory_id = id;
    if (!void_grief_widescreen_mounted()) return NULL;
    uint32_t width, height;
    uint8_t *pixels = void_grief_widescreen_image((unsigned)id, &width, &height);
    if (!pixels) return NULL;
    if (height == 600 && width >= 800 && !(width & 1)) {
        /* RGBA rows back to the A8R8G8B8 memory order of the original sprite texture. */
        for (size_t i = 0; i < (size_t)width * height * 4u; i += 4) {
            uint8_t red = pixels[i];
            pixels[i] = pixels[i + 2];
            pixels[i + 2] = red;
        }
        victory_art = d3d_texture_new(dev, D3DRTYPE_TEXTURE, width, height, 1, 1, 0, 21, D3DPOOL_DEFAULT);
        if (victory_art) d3d_texture_upload_level(victory_art, 0, 0, pixels, width * 4u);
    }
    free(pixels);
    return victory_art;
}

/* Single-player victories cover the old arena with an 800x600 world sprite. On the widened canvas
 * the same quad spans the whole target with the bank panorama, or the original picture mirrored
 * when the bank is absent; LAN keeps the original 800-wide camera and presentation. */
d3d_texture *void_grief_victory_quad(d3d_device *dev, const void *vertices, uint32_t stride, uint8_t out[112])
{
    d3d_texture *texture = (d3d_texture *)dev->st.textures[0];
    const d3d_surface *target = dev->rt[0];
    if (!vertices || stride != 28 || !texture || texture->rtype != D3DRTYPE_TEXTURE || texture->width != 800 ||
        texture->height != 600 || (texture->usage & D3DUSAGE_RENDERTARGET) || !target || target->width <= 800 ||
        target->height != 600 || void_grief_lan_active()) return NULL;
    int id = void_grief_widescreen_state(NULL);
    if (id < 2 || id > 6) return NULL;
    float x[4], u[4], minx = INFINITY, maxx = -INFINITY, miny = INFINITY, maxy = -INFINITY;
    float min_u = INFINITY, max_u = -INFINITY, min_v = INFINITY, max_v = -INFINITY;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *at = (const uint8_t *)vertices + i * stride;
        float y, v;
        memcpy(&x[i], at, 4); memcpy(&y, at + 4, 4);
        memcpy(&u[i], at + 20, 4); memcpy(&v, at + 24, 4);
        minx = fminf(minx, x[i]); maxx = fmaxf(maxx, x[i]);
        miny = fminf(miny, y); maxy = fmaxf(maxy, y);
        min_u = fminf(min_u, u[i]); max_u = fmaxf(max_u, u[i]);
        min_v = fminf(min_v, v); max_v = fmaxf(max_v, v);
    }
    if (fabsf(maxx - minx - 800.0f) > 1.0f || fabsf(maxy - miny - 600.0f) > 1.0f || min_u < -0.01f ||
        max_u > 1.01f || max_u - min_u < 0.99f || min_v < -0.01f || max_v > 1.01f || max_v - min_v < 0.99f) return NULL;
    float left_u = 0.0f, right_u = 0.0f;
    unsigned left = 0, right = 0, at_left[4];
    for (unsigned i = 0; i < 4; ++i) {
        at_left[i] = fabsf(x[i] - minx) < 0.01f;
        if (at_left[i]) { left_u += u[i]; ++left; }
        else if (fabsf(x[i] - maxx) < 0.01f) { right_u += u[i]; ++right; }
    }
    if (left != 2 || right != 2) return NULL;
    left_u *= 0.5f;
    right_u *= 0.5f;
    d3d_texture *art = victory_texture(dev, id);
    float art_width = art ? (float)art->width : 800.0f, center = (art_width - 800.0f) * 0.5f;
    memcpy(out, vertices, 112);
    for (unsigned i = 0; i < 4; ++i) {
        float edge = at_left[i] ? 0.0f : (float)target->width;
        float picture = left_u + (edge - minx) / (maxx - minx) * (right_u - left_u);
        float art_u = (center + picture * 800.0f) / art_width;
        memcpy(out + i * stride, &edge, 4);
        memcpy(out + i * stride + 20, &art_u, 4);
    }
    return art ? art : texture;
}

void void_grief_compile_buffer(void)
{
    uint32_t caller = g_cur_func;
    uint32_t source = ARG(1), size = ARG(2);
    static unsigned capture_serial;
    if (getenv("VOID_CAPTURE_SCRIPTS") && source && size && size <= (1u << 20) && capture_serial < 128 &&
        void_vm_readable(source, size)) {
        char path[1200];
        snprintf(path, sizeof path, "%s/compile_%03u_%u.nut", void_data_dir(), capture_serial, size);
        FILE *file = fopen(path, "wb");
        if (file) {
            fwrite(GPTR(source), 1, size, file);
            fclose(file);
            void_log("[scene] captured %s", path);
        }
        ++capture_serial;
    }
    if (source && size && size < 1024 && void_vm_readable(source, size))
        void_grief_menu_scene((const char *)GPTR(source), size);
    compile_buffer();
    g_cur_func = caller;
}

static void_fn read_closure;

void grief_read_closure(void)
{
    /* Direct lifted WinMain calls bypass dispatch hooks; this callback runs before scene assets. */
    mount_widescreen();
    uint32_t caller = g_cur_func;
    static unsigned serial;
    if (getenv("VOID_CAPTURE_CLOSURES") && serial < 64) {
        uint32_t context = ARG(2);
        if (void_vm_readable(context, 12)) {
            uint32_t start = MEM32(context + 8), size = MEM32(context + 4);
            void_log("[scene] closure context %08X start %08X size %u", context, start, size);
            if (start && size && size <= (2u << 20) && void_vm_readable(start, size)) {
                char path[1200];
                snprintf(path, sizeof path, "%s/closure_%03u.cv4", void_data_dir(), serial);
                FILE *file = fopen(path, "wb");
                if (file) {
                    fwrite(GPTR(start), 1, size, file);
                    fclose(file);
                    void_log("[scene] captured %s", path);
                }
                ++serial;
            }
        }
    }
    uint32_t vm = ARG(0), context = ARG(2);
    const char *extension = NULL;
    if (void_vm_readable(context, 12)) {
        uint32_t start = MEM32(context + 8), size = MEM32(context + 4);
        if (start && size && void_vm_readable(start, size)) {
            const uint8_t *bytes = GPTR(start);
            if (closure_source(bytes, size, "conv_tmp/sq_tmp_StageMain.global.nut")) {
                void_grief_menu_scene((const char *)bytes, size);
                extension = stage_main_script;
            }
            if (closure_source(bytes, size, "conv_tmp/sq_tmp_GameOver.global.nut"))
                extension = gameover_script;
            if (closure_source(bytes, size, "conv_tmp/sq_tmp_effect.nut"))
                extension = effect_script;
            if (closure_source(bytes, size, "conv_tmp/sq_tmp_Title.global.nut"))
                extension = title_script;
            if (closure_source(bytes, size, "conv_tmp/sq_tmp_TitleBG.global.nut"))
                extension = title_background_script;
            if (closure_source(bytes, size, "conv_tmp/sq_tmp_StageSelect.global.nut"))
                extension = void_grief_lan_active() ? stage_select_lan_script : stage_select_script;
            if (closure_source(bytes, size, "conv_tmp/sq_tmp_StagePause.global.nut"))
                extension = stage_pause_script;
            if (closure_source(bytes, size, "conv_tmp/sq_tmp_NowLoading.global.nut"))
                extension = now_loading_script;
            if (closure_source(bytes, size, "conv_tmp/sq_tmp_StageResult.global.nut"))
                extension = stage_result_script;
            if (closure_source(bytes, size, "conv_tmp/sq_tmp_bulletBoss.nut"))
                extension = boss_clear_script;
            if (closure_source(bytes, size, "conv_tmp/sq_tmp_camera.nut"))
                extension = camera_script;
            int void_grief_tv_enabled(void);
            if (closure_source(bytes, size, "conv_tmp/sq_tmp_stage3C.global.nut") && void_grief_tv_enabled())
                extension = stage3_tv_script;
        }
    }
    read_closure();
    if (extension && (int32_t)g_eax >= 0) {
        /* CompileFile caches Init/Update before returning; adapt its closure before that cache. */
        void_regs saved;
        void_regs_save(&saved);
        static uint32_t adapted_vm;
        register_port_menu(vm);
        uint32_t object = void_galloc(8), name = void_gstrdup("PortOriginal");
        uint32_t stack[] = { vm, (uint32_t)-1, object };
        void_call_guest(0x0052BDA0u, 3, stack, 0);
        void_call_guest(0x0052B930u, 1, &vm, 0);
        uint32_t push_name[] = { vm, name, (uint32_t)-1 };
        void_call_guest(0x0052B740u, 3, push_name, 0);
        uint32_t push_object[] = { vm, MEM32(object), MEM32(object + 4) };
        void_call_guest(0x0052BDF0u, 3, push_object, 0);
        uint32_t slot[] = { vm, (uint32_t)-3, 0 };
        if ((int32_t)void_call_guest(0x0052DBC0u, 3, slot, 0) < 0)
            void_fatal("Cannot retain original menu closure");
        uint32_t pop[] = { vm, 2 };
        void_call_guest(0x0052BC90u, 2, pop, 0);
        int common = extension == stage_select_script || extension == stage_select_lan_script || extension == stage_pause_script;
        const char *prefix = adapted_vm == vm || !common ? "" : common_script;
        const char *begin = "local port = ::PortMenu;\ntry {\nlocal original = ::PortOriginal; delete ::PortOriginal; original.call(this);\n";
        const char *end = "\n} catch(error) { port(3, error.tostring()); throw error; }\n";
        uint32_t size = (uint32_t)(strlen(begin) + strlen(prefix) + strlen(extension) + strlen(end));
        uint32_t source = void_galloc(size + 1);
        snprintf(GPTR(source), size + 1, "%s%s%s%s", begin, prefix, extension, end);
        uint32_t compile[] = { vm, source, size, name, 1 };
        if ((int32_t)void_call_guest(COMPILE_BUFFER, 5, compile, 0) < 0)
            void_fatal("Cannot compile menu adapter");
        if (common) adapted_vm = vm;
        void_log("[touch] adapted original menu closure");
        void_gfree(source); void_gfree(name); void_gfree(object);
        void_regs_load(&saved);
    }
    g_cur_func = caller;
}

int void_grief_widescreen_mounted(void)
{
    return __atomic_load_n(&widescreen_mounted, __ATOMIC_ACQUIRE);
}

static void mount_widescreen(void)
{
    if (widescreen_mount_attempted++) return;
    /* The original loader trusts its index; validate all known image entries first. */
    if (!void_grief_widescreen_validate()) return;
    void_regs saved;
    void_regs_save(&saved);
    uint32_t name = void_gstrdup("gs_widescreen.dat");
    int mounted = (void_call_guest_this(0x004D0400u, name, 0, NULL) & 255u) != 0;
    void_gfree(name);
    void_regs_load(&saved);
    __atomic_store_n(&widescreen_mounted, mounted, __ATOMIC_RELEASE);
    void_log("[widescreen] engine bank mount %s", mounted ? "OK" : "failed");
}

static void grief_render_config(void)
{
    uint32_t caller = g_cur_func;
    MEM8(0x006AC11Du) = 1;
    render_config();
    g_cur_func = caller;
}

static void install_hook(uint32_t address, void_fn hook, void_fn *original)
{
    *original = void_dispatch_find(address);
    if (*original) void_dispatch_add(address, hook);
}

void void_grief_script_init(void)
{
    void void_grief_tv_init(void);
    void_grief_tv_init();
    compile_buffer = void_dispatch_find(COMPILE_BUFFER);
    if (!compile_buffer) void_fatal("Squirrel compilebuffer is not lifted");
    void_dispatch_add(COMPILE_BUFFER, void_grief_compile_buffer);
    install_hook(CAMERA_INIT, grief_camera_init, &camera_init);
    install_hook(RENDER_CONFIG, grief_render_config, &render_config);
    read_closure = void_dispatch_find(0x0052C260u);
    void_grief_menu_play_se_init();
    void_log("[viewport] hooks camera %p render %p", camera_init, render_config);
}

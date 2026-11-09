#include "host.h"
#include "grief_menu.h"
#include "grief_widescreen.h"
#include "input.h"
#include "d3d9_internal.h"
#include <math.h>

#define MENU_GENERAL 1
#define MENU_KEYBOARD 2
#define MENU_PAD 3
#define MENU_CAPTURE 4
#define CFG_TOUCH_LAYOUT 12
#define CFG_MENU_VISIBILITY 15
#define CFG_TOUCH_LABELS 16
#define CFG_LAN 17
#define CFG_GET_AA 20
#define CFG_SET_AA 21
#define CFG_GAMEPLAY_VISIBILITY 22
#define CFG_PAUSE_VISIBILITY 23
#define CFG_CONFIRM_DEFAULTS 24
#define CFG_CONFIRM_EXIT 25

static pthread_mutex_t menu_lock = PTHREAD_MUTEX_INITIALIZER;
static int scene, wide_scene, known_scene, loading_visible, page, cursor, player, capturing;
static int dirty = 1, pending_action, pending_stage;
/* Selector.nut ignores navigation for six updates after moving. */
#define MENU_KEY_FRAMES 8
static uint32_t menu_keys[32];
static int menu_key_count, menu_key_wait;
static int touch_slider, touch_row, skip_logo, logo_skipped, logo_skip_frames, logo_scene;
static int level_scene, level_cursor, level_character, character_cursor, gameplay_pending;
static int lan_selection_scene;
static int lan_target = -2, lan_flags, lan_context, lan_state, lan_glasses, lan_exit;
static int lan_waiting, lan_sent_state;
static int pause_scene, pause_cursor, pause_dialog, pause_yes;
static float touch_start_x, touch_start_y;
static int script_context, script_request, request_context, stage_available = 1;
static int hold_kind, character_held, pending_sound;
static uint32_t hold_started;
static int stage_rects[7][4], stage_rect_mask;
static int panorama_scene = -1, panorama_alpha;
/* A single-player victory result keeps the widened scene; its 800-pixel overlay is centered. */
static int result_overlay;

int void_grief_qa_flags(void);
int void_grief_lan_active(void);
int void_grief_lan_host(void);
void void_android_config_event(int event, float value);
static void inject(uint32_t vk, int down);
static void tap_key(uint32_t vk);
/* Selection request flags shared with stage_select.nut. */
#define SELECT_CONFIRM 256
#define SELECT_VARIANT 512
#define SELECT_CANCEL 1024

void void_grief_menu_rect(int id, const int rect[4])
{
    if (id < 0 || id > 6 || rect[0] < -1600 || rect[1] < -1200 ||
        rect[2] > 2400 || rect[3] > 1800 || rect[2] <= rect[0] || rect[3] <= rect[1]) return;
    pthread_mutex_lock(&menu_lock);
    memcpy(stage_rects[id], rect, sizeof stage_rects[id]);
    stage_rect_mask |= 1 << id;
    pthread_mutex_unlock(&menu_lock);
}

int void_grief_menu_pause_visible(void) { return __atomic_load_n(&pause_scene, __ATOMIC_ACQUIRE); }

void void_grief_widescreen_scene(int id, int alpha)
{
    if (id < -1 || id > 6) return;
    if (alpha < 0) alpha = 0;
    if (alpha > 1000) alpha = 1000;
    pthread_mutex_lock(&menu_lock);
    panorama_scene = id;
    panorama_alpha = alpha;
    pthread_mutex_unlock(&menu_lock);
}

int void_grief_widescreen_state(int *alpha)
{
    pthread_mutex_lock(&menu_lock);
    int id = panorama_scene;
    if (alpha) *alpha = panorama_alpha;
    pthread_mutex_unlock(&menu_lock);
    return id;
}

static void request_selection(int target, int flags)
{
    if (void_grief_lan_active() && (script_context == 2 || script_context == 3 || script_context == 9)) {
        int context = script_context == 9 ? 3 : script_context;
        if (context == 2 && !void_grief_lan_host()) return;
        if ((flags & SELECT_CANCEL) || lan_context != context || lan_target != target) lan_waiting = 0;
        lan_context = context;
        lan_target = target;
        lan_flags = flags;
        return;
    }
    request_context = script_context == 9 ? 3 : script_context;
    script_request = (target + 1) | flags;
}

int void_grief_menu_script(int command, int value)
{
    if (command == 1) return void_grief_qa_flags();
    if (command == 2) { uint32_t w, h; void_grief_render_size(&w, &h); return (int)w; }
    if (command == 6) return void_grief_lan_active();
    if (command == 7) { void_grief_widescreen_scene(value >> 10, value & 1023); return 0; }
    if (command == 8) return void_grief_widescreen_state(NULL);
    if (command == 10) {
        pthread_mutex_lock(&menu_lock);
        loading_visible = value != 0;
        pthread_mutex_unlock(&menu_lock);
        return 0;
    }
    if (command == 5) {
        uint32_t w = 800, h;
        if (void_grief_present_wide()) void_grief_render_size(&w, &h);
        return (int)w;
    }
    if (command != 0) return 0;
    int context = value & 15, selected = (value >> 4) & 15, changed_camera = 0;
    pthread_mutex_lock(&menu_lock);
    if (script_context != context) {
        void_log("[menu] context %d cursor %d stages %02X", context, selected, (value >> 8) & 63);
        if (context == 3) void_android_config_event(28, 0);
    }
    script_context = context;
    if (void_grief_lan_active()) {
        lan_state = (value >> 8) & 3;
        lan_glasses = (value >> 10) & 1;
        lan_exit = (value >> 14) & 1;
        if (context != lan_context && !(context == 9 && lan_context == 3)) {
            lan_target = -2;
            lan_waiting = 0;
        }
    }
    if (context == 2 || context == 3 || context == 7 || context == 9) result_overlay = 0;
    if (context == 2 || context == 3 || context == 9) {
        level_scene = 1;
        level_character = context != 2;
        gameplay_pending = 0;
        if (context == 2) { level_cursor = selected; stage_available = (value >> 8) & 63; }
        else character_cursor = selected;
    }
    int paused = context == 4 || context == 5 || context == 8;
    if (pause_scene != paused) {
        pause_scene = paused;
        void_android_config_event(CFG_PAUSE_VISIBILITY, paused);
    }
    if (paused) {
        pause_dialog = context == 5;
        if (pause_dialog) pause_yes = selected == 0;
        else pause_cursor = selected;
    }
    if (context == 6) {
        if (!wide_scene) { wide_scene = known_scene = 1; changed_camera = 1; }
        if (level_scene || !gameplay_pending) void_android_config_event(CFG_GAMEPLAY_VISIBILITY, 1);
        scene = level_scene = 0;
        gameplay_pending = -1;
    } else if (context == 7) {
        level_scene = 0;
        panorama_scene = -1;
        panorama_alpha = 0;
        void_android_config_event(CFG_GAMEPLAY_VISIBILITY, 0);
    } else if (context == 10) {
        /* Single-player victories stay on the widened scene with their art; LAN keeps the original canvas. */
        int victory = panorama_scene >= 2 && !void_grief_lan_active();
        if (!victory) wide_scene = 0;
        result_overlay = victory;
        known_scene = 1;
        level_scene = gameplay_pending = 0;
        changed_camera = panorama_scene < 2;
        void_android_config_event(CFG_GAMEPLAY_VISIBILITY, 0);
    }
    int result = request_context == context ? script_request : 0;
    if (request_context == context || context != 9) script_request = 0;
    pthread_mutex_unlock(&menu_lock);
    if (changed_camera) void_grief_camera_scene();
    return result;
}

int void_grief_config_command(int command, int32_t argument);
int32_t void_grief_config_binding(unsigned index);
void void_grief_config_fps_choice(float choice);
float void_grief_config_fps_cycle(int delta);
float void_grief_config_fps(void);
int void_grief_config_touch_labels(void);
void void_android_config_event(int event, float value);
int void_android_menu_render(const int state[64], uint32_t *pixels, int count) __attribute__((weak));
int void_android_menu_render(const int state[64], uint32_t *pixels, int count)
{
    (void)state; (void)pixels; (void)count; return 0;
}

static const int default_dik[9] = { 200,208,203,205,44,45,46,30,16 };
static const int default_vk[9] = { 0x26,0x28,0x25,0x27,'Z','X','C','A','Q' };
static const int default_pad[5] = { 524,526,527,525,768 };

static int contains(const char *source, uint32_t size, const char *needle)
{
    size_t n = strlen(needle);
    if (!source || size < n) return 0;
    for (uint32_t i = 0; i + n <= size; ++i)
        if (!memcmp(source + i, needle, n)) return 1;
    return 0;
}

void void_grief_set_skip_logo(int value)
{
    skip_logo = value != 0;
    if (!skip_logo) logo_skip_frames = 0;
}

void void_grief_menu_scene(const char *source, uint32_t size)
{
    void_grief_menu_touch(3, 0, 0);
    if (contains(source, size, "data/system/StagePause/")) {
        return;
    }
    int title = contains(source, size, "data/system/Title/Title.global.nut");
    int stage = contains(source, size, "data/system/StageMain/");
    int stage_select = contains(source, size, "data/system/StageSelect/");
    int lan_select = stage_select && void_grief_lan_active();
    int fixed = stage_select || contains(source, size, "data/system/staffroll/");
    int logo = contains(source, size, "data/system/Opening/") || contains(source, size, "data/system/Logo") ||
        contains(source, size, "data/system/Company") || contains(source, size, "data/system/CircleLogo/");
    if (!title && !stage && !fixed && !logo) return;
    pthread_mutex_lock(&menu_lock);
    scene = title;
    result_overlay = 0;
    panorama_scene = title ? 0 : stage_select ? 1 : -1;
    panorama_alpha = 0;
    wide_scene = stage || (title && void_grief_widescreen_mounted());
    loading_visible = 0;
    level_scene = stage_select;
    lan_selection_scene = lan_select;
    logo_scene = logo;
    pause_scene = 0;
    script_context = stage_select ? 2 : 0;
    script_request = 0;
    stage_available = 1;
    stage_rect_mask = 0;
    known_scene = 1;
    menu_key_count = menu_key_wait = 0;
    page = 0;
    capturing = 0;
    touch_row = -1;
    if (title || stage_select) gameplay_pending = 0;
    if (title) {
        cursor = 0;
        pending_action = pending_stage = 0;
    }
    if (stage_select) {
        level_cursor = 0;
        level_character = 0;
    }
    if (logo && skip_logo && !logo_skipped) {
        logo_skipped = 1;
        logo_skip_frames = 4;
    }
    dirty = 1;
    pthread_mutex_unlock(&menu_lock);
    void_grief_camera_scene();
    void_android_config_event(CFG_PAUSE_VISIBILITY, 0);
    void_android_config_event(CFG_GAMEPLAY_VISIBILITY, stage != 0);
    if (title) void_android_config_event(CFG_MENU_VISIBILITY, 0);
    void_android_config_event(21, 1);
}

void void_grief_menu_file_write(const char *path)
{
    const char *name = strrchr(path, '/');
    if (!name || strncasecmp(++name, "save", 4)) return;
    const char *number = name + 4, *suffix = number;
    while (*suffix >= '0' && *suffix <= '9') ++suffix;
    if (suffix == number || strcasecmp(suffix, ".dat")) return;
    pthread_mutex_lock(&menu_lock);
    /* shortcut: LAN battle starts at its stage autosave; use a scene callback for no-save entry paths. */
    if (lan_selection_scene) {
        lan_selection_scene = 0;
        gameplay_pending = 1;
    }
    pthread_mutex_unlock(&menu_lock);
}

int void_grief_menu_wide(void)
{
    pthread_mutex_lock(&menu_lock);
    int wide = (known_scene && wide_scene) || loading_visible;
    pthread_mutex_unlock(&menu_lock);
    return wide;
}

int void_grief_present_wide(void)
{
    pthread_mutex_lock(&menu_lock);
    int wide = (known_scene && wide_scene) || loading_visible, victory = panorama_scene >= 2;
    pthread_mutex_unlock(&menu_lock);
    return wide && (!victory || !void_grief_lan_active());
}

int void_grief_menu_result_visible(void)
{
    pthread_mutex_lock(&menu_lock);
    int visible = result_overlay;
    pthread_mutex_unlock(&menu_lock);
    return visible;
}

static int original_count(void) { return 2; }
static int native_count(void) { return 4; }

int void_grief_menu_suppress_item(const void *vertices, uint32_t stride)
{
    const uint8_t *bytes = vertices;
    float left = 10000.0f, top = 10000.0f, right = -10000.0f, bottom = -10000.0f;
    if (!vertices || stride != 28) return 0;
    for (int i = 0; i < 4; ++i) {
        float x, y;
        memcpy(&x, bytes + i * stride, 4);
        memcpy(&y, bytes + i * stride + 4, 4);
        if (x < left) left = x;
        if (x > right) right = x;
        if (y < top) top = y;
        if (y > bottom) bottom = y;
    }
    pthread_mutex_lock(&menu_lock);
    int suppress = scene && left >= 450.0f && top >= 230.0f && bottom <= 430.0f &&
        right - left <= 400.0f && bottom - top <= 70.0f;
    pthread_mutex_unlock(&menu_lock);
    return suppress;
}

int void_grief_menu_active(void)
{
    return __atomic_load_n(&page, __ATOMIC_ACQUIRE) != 0;
}

static void show_page(int value)
{
    __atomic_store_n(&page, value, __ATOMIC_RELEASE);
    cursor = 0;
    if (value != MENU_CAPTURE) capturing = 0;
    dirty = 1;
    void_android_config_event(CFG_MENU_VISIBILITY, value != 0);
}

static int binding_index(void) { return player * 9 + cursor; }
static int binding_kind(void) { return page == MENU_PAD || (page == MENU_CAPTURE && capturing == 2) ? 30 : 0; }

static int pad_code(int code)
{
    switch (code) {
    case 96: return 524;
    case 97: return 526;
    case 99: return 525;
    case 100: return 527;
    case 108: return 768;
    case 19: return 0;
    case 20: return 1;
    case 21: return 2;
    case 22: return 3;
    default: return 0x300 + code;
    }
}

int void_grief_menu_android_vk(int code)
{
    if (code >= 29 && code <= 54) return 'A' + code - 29;
    if (code >= 7 && code <= 16) return '0' + code - 7;
    if (code >= 131 && code <= 142) return 0x70 + code - 131;
    if (code >= 144 && code <= 153) return 0x60 + code - 144;
    switch (code) {
    case 19: return 0x26; case 20: return 0x28; case 21: return 0x25; case 22: return 0x27;
    case 62: return 0x20; case 66: return 0x0D; case 111: return 0x1B; case 61: return 0x09;
    case 67: return 0x2E; case 92: return 0x21; case 93: return 0x22; case 122: return 0x24; case 123: return 0x23;
    case 113: return 0xA2; case 114: return 0xA3; case 59: return 0xA0; case 60: return 0xA1;
    case 57: return 0xA4; case 58: return 0xA5; case 55: return 0xBC; case 56: return 0xBE;
    case 74: return 0xBA; case 76: return 0xBF; case 73: return 0xDC; case 71: return 0xDB; case 72: return 0xDD;
    case 69: return 0xBD; case 70: return 0xBB; case 68: return 0xC0; case 115: return 0x14;
    case 143: return 0x90; case 116: return 0x91; case 120: return 0x2C; case 121: return 0x13;
    case 160: return 0x0C; case 157: return 0x6B; case 156: return 0x6D; case 155: return 0x6A;
    case 154: return 0x6F; case 158: return 0x6E;
    default: return 0;
    }
}

int void_grief_menu_capture(int source, int code)
{
    pthread_mutex_lock(&menu_lock);
    if (page != MENU_CAPTURE) { pthread_mutex_unlock(&menu_lock); return 0; }
    int vk = source == 1 ? void_grief_menu_android_vk(code) : 0;
    if (source == 1 && vk == 0x1B) {
        show_page(capturing == 2 ? MENU_PAD : MENU_KEYBOARD);
        pthread_mutex_unlock(&menu_lock);
        return 1;
    }
    int value = source == 1 ? (int)void_vk_to_dik((uint32_t)vk) : pad_code(code);
    if (source == 1 && vk == 'X') value = -1;
    if (value || source != 1) {
        void_grief_config_command(8, (binding_kind() + binding_index()) << 16 | (value & 0xFFFF));
        show_page(capturing == 2 ? MENU_PAD : MENU_KEYBOARD);
    }
    dirty = 1;
    pthread_mutex_unlock(&menu_lock);
    return 1;
}

static void general_key(int vk)
{
    if (vk == 0x26 || vk == 0x28) {
        cursor = (cursor + (vk == 0x26 ? 7 : 1)) % 8;
    } else if (vk == 0x25 || vk == 0x27) {
        int delta = vk == 0x25 ? -1 : 1;
        if (cursor == 0) void_grief_config_command(2, void_grief_config_command(1, 0) + delta * 5);
        else if (cursor == 1) void_grief_config_command(CFG_SET_AA,
            (void_grief_config_command(CFG_GET_AA, 0) + delta + 4) % 4);
        else if (cursor == 2) void_grief_config_fps_cycle(delta);
        else if (cursor == 3) void_grief_config_command(CFG_TOUCH_LAYOUT, 0);
        else if (cursor == 4) void_grief_config_command(CFG_TOUCH_LABELS, 0);
    } else if (vk == 'Z' || vk == 0x0D) {
        if (cursor == 1) general_key(0x27);
        else if (cursor == 2) general_key(0x27);
        else if (cursor == 3) void_grief_config_command(CFG_TOUCH_LAYOUT, 0);
        else if (cursor == 4) void_grief_config_command(CFG_TOUCH_LABELS, 0);
        else if (cursor == 5) void_android_config_event(CFG_CONFIRM_DEFAULTS, 0);
        else if (cursor == 6 && void_grief_config_command(10, 0)) show_page(0);
        else if (cursor == 7) { void_grief_config_command(11, 0); show_page(0); }
    } else if (vk == 'X' || vk == 'Q' || vk == 0x1B) {
        void_grief_config_command(11, 0);
        show_page(0);
    } else if (vk == 0x09) show_page(MENU_KEYBOARD);
    dirty = 1;
}

static void binding_key(int vk)
{
    if (vk == 0x26 || vk == 0x28) cursor = (cursor + (vk == 0x26 ? 8 : 1)) % 9;
    else if (vk >= '1' && vk <= '3') player = vk - '1';
    else if (vk == 0x09) show_page(page == MENU_KEYBOARD ? MENU_PAD : MENU_KEYBOARD);
    else if (vk == 'X') void_grief_config_command(8, (binding_kind() + binding_index()) << 16 | 0xFFFF);
    else if (vk == 'Q' || vk == 0x1B) show_page(MENU_GENERAL);
    else if (vk == 'Z' || vk == 0x0D) { capturing = page == MENU_PAD ? 2 : 1; show_page(MENU_CAPTURE); }
    dirty = 1;
}

int void_grief_menu_key(uint32_t vk, int down)
{
    pthread_mutex_lock(&menu_lock);
    if (!scene || !down || page == MENU_CAPTURE) { pthread_mutex_unlock(&menu_lock); return 0; }
    int consume = 1;
    if (!page) {
        if (vk == 0x26 || vk == 0x28) {
            cursor = (cursor + (vk == 0x26 ? native_count() - 1 : 1)) % native_count();
            pending_sound = 1;
            dirty = 1;
        } else if (vk == 'Z' || vk == 0x0D) {
            if (void_grief_lan_active() && !void_grief_lan_host() && (cursor == 0 || cursor == 3)) {
                pthread_mutex_unlock(&menu_lock);
                return 1;
            }
            if (cursor == 1) void_android_config_event(CFG_LAN, 1);
            else if (cursor == 2) show_page(MENU_GENERAL);
            else if (cursor == 3) void_android_config_event(CFG_CONFIRM_EXIT, 0);
            else if (!pending_action) { pending_action = 1; pending_stage = 0; dirty = 1; }
        } else consume = 0;
    } else if (page == MENU_GENERAL) general_key(vk);
    else binding_key(vk);
    pthread_mutex_unlock(&menu_lock);
    return consume;
}

static void inject(uint32_t vk, int down)
{
    if (void_grief_lan_active())
        for (int action = 0; action < 9; ++action)
            if ((uint32_t)default_vk[action] == vk) { vk = (uint32_t)void_grief_touch_binding(action); break; }
    void_input_key_ex(vk, down, 0);
}

static void update_hold(void)
{
    if (hold_kind && (uint32_t)(void_tick_ms() - hold_started) >= (hold_kind == 1 ? 3000u : 500u)) {
        if (hold_kind == 1) void_android_config_event(26, 0);
        else {
            /* The script reads its own variant flag, so every Homura hold toggles it. */
            character_held = 1;
            request_selection(touch_row, touch_row == 0 ? SELECT_VARIANT : 0);
        }
        hold_kind = 0;
    }
}

/* Only ordinary local P1 keys enter GSOnline; UI metadata never changes guest selection state. */
static void lan_selection_pump(void)
{
    if (lan_target == -2 || script_context != lan_context || menu_key_count || menu_key_wait) return;
    int selected = lan_context == 2 ? level_cursor : character_cursor;
    int observed = selected | (lan_context == 2 ? lan_exit << 7 : (lan_state << 4) | (lan_glasses << 6));
    if (lan_waiting) {
        if (observed == lan_sent_state) return;
        lan_waiting = 0;
    }
    uint32_t key = 0;
    if (lan_context == 2) {
        if (!void_grief_lan_host()) { lan_target = -2; return; }
        if (lan_flags & SELECT_CANCEL) {
            if (!lan_exit) key = 0x28;
            else { key = 'Z'; lan_target = -2; }
        } else if (lan_exit) key = 0x26;
        else if (selected != lan_target) key = selected < lan_target ? 0x27 : 0x25;
        else if (lan_flags & SELECT_CONFIRM) { key = 'Z'; lan_target = -2; }
        else lan_target = -2;
    } else if (lan_flags & SELECT_CANCEL) { key = 'X'; lan_target = -2; }
    else if (lan_state == 2 && (selected != lan_target || (lan_flags & SELECT_VARIANT))) key = 'X';
    else if (lan_state == 0) key = 'Z';
    else if (lan_state == 2) lan_target = -2;
    else if (selected != lan_target) key = selected < lan_target ? 0x27 : 0x25;
    else if (lan_flags & SELECT_VARIANT) {
        key = lan_glasses ? 0x28 : 0x26;
        lan_flags &= ~SELECT_VARIANT;
    } else if (lan_flags & SELECT_CONFIRM) { key = 'Z'; lan_target = -2; }
    else lan_target = -2;
    if (key) {
        tap_key(key);
        lan_waiting = 1;
        lan_sent_state = observed;
    }
}

static void menu_pump(void)
{
    int camera_changed = 0;
    pthread_mutex_lock(&menu_lock);
    update_hold();
    if (menu_key_wait > 0) --menu_key_wait;
    lan_selection_pump();
    if (menu_key_count && !menu_key_wait) {
        inject(menu_keys[0], 1);
        inject(menu_keys[0], 0);
        --menu_key_count;
        memmove(menu_keys, menu_keys + 1, (size_t)menu_key_count * sizeof *menu_keys);
        menu_key_wait = MENU_KEY_FRAMES;
    }
    if (logo_skip_frames > 0) {
        --logo_skip_frames;
        inject('Z', 1);
        inject('Z', 0);
    }
    if (gameplay_pending > 0 && --gameplay_pending == 0) {
        wide_scene = 1;
        level_scene = 0;
        known_scene = 1;
        camera_changed = 1;
        void_android_config_event(CFG_GAMEPLAY_VISIBILITY, 1);
    }
    if (!pending_action) goto done;
    int row = pending_action - 1;
    int count = original_count();
    int wanted = row ? count - 1 : 0;
    int phase = pending_stage / MENU_KEY_FRAMES;
    if (pending_stage % MENU_KEY_FRAMES == 0) {
        if (phase < count) {
            inject(0x26, 1);
            inject(0x26, 0);
        } else if (phase < count + wanted) {
            inject(0x28, 1);
            inject(0x28, 0);
        } else if (phase == count + wanted) {
            inject('Z', 1);
            inject('Z', 0);
            pending_action = 0;
        }
    }
    ++pending_stage;
done:;
    int sound = pending_sound;
    pending_sound = 0;
    pthread_mutex_unlock(&menu_lock);
    if (camera_changed) void_grief_camera_scene();
    if (sound) {
        void_regs saved;
        void_regs_save(&saved);
        uint32_t id = (uint32_t)(sound - 1);
        void_call_guest(0x00443EE0u, 1, &id, 0);
        void_regs_load(&saved);
    }
}

static void tap_key(uint32_t vk)
{
    if (menu_key_count >= (int)(sizeof menu_keys / sizeof *menu_keys))
        void_fatal("Menu input queue overflow");
    menu_keys[menu_key_count++] = vk;
}

static void activate_touch(int row)
{
    if (pending_action) return;
    if (void_grief_lan_active() && !void_grief_lan_host() && (row == 0 || row == 3)) return;
    if (row == 1) { void_android_config_event(CFG_LAN, 1); return; }
    if (row == 2) { show_page(MENU_GENERAL); return; }
    if (row == 3) { void_android_config_event(CFG_CONFIRM_EXIT, 0); return; }
    pending_action = 1;
    pending_stage = 0;
    dirty = 1;
}

static int stage_zone(float x, float y)
{
    if (stage_rect_mask) {
        for (int n = 7; n >= 0; --n) {
            int i = n == 7 ? level_cursor : n;
            if (i > 6 || !(stage_rect_mask & (1 << i))) continue;
            const int *r = stage_rects[i];
            if (x >= r[0] - 6 && x <= r[2] + 6 && y >= r[1] - 6 && y <= r[3] + 6) return i;
        }
        return -1;
    }
    static const float zones[6][4] = {
        { 20, 90, 295, 280 }, { 245, 10, 510, 185 }, { 520, 20, 755, 200 },
        { 540, 205, 800, 410 }, { 425, 155, 660, 355 }, { 150, 225, 430, 440 },
    };
    for (int i = 5; i >= 0; --i)
        if (x >= zones[i][0] && x <= zones[i][2] && y >= zones[i][1] && y <= zones[i][3]) return i;
    return -1;
}

static int character_zone(float x, float y)
{
    static const float zones[5][4] = {
        { 20, 20, 162.5f, 470 }, { 162.5f, 20, 312.5f, 470 }, { 312.5f, 20, 462.5f, 470 },
        { 462.5f, 20, 612.5f, 470 }, { 612.5f, 20, 775, 470 },
    };
    for (int i = 0; i < 5; ++i)
        if (x >= zones[i][0] && x < zones[i][2] && y >= zones[i][1] && y <= zones[i][3]) return i;
    return -1;
}

static void level_touch(int action, float gx, float gy, int *swiped)
{
    int wanted = level_character ? character_zone(gx, gy) : stage_zone(gx, gy);
    if (action == 0) {
        touch_start_x = gx; touch_start_y = gy;
        touch_row = wanted;
        *swiped = 0;
        character_held = 0;
        hold_kind = level_character && wanted >= 0 ? 2 : 0;
        hold_started = void_tick_ms();
        if (wanted >= 0 && (level_character || (stage_available & (1 << wanted))))
            request_selection(wanted, 0);
    } else if (action == 2) {
        float dx = gx - touch_start_x, dy = gy - touch_start_y;
        if (dx * dx + dy * dy >= 900) {
            hold_kind = 0;
            *swiped = 1;
            if (wanted >= 0 && (level_character || (stage_available & (1 << wanted))))
                request_selection(wanted, 0);
        }
    } else if (action == 1) {
        update_hold();
        hold_kind = 0;
        if (!*swiped && !character_held && level_character && wanted < 0 && touch_row < 0)
            request_selection(-1, SELECT_CANCEL);
        else if (!*swiped && !level_character && wanted == 6 && wanted == touch_row)
            request_selection(-1, SELECT_CANCEL);
        else if (!*swiped && !character_held && wanted >= 0 && wanted == touch_row) {
            if (level_character || (stage_available & (1 << wanted)))
                request_selection(wanted, SELECT_CONFIRM);
            else pending_sound = 8;
        }
        touch_row = -1;
        *swiped = character_held = 0;
    }
}

static void pause_touch(int action, float gx, float gy)
{
    int row = -1;
    if (script_context == 8) row = 0;
    else if (pause_dialog) {
        if (gx >= 260 && gx <= 510 && gy >= 275 && gy <= 335) row = gx < 385 ? 0 : 1;
    } else if (gx >= 300 && gx <= 635 && gy >= 10 && gy < 94) row = (int)((gy - 10) / 28);
    if (action == 0) {
        touch_row = row;
        touch_start_x = gx; touch_start_y = gy;
        if (row >= 0) request_selection(row, 0);
    } else if (action == 1) {
        float dx = gx - touch_start_x, dy = gy - touch_start_y;
        if (dx * dx + dy * dy < 900 && row == touch_row)
            request_selection(row, row < 0 || script_context == 8 ? SELECT_CANCEL : SELECT_CONFIRM);
        touch_row = -1;
    }
}

int void_grief_menu_touch(int action, float x, float y)
{
    static int swiped, touch_page, touch_blocked, touch_active;
    if (action == 3) {
        pthread_mutex_lock(&menu_lock);
        swiped = hold_kind = character_held = script_request = 0;
        lan_target = -2;
        lan_waiting = 0;
        touch_blocked = 0;
        touch_active = 0;
        touch_slider = 0;
        touch_row = -1;
        touch_page = -1;
        pthread_mutex_unlock(&menu_lock);
        return 1;
    }
    if (action > 2) return 0;
    int sw = 0, sh = 0;
    void_android_display_size(&sw, &sh);
    if (!sw || !sh) return 0;
    float scale = fminf((float)sw / 800.0f, (float)sh / 600.0f);
    float dw = 800.0f * scale, dh = 600.0f * scale;
    float gx = (x * sw - (sw - dw) * 0.5f) / scale;
    float gy = (y * sh - (sh - dh) * 0.5f) / scale;
    pthread_mutex_lock(&menu_lock);
    float title_offset = scene && !page && wide_scene ? (sw - dw) * 0.5f / scale : 0;
    if (!scene && !level_scene && !pause_scene && !logo_scene) { pthread_mutex_unlock(&menu_lock); return 0; }
    if (action == 0) {
        touch_active = 1;
        int full_screen = (pause_scene && script_context == 8) || (level_scene && level_character);
        touch_blocked = (!full_screen && (gx < -title_offset || gx >= 800 + title_offset || gy < 0 || gy >= 600)) ||
            menu_key_count || pending_action || gameplay_pending > 0;
    } else if (!touch_active) { pthread_mutex_unlock(&menu_lock); return 1; }
    if (action == 1) touch_active = 0;
    if (touch_blocked) { pthread_mutex_unlock(&menu_lock); return 1; }
    if (logo_scene) {
        if (action == 1) {
            tap_key('Z');
            logo_scene = 0;
        }
        pthread_mutex_unlock(&menu_lock);
        return 1;
    }
    if (pause_scene) {
        pause_touch(action, gx, gy);
        pthread_mutex_unlock(&menu_lock);
        return 1;
    }
    if (level_scene && !page) {
        level_touch(action, gx, gy, &swiped);
        pthread_mutex_unlock(&menu_lock);
        return 1;
    }
    if (!scene) { pthread_mutex_unlock(&menu_lock); return 0; }
    if (!page) {
        float menu_x = gx - title_offset, stats_x = gx + title_offset;
        if (action == 0) {
            touch_start_x = gx;
            touch_start_y = gy;
            swiped = 0;
            touch_row = -1;
            if (!void_grief_lan_active() && stats_x >= 20 && stats_x <= 445 && gy >= 410 && gy <= 590) {
                hold_kind = 1;
                hold_started = void_tick_ms();
            }
            if (menu_x >= 480 && menu_x <= 800 && gy >= 231 && gy < 383) {
                int row = (int)((gy - 231) / 38);
                if (row >= 0 && row < native_count()) {
                    touch_row = row;
                    if (cursor != row) pending_sound = 1;
                    cursor = row;
                    dirty = 1;
                }
            }
        } else if (action == 2 && !swiped) {
            float dy = gy - touch_start_y;
            float dx = gx - touch_start_x;
            if (dx * dx + dy * dy >= 900) hold_kind = 0;
            if (dy * dy >= 900) {
                hold_kind = 0;
                pending_sound = 1;
                cursor = (cursor + (dy > 0 ? 1 : native_count() - 1)) % native_count();
                swiped = 1;
                dirty = 1;
            }
        } else if (action == 1) {
            hold_kind = 0;
            int row = -1;
            if (menu_x >= 480 && menu_x <= 800 && gy >= 231 && gy < 383) row = (int)((gy - 231) / 38);
            if (!swiped && row >= 0 && row < native_count() && row == touch_row) activate_touch(row);
            touch_row = -1;
            swiped = 0;
        }
        pthread_mutex_unlock(&menu_lock);
        return 1;
    }
    if (action == 0) {
        touch_page = page;
        touch_row = -1;
        touch_slider = 0;
        if (gy >= 54 && gy <= 92) {
            touch_row = 100 + (gx < 260 ? 0 : gx < 520 ? 1 : 2);
        } else if (page != MENU_GENERAL && gy >= 94 && gy <= 126 && gx >= 260) {
            int wanted = (int)((gx - 260) / 170);
            if (wanted < 0) wanted = 0;
            if (wanted > 2) wanted = 2;
            touch_row = 200 + wanted;
        } else if (page == MENU_GENERAL && gy >= 112 && gy < 512) {
            touch_row = (int)((gy - 112) / 50);
            if (touch_row >= 0 && touch_row <= 7) {
                cursor = touch_row;
                if (touch_row == 0) touch_slider = 1;
            } else touch_row = -1;
        } else if ((page == MENU_KEYBOARD || page == MENU_PAD) && gy >= 108 && gy < 558) {
            touch_row = (int)((gy - 108) / 50);
            if (touch_row >= 0 && touch_row <= 8) cursor = touch_row;
            else touch_row = -1;
        } else if (page == MENU_CAPTURE) {
            touch_row = 300;
        }
    } else if (action == 2) {
        if (touch_slider && touch_page == MENU_GENERAL && page == MENU_GENERAL && touch_row == 0) {
            float value = (gx - 360) / 340;
            if (value < 0) value = 0;
            if (value > 1) value = 1;
            void_grief_config_command(2, (int)(value * 100 + 0.5f));
        }
    } else if (action == 1) {
        int zone = -1;
        if (page == touch_page) {
            if (gy >= 54 && gy <= 92) zone = 100 + (gx < 260 ? 0 : gx < 520 ? 1 : 2);
            else if (page != MENU_GENERAL && gy >= 94 && gy <= 126 && gx >= 260) {
                zone = (int)((gx - 260) / 170);
                if (zone < 0) zone = 0;
                if (zone > 2) zone = 2;
                zone += 200;
            } else if (page == MENU_GENERAL && gy >= 112 && gy < 512) zone = (int)((gy - 112) / 50);
            else if ((page == MENU_KEYBOARD || page == MENU_PAD) && gy >= 108 && gy < 558) zone = (int)((gy - 108) / 50);
            else if (page == MENU_CAPTURE) zone = 300;
        }
        if (zone == touch_row && zone >= 0) {
            if (zone >= 100 && zone < 103) show_page(zone - 99);
            else if (zone >= 200 && zone < 203) player = zone - 200;
            else if (zone == 300) show_page(capturing == 2 ? MENU_PAD : MENU_KEYBOARD);
            else if (page == MENU_GENERAL && !touch_slider) general_key('Z');
            else if (page == MENU_KEYBOARD || page == MENU_PAD) binding_key('Z');
        } else if (zone < 0 && touch_row < 0 && page == touch_page && page != MENU_CAPTURE) {
            if (page == MENU_GENERAL) void_grief_config_command(11, 0);
            show_page(page == MENU_GENERAL ? 0 : MENU_GENERAL);
        }
        touch_row = -1;
        touch_slider = 0;
        touch_page = -1;
    }
    dirty = 1;
    pthread_mutex_unlock(&menu_lock);
    return 1;
}

int void_grief_menu_back(void)
{
    pthread_mutex_lock(&menu_lock);
    int handled = 1;
    if (pause_scene) {
        request_selection(-1, SELECT_CANCEL);
    } else if (level_scene) {
        request_selection(-1, SELECT_CANCEL);
    } else if (page == MENU_CAPTURE) {
        show_page(capturing == 2 ? MENU_PAD : MENU_KEYBOARD);
    } else if (page == MENU_GENERAL) {
        void_grief_config_command(11, 0);
        show_page(0);
    } else if (page) {
        show_page(MENU_GENERAL);
    } else if (scene) {
        if (!void_grief_lan_active() || void_grief_lan_host()) void_android_config_event(CFG_CONFIRM_EXIT, 0);
    } else {
        handled = 0;
    }
    pthread_mutex_unlock(&menu_lock);
    return handled;
}

void void_grief_menu_confirm(int kind, int accepted)
{
    pthread_mutex_lock(&menu_lock);
    if (accepted && kind == 0) {
        void_grief_config_command(9, 0);
    } else if (accepted && kind == 1 && (!void_grief_lan_active() || void_grief_lan_host())) {
        cursor = 3;
        pending_action = 4;
        pending_stage = 0;
    }
    dirty = 1;
    pthread_mutex_unlock(&menu_lock);
}

static void_fn play_se_original;

static void grief_play_se(void)
{
    uint32_t id = ARG(0) & 0xFFu;
    if (id == 90) { show_page(MENU_GENERAL); return; }
    if (id == 91) { void_android_config_event(CFG_LAN, 1); return; }
    play_se_original();
}

void void_grief_menu_play_se_init(void)
{
    play_se_original = void_dispatch_find(0x00443EE0u);
    if (play_se_original) void_dispatch_add(0x00443EE0u, grief_play_se);
}

static int vk_for_dik(int code)
{
    if (code <= 0) return 0;
    for (int vk = 1; vk < 256; ++vk) if ((int)void_vk_to_dik((uint32_t)vk) == code) return vk;
    return 0;
}

int void_grief_touch_binding(int action)
{
    if (action < 0 || action > 8) return 0;
    int vk = vk_for_dik(void_grief_config_binding((unsigned)action));
    return vk ? vk : default_vk[action];
}

int void_grief_menu_pad_direction(int code, int player)
{
    if (player < 0 || player > 2 || code < 0 || code > 3) return 0;
    if (player > 0) {
        int vk = vk_for_dik(void_grief_config_binding(player * 9 + code));
        if (vk) return vk;
    }
    return default_vk[code];
}

int void_grief_menu_pad_key(int code, int player)
{
    if (player < 0 || player > 2) return 0;
    void_grief_config_command(0, 0);
    int encoded = pad_code(code);
    for (int action = 4; action < 9; ++action) {
        int configured = void_grief_config_binding(30 + player * 9 + action);
        if (configured != encoded) continue;
        if (player == 0) return default_vk[action];
        int dik = void_grief_config_binding(player * 9 + action);
        int vk = vk_for_dik(dik);
        return vk ? vk : default_vk[action];
    }
    return 0;
}

int void_grief_menu_map_dik(uint32_t code)
{
    void_grief_config_command(0, 0);
    for (int action = 0; action < 9; ++action) {
        int binding = void_grief_config_binding((unsigned)action);
        if (binding >= 0 && (uint32_t)binding == code) return (uint32_t)default_dik[action];
        if ((uint32_t)default_dik[action] == code && binding != default_dik[action]) return 0;
    }
    return code;
}

static GLuint menu_texture, menu_program, menu_vbo;
static uint32_t *menu_pixels;

static void menu_viewport(int sw, int sh, int right, int rect[4])
{
    float scale = fminf((float)sw / 800.0f, (float)sh / 600.0f);
    rect[2] = (int)(800.0f * scale);
    rect[3] = (int)(600.0f * scale);
    rect[0] = right ? sw - rect[2] : (sw - rect[2]) / 2;
    rect[1] = (sh - rect[3]) / 2;
}

static GLuint menu_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(shader); return 0; }
    return shader;
}

static void menu_gl_init(void)
{
    if (menu_program) return;
    const char *vs = "attribute vec2 p;attribute vec2 t;varying vec2 uv;void main(){uv=t;gl_Position=vec4(p,0.0,1.0);}";
    const char *fs = "precision mediump float;varying vec2 uv;uniform sampler2D tex;void main(){gl_FragColor=texture2D(tex,uv);}";
    GLuint vertex = menu_shader(GL_VERTEX_SHADER, vs), fragment = menu_shader(GL_FRAGMENT_SHADER, fs);
    if (!vertex || !fragment) return;
    menu_program = glCreateProgram();
    glAttachShader(menu_program, vertex);
    glAttachShader(menu_program, fragment);
    glBindAttribLocation(menu_program, 0, "p");
    glBindAttribLocation(menu_program, 1, "t");
    glLinkProgram(menu_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    glGenTextures(1, &menu_texture);
    glBindTexture(GL_TEXTURE_2D, menu_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenBuffers(1, &menu_vbo);
    menu_pixels = malloc(800 * 600 * 4);
}

static void menu_gl_error(const char *stage) { (void)stage; }

void void_grief_menu_render(int sw, int sh, int dw, int dh, int dx, int dy)
{
    menu_pump();
    if (!scene || sw <= 0 || sh <= 0) return;
    GLint old_texture = 0, old_buffer = 0, old_unit = 0, old_blend = 0, old_src = 0, old_dst = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_buffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_unit);
    glGetIntegerv(GL_BLEND, &old_blend);
    glGetIntegerv(GL_BLEND_SRC_RGB, &old_src);
    glGetIntegerv(GL_BLEND_DST_RGB, &old_dst);
    menu_gl_error("entry");
    menu_gl_init();
    menu_gl_error("init");
    if (!menu_program || !menu_pixels) return;
    pthread_mutex_lock(&menu_lock);
    int right = !page && wide_scene;
    if (dirty) {
        int state[64] = {0};
        state[0] = page;
        state[1] = cursor;
        state[2] = void_grief_config_command(1, 0);
        state[3] = void_grief_config_command(CFG_GET_AA, 0);
        float fps = void_grief_config_fps();
        memcpy(&state[4], &fps, sizeof fps);
        state[5] = player;
        state[6] = binding_kind() + binding_index();
        state[7] = void_ansi_codepage() != 1251;
        for (int i = 0; i < 54; ++i) state[8 + i] = void_grief_config_binding((unsigned)i);
        state[62] = void_grief_config_touch_labels();
        if (void_android_menu_render(state, menu_pixels, 800 * 600)) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, menu_texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 800, 600, 0, GL_RGBA, GL_UNSIGNED_BYTE, menu_pixels);
        }
        dirty = 0;
    }
    pthread_mutex_unlock(&menu_lock);
    menu_gl_error("upload");
    int rect[4];
    menu_viewport(sw, sh, right, rect);
    dx = rect[0]; dy = rect[1]; dw = rect[2]; dh = rect[3];
    float vertices[] = {
        2.0f * dx / sw - 1.0f, 1.0f - 2.0f * (dy + dh) / sh, 0, 1,
        2.0f * (dx + dw) / sw - 1.0f, 1.0f - 2.0f * (dy + dh) / sh, 1, 1,
        2.0f * dx / sw - 1.0f, 1.0f - 2.0f * dy / sh, 0, 0,
        2.0f * (dx + dw) / sw - 1.0f, 1.0f - 2.0f * dy / sh, 1, 0,
    };
    glViewport(0, 0, sw, sh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    GLint old_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
    glUseProgram(menu_program);
    glBindBuffer(GL_ARRAY_BUFFER, menu_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (const void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (const void *)8);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, menu_texture);
    glUniform1i(glGetUniformLocation(menu_program, "tex"), 0);
    menu_gl_error("attributes");
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    menu_gl_error("draw");
    glUseProgram(old_program);
    glBindBuffer(GL_ARRAY_BUFFER, old_buffer);
    glActiveTexture((GLenum)old_unit);
    glBindTexture(GL_TEXTURE_2D, (GLuint)old_texture);
    if (old_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc((GLenum)old_src, (GLenum)old_dst);
    menu_gl_error("restore");
}

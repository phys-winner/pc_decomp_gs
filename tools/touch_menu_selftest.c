/* Run on Android: exercises the production menu state, input and gesture code. */
#include "../app/native/host/grief_menu.c"
#include "../app/native/host/input.c"
#include <assert.h>

int void_verbose;
static int time_ms, qa_events, hint_events, sounds, test_qa, test_lan, config_command, camera_changes;
static int test_host = 1, lan_events, exit_events, gameplay_visibility;
static int test_bindings[9] = {200,208,203,205,44,45,46,30,16};
static int test_width = 1066, test_sw = 1280, test_sh = 720, test_bank = 1;
int void_grief_widescreen_mounted(void) { return test_bank; }
int void_grief_lan_active(void) { return test_lan; }
int void_grief_lan_host(void) { return test_host; }
int void_grief_qa_flags(void) { return test_qa; }
void void_log(const char *fmt, ...) { (void)fmt; }
_Noreturn void void_fatal(const char *fmt, ...) { (void)fmt; abort(); }
uint32_t void_tick_ms(void) { return (uint32_t)time_ms; }
int void_android_display_size(int *w, int *h) { *w = test_sw; *h = test_sh; return 1; }
void void_grief_camera_scene(void) { ++camera_changes; }
void void_grief_render_size(uint32_t *w, uint32_t *h) { *w = test_width; *h = 600; }
void void_regs_save(void_regs *r) { memset(r, 0, sizeof *r); }
void void_regs_load(const void_regs *r) { (void)r; }
uint32_t void_call_guest(uint32_t va, int n, const uint32_t *args, int pops)
{ (void)n; (void)args; (void)pops; assert(va == 0x00443EE0u); ++sounds; return 0; }
void void_android_config_event(int event, float value)
{
    if (event == 26) ++qa_events;
    if (event == 28) ++hint_events;
    if (event == 17) ++lan_events;
    if (event == 25) ++exit_events;
    if (event == 22) gameplay_visibility = (int)value;
}
int void_grief_config_command(int command, int32_t argument)
{ (void)argument; config_command = command; return command == 10; }
int32_t void_grief_config_binding(unsigned i) { return i < 9 ? test_bindings[i] : 0; }
float void_grief_config_fps_cycle(int delta) { (void)delta; return 60; }

static void touch(int action, float x, float y)
{
    float scale = fminf(test_sw / 800.0f, test_sh / 600.0f);
    assert(void_grief_menu_touch(action, ((test_sw - 800 * scale) * 0.5f + x * scale) / test_sw,
        ((test_sh - 600 * scale) * 0.5f + y * scale) / test_sh));
}
static int state(int context, int cursor, int mask)
{ return void_grief_menu_script(0, context | (cursor << 4) | (mask << 8)); }
static void stages(void)
{
    const char *source = "data/system/StageSelect/StageSelect.global.nut";
    void_grief_menu_scene(source, (uint32_t)strlen(source));
    menu_key_count = menu_key_wait = pending_action = 0;
    gameplay_pending = 0;
}
static void lan_key(int expected)
{
    uint8_t seen[256] = {0};
    for (int tick = 0; tick < MENU_KEY_FRAMES; ++tick) {
        uint8_t keys[256];
        menu_pump();
        void_input_keyboard_snapshot(keys);
        for (int i = 0; i < 256; ++i) seen[i] |= keys[i];
    }
    for (int i = 0; i < 256; ++i)
        assert(!!(seen[i] & 0x80) == (expected && i == (int)void_vk_to_dik((uint32_t)expected)));
    assert(!script_request);
}
int main(void)
{
    int alpha = -1;
    assert(void_grief_widescreen_state(&alpha) == -1 && alpha == 0);
    void_grief_widescreen_scene(0, -1);
    assert(void_grief_widescreen_state(&alpha) == 0 && alpha == 0);
    void_grief_widescreen_scene(0, 2000);
    assert(void_grief_widescreen_state(&alpha) == 0 && alpha == 1000);
    void_grief_widescreen_scene(7, 100);
    assert(void_grief_widescreen_state(&alpha) == 0 && alpha == 1000);
    void_grief_menu_script(7, (6 << 10) | 420);
    assert(void_grief_widescreen_state(&alpha) == 6 && alpha == 420);
    state(6, 0, 0);
    assert(void_grief_menu_script(8, 0) == 6);
    int before_result = camera_changes;
    state(10, 0, 0);
    assert(camera_changes == before_result && void_grief_menu_script(8, 0) == 6);
    state(7, 0, 0);
    assert(void_grief_widescreen_state(&alpha) == -1 && alpha == 0);
    void_grief_menu_script(7, (6 << 10) | 420);
    state(6, 0, 0);
    assert(void_grief_widescreen_state(&alpha) == 6 && alpha == 420);
    const char *stage_main = "data/system/StageMain/StageMain.global.nut";
    void_grief_menu_scene(stage_main, (uint32_t)strlen(stage_main));
    assert(void_grief_menu_script(8, 0) == -1);
    before_result = camera_changes;
    state(10, 0, 0);
    assert(camera_changes == before_result + 1);
    stages();
    assert(void_grief_widescreen_state(&alpha) == 1 && alpha == 0);
    state(7, 0, 0);
    assert(!void_grief_menu_wide() && void_grief_menu_script(5, 0) == 800);
    void_grief_menu_script(10, 1);
    assert(void_grief_menu_wide() && void_grief_menu_script(5, 0) == 1066);
    assert(!level_scene && !pause_scene);
    void_grief_menu_script(10, 0);
    assert(!void_grief_menu_wide() && void_grief_menu_script(5, 0) == 800);
    void_grief_menu_script(10, 1);
    state(6, 0, 0);
    void_grief_menu_script(10, 0);
    assert(void_grief_menu_wide() && void_grief_menu_script(5, 0) == 1066);
    void_grief_menu_script(10, 1);
    stages();
    assert(!loading_visible && !void_grief_menu_wide());
    for (int target = 0; target < 5; ++target) {
        stages(); state(3, 2, 0);
        touch(0, 87.5f + 150 * target, 200);
        touch(1, 87.5f + 150 * target, 200);
        assert(state(3, 2, 0) == (256 | (target + 1)));
        assert(!menu_key_count && !menu_key_wait && !gameplay_pending);
        assert(state(3, target, 0) == 0);
    }
    stages(); state(3, 2, 0);
    touch(0, 87, 200); touch(3, 0, 0); touch(1, 87, 200);
    assert(state(3, 2, 0) == 0);
    touch(0, 87, 200); time_ms += 499; menu_pump();
    assert(state(3, 2, 0) == 1);
    time_ms += 1; menu_pump();
    assert(state(3, 0, 0) == (512 | 1));
    touch(1, 87, 200);
    assert(!state(3, 0, 0));
    /* A confirmation never carries a variant or keeps a synthetic direction key held. */
    touch(0, 87, 200); touch(1, 87, 200);
    assert(state(3, 0, 0) == (256 | 1));
    { uint8_t keys[256]; void_input_keyboard_snapshot(keys); assert(!(keys[void_vk_to_dik(0x26)] & 0x80)); }
    state(6, 0, 0); assert(wide_scene);
    assert(void_grief_menu_script(5, 0) == 1066);
    state(10, 0, 0);
    assert(!wide_scene && !level_scene && !gameplay_pending);
    assert(void_grief_menu_script(5, 0) == 800);
    assert(void_grief_menu_script(2, 0) == 1066);
    stages(); state(3, 0, 0);
    /* Every Homura hold asks the script to toggle; repeated holds alternate in the script. */
    for (int hold = 0; hold < 4; ++hold) {
        touch(0, 87, 200); time_ms += 500; menu_pump();
        assert(state(3, 0, 0) == 513);
        touch(1, 87, 200); assert(!state(3, 0, 0));
    }
    /* Holding another heroine selects her without a variant request. */
    touch(0, 237, 200); time_ms += 500; menu_pump();
    assert(state(3, 1, 0) == 2);
    touch(1, 237, 200); assert(!state(3, 1, 0));
    touch(0, 87, 200); touch(1, 87, 200);
    assert(state(3, 0, 0) == 257);
    touch(0, 87, 200); time_ms += 500; touch(1, 87, 200);
    assert(state(3, 0, 0) == 513);
    assert(void_grief_menu_back());
    assert(state(3, 0, 0) == 1024);
    state(2, 0, 1); state(3, 0, 0);
    touch(0, 87, 200); touch(1, 87, 200);
    assert(state(3, 0, 0) == 257);
    touch(0, 87, 200); time_ms += 500; touch(1, 87, 200);
    assert(state(3, 0, 0) == 513);
    touch(0, 87, 200); touch(1, 87, 200);
    assert(state(3, 0, 0) == 257);
    state(2, 0, 1);
    state(3, 0, 0);
    touch(0, 87, 200); touch(2, 400, 550); touch(1, 400, 550);
    assert(!(state(3, 0, 0) & 1024));
    const float outside[][2] = {{400, 550}, {-100, 200}, {900, 200}, {400, 10}};
    for (unsigned i = 0; i < sizeof outside / sizeof *outside; ++i) {
        touch(0, outside[i][0], outside[i][1]); touch(1, outside[i][0], outside[i][1]);
        assert(state(3, 0, 0) == 1024);
    }
    touch(0, 400, 550); touch(3, 0, 0); touch(1, 400, 550);
    assert(!state(3, 0, 0));
    assert(hint_events);
    stages(); state(9, 2, 0);
    touch(0, 687, 200); touch(1, 687, 200);
    assert(state(9, 2, 0) == 0);
    assert(state(3, 2, 0) == (256 | 5));

    stages(); state(2, 0, 33);
    touch(0, 200, 400); touch(1, 200, 400);
    assert(state(2, 0, 33) == (256 | 6));
    touch(0, 400, 80); touch(1, 400, 80);
    assert(state(2, 0, 33) == 0);
    assert(stage_available == 33);
    touch(0, 200, 400); touch(2, 200, 100); touch(1, 200, 100);
    assert((state(2, 0, 33) & 256) == 0);
    int first[] = {20, 90, 270, 280}, last[] = {500, 220, 650, 360}, exit[] = {140, 300, 210, 400};
    void_grief_menu_rect(0, first); void_grief_menu_rect(5, last); void_grief_menu_rect(6, exit);
    assert(stage_zone(550, 250) == 5 && stage_zone(175, 350) == 6);
    assert(stage_zone(400, 80) == -1);
    touch(0, 175, 350); touch(1, 175, 350);
    assert(state(2, 0, 33) == 1024);

    state(4, 0, 0);
    touch(0, 400, 52); touch(1, 400, 52);
    assert(state(4, 0, 0) == (256 | 2));
    touch(0, 400, 80); touch(1, 400, 80);
    assert(state(4, 0, 0) == (256 | 3));
    state(5, 1, 0);
    touch(0, 320, 300); touch(1, 320, 300);
    assert(state(5, 1, 0) == (256 | 1));
    touch(0, 390, 300); touch(1, 390, 300);
    assert(state(5, 1, 0) == (256 | 2));
    assert(void_grief_menu_back());
    assert(state(5, 1, 0) == 1024 && pause_scene);
    state(4, 0, 0); assert(void_grief_menu_back());
    assert(state(4, 0, 0) == 1024);
    state(6, 0, 0); assert(!pause_scene);
    state(4, 0, 0); touch(0, 400, 20); touch(2, 400, 65); touch(1, 400, 65);
    assert((state(4, 0, 0) & 256) == 0);
    touch(0, 400, 20); touch(3, 0, 0); touch(1, 400, 20);
    assert(!state(4, 0, 0));
    state(8, 0, 0); touch(0, 400, 300); touch(1, 400, 300);
    assert(state(8, 0, 0) == 1025);
    touch(0, -100, 300); touch(1, -100, 300);
    assert(state(8, 0, 0) == 1025);
    touch(0, 900, 300); touch(1, 900, 300);
    assert(state(8, 0, 0) == 1025);

    const char *title = "data/system/Title/Title.global.nut";
    void_grief_menu_scene(title, (uint32_t)strlen(title));
    assert(void_grief_widescreen_state(&alpha) == 0 && alpha == 0);
    gameplay_pending = pending_action = 0;
    show_page(MENU_GENERAL);
    float original_item[28] = {0};
    for (int i = 0; i < 4; ++i) {
        original_item[i * 7] = (i & 1) ? 790 : 500;
        original_item[i * 7 + 1] = i >= 2 ? 280 : 250;
    }
    assert(void_grief_menu_suppress_item(original_item, 28));
    show_page(MENU_KEYBOARD); assert(void_grief_menu_suppress_item(original_item, 28));
    show_page(MENU_GENERAL);
    cursor = 7; general_key(0x28); assert(cursor == 0);
    general_key(0x26); assert(cursor == 7);
    general_key('Z'); assert(!page && config_command == 11);
    show_page(MENU_GENERAL);
    touch(0, 400, 437); touch(1, 400, 437);
    assert(!page && config_command == 10);
    show_page(MENU_GENERAL);
    touch(0, 400, 487); touch(1, 400, 487);
    assert(!page && config_command == 11);
    touch(0, 200, 500); time_ms += 2999; menu_pump(); assert(!qa_events);
    time_ms += 1; menu_pump(); assert(qa_events == 1);
    time_ms += 4000; menu_pump(); assert(qa_events == 1);
    touch(1, 200, 500);
    touch(0, 200, 500); touch(2, 280, 500); time_ms += 4000; menu_pump(); assert(qa_events == 1);
    touch(3, 0, 0);
    touch(0, 600, 325); touch(3, 0, 0); menu_pump(); assert(sounds);
    for (int width = 800; width <= 2134; width = width == 800 ? 1066 : width == 1066 ? 2134 : 2135) {
        test_width = test_sw = width; test_sh = 600;
        void_grief_menu_scene(title, (uint32_t)strlen(title));
        assert(void_grief_menu_wide() && void_grief_menu_script(5, 0) == width);
        int rect[4]; menu_viewport(test_sw, test_sh, 1, rect);
        assert(rect[0] == width - 800 && rect[1] == 0 && rect[2] == 800 && rect[3] == 600);
        assert(void_grief_menu_touch(0, (width - 155.0f) / width, 325.0f / 600));
        assert(void_grief_menu_touch(1, (width - 155.0f) / width, 325.0f / 600));
        assert(page == MENU_GENERAL); /* The visible right-anchored third row. */
        menu_viewport(test_sw, test_sh, 0, rect);
        assert(rect[0] == (width - 800) / 2 && rect[2] == 800);
        touch(0, 400, 487); touch(1, 400, 487);
        assert(!page && config_command == 11); /* Settings keep the centered canvas. */
        int before = qa_events;
        assert(void_grief_menu_touch(0, 100.0f / width, 500.0f / 600));
        time_ms += 3000; menu_pump();
        assert(qa_events == before + 1);
        assert(void_grief_menu_touch(1, 100.0f / width, 500.0f / 600));
        if (width == 2134) {
            cursor = 0;
            assert(void_grief_menu_touch(0, 645.0f / width, 325.0f / 600));
            assert(void_grief_menu_touch(1, 645.0f / width, 325.0f / 600));
            assert(!page && !pending_action && cursor == 0); /* Old center hitbox is inactive. */
        }
    }
    test_bank = 0;
    void_grief_menu_scene(title, (uint32_t)strlen(title));
    assert(!void_grief_menu_wide() && void_grief_menu_script(5, 0) == 800);
    touch(0, 645, 325); touch(1, 645, 325); assert(page == MENU_GENERAL);
    show_page(0); test_bank = 1;
    test_width = 1066; test_sw = 1280; test_sh = 720;
    test_lan = 1;
    assert(void_grief_menu_script(6, 0));
    stages(); assert(level_scene && !wide_scene && !gameplay_visibility);
    assert(!void_grief_menu_suppress_item(original_item, 28));
    { uint8_t keys[256]; void_input_keyboard_snapshot(keys); }
    test_host = 0;
    state(2, 0, 63);
    touch(0, 700, 100); touch(1, 700, 100);
    assert(lan_target == -2 && !state(2, 0, 63)); lan_key(0);
    test_host = 1;
    touch(0, 700, 100); touch(1, 700, 100);
    assert(lan_target == 2 && !state(2, 0, 63)); lan_key(0x27);
    lan_key(0); /* No repeated key until synchronized cursor feedback. */
    state(2, 1, 63); lan_key(0x27);
    state(2, 2, 63); lan_key('Z');
    state(9, 4, 2); lan_key(0);
    touch(0, 87, 200); touch(1, 87, 200); lan_key(0);
    state(3, 4, 2); lan_key('X'); /* Unready a confirmed character before changing it. */
    lan_key(0);
    for (int selected = 4; selected > 0; --selected) {
        assert(!state(3, selected, 1)); lan_key(0x25);
    }
    state(3, 0, 1); lan_key('Z');
    state(3, 0, 2); lan_key(0);
    for (int glasses = 0; glasses < 2; ++glasses) {
        state(3, 0, 2 | (glasses << 2));
        touch(0, 87, 200); time_ms += 500; menu_pump();
        /* menu_pump has already delivered the unready key. */
        { uint8_t keys[256]; void_input_keyboard_snapshot(keys); assert(keys[void_vk_to_dik('X')] & 0x80); }
        state(3, 0, 1 | (glasses << 2)); lan_key(glasses ? 0x28 : 0x26);
        state(3, 0, 1 | ((!glasses) << 2));
        touch(1, 87, 200); lan_key(0); /* Long release never confirms. */
    }
    touch(0, 400, 550); touch(1, 400, 550); lan_key('X');
    state(2, 2, 63);
    void_grief_menu_back(); lan_key(0x28);
    assert(lan_waiting && lan_target == -1);
    void_grief_menu_back(); lan_key(0x28); /* Same-target cancel can retry an ignored key. */
    state(2, 2, 63 | 64); lan_key('Z'); /* Stage exit is a separate focused item. */
    state(3, 2, 1); request_selection(4, SELECT_CONFIRM);
    state(7, 0, 0); lan_key(0); assert(lan_target == -2);
    stages(); state(3, 2, 1); request_selection(4, SELECT_CONFIRM);
    void_grief_menu_touch(3, 0, 0); lan_key(0);
    assert(lan_target == -2);
    void_grief_menu_scene(title, (uint32_t)strlen(title));
    test_host = 0;
    activate_touch(0); activate_touch(3);
    assert(!pending_action);
    cursor = 0; assert(void_grief_menu_key('Z', 1) && !pending_action);
    cursor = 3; assert(void_grief_menu_key('Z', 1) && !pending_action);
    int old_exits = exit_events, old_lan = lan_events;
    assert(void_grief_menu_back() && exit_events == old_exits);
    void_grief_menu_confirm(1, 1); assert(!pending_action);
    activate_touch(1); assert(lan_events == old_lan + 1);
    activate_touch(2); assert(page == MENU_GENERAL);
    show_page(0); test_host = 1;
    activate_touch(0); assert(pending_action == 1); pending_action = 0;
    assert(void_grief_menu_back() && exit_events == old_exits + 1);
    /* Rebound P1 keys apply once at injection, including Title's semantic input sequence. */
    test_bindings[0] = (int)void_vk_to_dik('I');
    test_bindings[4] = default_dik[5]; test_bindings[5] = default_dik[4];
    activate_touch(0); lan_key('I'); pending_action = 0;
    stages(); state(2, 2, 63); request_selection(2, SELECT_CONFIRM); lan_key('X');
    assert(!state(2, 2, 63));
    inject('X', 1); inject('X', 0); lan_key('Z');
    test_lan = 0;
    inject('Z', 1); inject('Z', 0); lan_key('Z'); /* Solo keeps canonical guest actions. */
    puts("PASS: solo touch/hold/cancel, title layout and settings; LAN host-only stages/Title, synchronized key feedback, confirmed character changes, consumed variant release, exit focus and scene/cancel reset");
    return 0;
}

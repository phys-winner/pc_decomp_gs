#ifndef GRIEF_MENU_H
#define GRIEF_MENU_H
#include <stdint.h>

void void_grief_menu_scene(const char *source, uint32_t size);
int void_grief_menu_key(uint32_t vk, int down);
int void_grief_menu_touch(int action, float x, float y);
int void_grief_menu_back(void);
void void_grief_menu_confirm(int kind, int accepted);
int void_grief_menu_capture(int source, int code);
int void_grief_menu_active(void);
int void_grief_menu_wide(void);
/* The presenter shows the whole widened target; victories narrow it only for LAN's 800-wide camera. */
int void_grief_present_wide(void);
int void_grief_menu_result_visible(void);
void void_grief_widescreen_scene(int id, int alpha);
int void_grief_widescreen_state(int *alpha);
void void_grief_render_size(uint32_t *width, uint32_t *height);
void void_grief_camera_stream(uint32_t address, uint32_t size);
void void_grief_camera_scene(void);
void void_grief_set_skip_logo(int value);
struct d3d_device;
float void_grief_hud_offset(const struct d3d_device *dev, float left);
void void_grief_menu_render(int sw, int sh, int dw, int dh, int dx, int dy);
int void_grief_menu_pad_key(int code, int player);
int void_grief_menu_pad_direction(int code, int player);
int void_grief_menu_map_dik(uint32_t code);
int void_grief_menu_android_vk(int code);
int void_grief_touch_binding(int action);
void void_grief_menu_play_se_init(void);

#endif

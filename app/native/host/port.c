#include "host.h"
#include "d3d9_internal.h"

static uint32_t fps_limit_bits = 0x42700000u;

float void_port_fps_limit(void)
{
    uint32_t bits = __atomic_load_n(&fps_limit_bits, __ATOMIC_ACQUIRE);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

void void_port_set_fps_limit(float value)
{
    if (!(value >= 1.0f && value <= 240.0f)) value = 60.0f;
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    __atomic_store_n(&fps_limit_bits, bits, __ATOMIC_RELEASE);
}

void void_touch_pump(void) {}
void void_touch_render_frame_pump(void) {}
void void_touch_frame_presented(void) {}

int d3d_native_take(uint32_t width, uint32_t height, uint32_t format, const d3d_native_format **out)
{
    (void)width;
    (void)height;
    (void)format;
    (void)out;
    return 0;
}

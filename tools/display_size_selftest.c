/* Use the production cache while Android replaces the initial portrait surface. */
#define VOID_ANDROID_APP
#include "../app/native/host/android_main.c"
#include <assert.h>

int main(void)
{
    remember_display_size_locked(960, 1680);
    assert(display_width == 0 && display_height == 0);
    remember_display_size_locked(1680, 960);
    remember_display_size_locked(960, 1680);
    remember_display_size_locked(0, 0);
    assert(display_width == 1680 && display_height == 960);
    remember_display_size_locked(1920, 540);
    assert(display_width == 1920 && display_height == 540);
    remember_display_size_locked(1680, 1264);
    assert(display_width == 1680 && display_height == 1264);
    puts("PASS: landscape display cache ignores transient portrait/empty surfaces and accepts 32:9 and physical 4:3 sizes");
    return 0;
}

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int device;
static unsigned int active;

static void fail(const char *what)
{
    fprintf(stderr, "%s: %s\n", what, strerror(errno));
    _exit(1);
}

static void event(unsigned short type, unsigned short code, int value)
{
    struct input_event event;
    memset(&event, 0, sizeof event);
    event.type = type;
    event.code = code;
    event.value = value;
    if (write(device, &event, sizeof event) != sizeof event) fail("write input event");
}

static void report(void)
{
    event(EV_SYN, SYN_REPORT, 0);
}

static void absolute(unsigned short code, int minimum, int maximum, int value)
{
    if (ioctl(device, UI_SET_ABSBIT, code) < 0) fail("UI_SET_ABSBIT");
    struct uinput_abs_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.code = code;
    setup.absinfo.minimum = minimum;
    setup.absinfo.maximum = maximum;
    setup.absinfo.value = value;
    if (ioctl(device, UI_ABS_SETUP, &setup) < 0) fail("UI_ABS_SETUP");
}

static void create_touchscreen(void)
{
    device = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (device < 0) fail("open /dev/uinput");
    if (ioctl(device, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(device, UI_SET_KEYBIT, BTN_TOUCH) < 0)
        fail("UI_SET_KEYBIT BTN_TOUCH");
    if (ioctl(device, UI_SET_EVBIT, EV_ABS) < 0) fail("UI_SET_EVBIT EV_ABS");
    absolute(ABS_MT_SLOT, 0, 9, 0);
    absolute(ABS_MT_TRACKING_ID, 0, 65535, 0);
    absolute(ABS_MT_POSITION_X, 0, 1079, 0);
    absolute(ABS_MT_POSITION_Y, 0, 2339, 0);
    if (ioctl(device, UI_SET_PROPBIT, INPUT_PROP_DIRECT) < 0) fail("UI_SET_PROPBIT");
    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    strcpy(setup.name, "GS Virtual Touchscreen");
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1209;
    setup.id.product = 0x4755;
    setup.id.version = 1;
    if (ioctl(device, UI_DEV_SETUP, &setup) < 0) fail("UI_DEV_SETUP touchscreen");
    if (ioctl(device, UI_DEV_CREATE) < 0) fail("UI_DEV_CREATE touchscreen");
    puts("created GS Virtual Touchscreen");
}

static void down(unsigned int slot, int id, int x, int y)
{
    event(EV_ABS, ABS_MT_SLOT, (int)slot);
    event(EV_ABS, ABS_MT_TRACKING_ID, id);
    event(EV_ABS, ABS_MT_POSITION_X, 1079 - y);
    event(EV_ABS, ABS_MT_POSITION_Y, x);
    active |= 1u << slot;
    event(EV_KEY, BTN_TOUCH, 1);
    report();
    printf("down %u %d %d %d\n", slot, id, x, y);
}

static void move(unsigned int slot, int x, int y)
{
    event(EV_ABS, ABS_MT_SLOT, (int)slot);
    event(EV_ABS, ABS_MT_POSITION_X, 1079 - y);
    event(EV_ABS, ABS_MT_POSITION_Y, x);
    report();
    printf("move %u %d %d\n", slot, x, y);
}

static void up(unsigned int slot)
{
    event(EV_ABS, ABS_MT_SLOT, (int)slot);
    event(EV_ABS, ABS_MT_TRACKING_ID, -1);
    active &= ~(1u << slot);
    event(EV_KEY, BTN_TOUCH, active != 0);
    report();
    printf("up %u\n", slot);
}

static void tap(unsigned int slot, int id, int x, int y)
{
    down(slot, id, x, y);
    usleep(80000);
    up(slot);
}

int main(int argc, char **argv)
{
    int x = argc >= 4 ? atoi(argv[2]) : -1;
    int y = argc >= 4 ? atoi(argv[3]) : -1;
    int single_tap = argc == 4 && !strcmp(argv[1], "tap") && x >= 0 && x < 2340 && y >= 0 && y < 1080;
    int hold_ms = argc == 5 ? atoi(argv[4]) : -1;
    int hold = argc == 5 && !strcmp(argv[1], "hold") && x >= 0 && x < 2340 && y >= 0 && y < 1080 &&
        hold_ms >= 0 && hold_ms <= 10000;
    int x2 = argc == 6 ? atoi(argv[4]) : -1;
    int y2 = argc == 6 ? atoi(argv[5]) : -1;
    int swipe = argc == 6 && !strcmp(argv[1], "swipe") && x >= 0 && x < 2340 && y >= 0 && y < 1080 &&
        x2 >= 0 && x2 < 2340 && y2 >= 0 && y2 < 1080;
    if (!single_tap && !hold && !swipe && (argc != 2 || (strcmp(argv[1], "play") && strcmp(argv[1], "menu") && strcmp(argv[1], "layout") &&
        strcmp(argv[1], "settings") && strcmp(argv[1], "bindings") && strcmp(argv[1], "save") &&
        strcmp(argv[1], "pad") && strcmp(argv[1], "volume") && strcmp(argv[1], "resetlayout") &&
        strcmp(argv[1], "start") && strcmp(argv[1], "z") && strcmp(argv[1], "defaults")))) {
        fprintf(stderr, "usage: virtual_touch play|menu|layout|settings|bindings|save|pad|volume|resetlayout|start|z|defaults|tap X Y|hold X Y MS|swipe X1 Y1 X2 Y2\n");
        return 64;
    }
    create_touchscreen();
    sleep(1);
    if (hold) {
        down(0, 100, 310, 860);
        move(0, x, y);
        usleep((useconds_t)hold_ms * 1000);
        up(0);
    } else if (swipe) {
        down(0, 100, x, y);
        usleep(80000);
        for (int step = 1; step <= 12; ++step) {
            move(0, x + (x2 - x) * step / 12, y + (y2 - y) * step / 12);
            usleep(16000);
        }
        usleep(80000);
        up(0);
    } else if (single_tap) {
        tap(0, 100, x, y);
    } else if (!strcmp(argv[1], "defaults")) {
        tap(0, 100, 1567, 492);
        sleep(1);
        tap(0, 101, 1170, 604);
        usleep(300000);
        tap(0, 102, 1170, 345);
        usleep(300000);
        tap(0, 103, 730, 155);
        usleep(300000);
        tap(0, 104, 1170, 690);
    } else if (!strcmp(argv[1], "z")) {
        tap(0, 100, 1938, 860);
    } else if (!strcmp(argv[1], "start")) {
        tap(0, 100, 1567, 454);
    } else if (!strcmp(argv[1], "settings")) {
        tap(0, 100, 1567, 492);
    } else if (!strcmp(argv[1], "bindings")) {
        tap(0, 100, 1567, 492);
        sleep(1);
        tap(0, 101, 1178, 155);
        usleep(500000);
        tap(0, 102, 650, 279);
    } else if (!strcmp(argv[1], "pad")) {
        tap(0, 100, 1567, 492);
        sleep(1);
        tap(0, 101, 1626, 155);
        usleep(500000);
        tap(0, 102, 650, 279);
    } else if (!strcmp(argv[1], "save")) {
        tap(0, 100, 730, 155);
        usleep(500000);
        tap(0, 101, 1170, 690);
    } else if (!strcmp(argv[1], "volume")) {
        tap(0, 100, 1567, 492);
        sleep(1);
        tap(0, 101, 1400, 259);
    } else if (!strcmp(argv[1], "resetlayout")) {
        tap(0, 100, 2274, 55);
        sleep(1);
        tap(0, 101, 1170, 585);
        sleep(1);
        for (int i = 0; i < 6; ++i) tap(0, 110 + i, 1848, 228);
        tap(0, 120, 186, 70);
        tap(0, 121, 2240, 35);
        sleep(1);
    } else if (!strcmp(argv[1], "menu")) {
        tap(0, 100, 2274, 55);
    } else if (!strcmp(argv[1], "layout")) {
        tap(0, 100, 2274, 55);
        sleep(1);
        tap(0, 101, 1170, 585);
        sleep(1);
        down(0, 102, 2250, 765);
        usleep(100000);
        move(0, 400, 820);
        usleep(100000);
        up(0);
        tap(0, 103, 2004, 228);
        tap(0, 104, 2240, 35);
        sleep(1);
    } else {
        for (int i = 0; i < 3; ++i) {
            tap(0, 100 + i, 1938, 860);
            sleep(2);
        }
        down(0, 110, 310, 860);
        move(0, 390, 860);
        down(1, 111, 1938, 860);
        usleep(700000);
        up(1);
        move(0, 310, 860);
        up(0);
        tap(0, 112, 2110, 696);
        sleep(1);
        tap(0, 113, 2274, 855);
        sleep(1);
        tap(0, 114, 2110, 1014);
        sleep(1);
        down(0, 115, 310, 960);
        down(1, 116, 2110, 696);
        usleep(300000);
        up(1);
        up(0);
        sleep(1);
    }
    if (ioctl(device, UI_DEV_DESTROY) < 0) fail("UI_DEV_DESTROY");
    close(device);
    return 0;
}

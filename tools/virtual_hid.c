#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int device;

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

static void key(unsigned short code, int down)
{
    event(EV_KEY, code, down);
    event(EV_SYN, SYN_REPORT, 0);
    printf("%s %d\n", down ? "down" : "up", code);
}

static void tap(unsigned short code)
{
    key(code, 1);
    usleep(60000);
    key(code, 0);
}

static void axis(unsigned short code, int value)
{
    event(EV_ABS, code, value);
    event(EV_SYN, SYN_REPORT, 0);
    printf("axis %u %d\n", code, value);
}

static void create_keyboard(void)
{
    static const unsigned short keys[] = {
        KEY_Z, KEY_X, KEY_A, KEY_C, KEY_Q, KEY_ESC, KEY_ENTER,
        KEY_I, KEY_J, KEY_L, KEY_N, KEY_M, KEY_COMMA, KEY_H, KEY_U,
        KEY_KP8, KEY_KP2, KEY_KP4, KEY_KP6, KEY_KP0, KEY_KPDOT, KEY_KPENTER, KEY_KPPLUS, KEY_KPMINUS,
        KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_K, KEY_TAB, KEY_1, KEY_2, KEY_3,
        KEY_HOME, KEY_ALL_APPLICATIONS, KEY_HOMEPAGE, KEY_POWER,
    };
    device = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (device < 0) fail("open /dev/uinput");
    if (ioctl(device, UI_SET_EVBIT, EV_KEY) < 0) fail("UI_SET_EVBIT EV_KEY");
    for (unsigned int i = 0; i < sizeof keys / sizeof keys[0]; ++i)
        if (ioctl(device, UI_SET_KEYBIT, keys[i]) < 0) fail("UI_SET_KEYBIT");
    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    strcpy(setup.name, "GS Virtual Keyboard");
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1209;
    setup.id.product = 0x4753;
    setup.id.version = 1;
    if (ioctl(device, UI_DEV_SETUP, &setup) < 0) fail("UI_DEV_SETUP keyboard");
    if (ioctl(device, UI_DEV_CREATE) < 0) fail("UI_DEV_CREATE keyboard");
    printf("created GS Virtual Keyboard\n");
}

static void create_gamepad(void)
{
    static const unsigned short keys[] = { BTN_A, BTN_B, BTN_X, BTN_Y, BTN_START };
    device = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (device < 0) fail("open /dev/uinput");
    if (ioctl(device, UI_SET_EVBIT, EV_KEY) < 0) fail("UI_SET_EVBIT EV_KEY");
    for (unsigned int i = 0; i < sizeof keys / sizeof keys[0]; ++i)
        if (ioctl(device, UI_SET_KEYBIT, keys[i]) < 0) fail("UI_SET_KEYBIT");
    if (ioctl(device, UI_SET_EVBIT, EV_ABS) < 0) fail("UI_SET_EVBIT EV_ABS");
    if (ioctl(device, UI_SET_ABSBIT, ABS_X) < 0 || ioctl(device, UI_SET_ABSBIT, ABS_Y) < 0 ||
        ioctl(device, UI_SET_ABSBIT, ABS_HAT0X) < 0 || ioctl(device, UI_SET_ABSBIT, ABS_HAT0Y) < 0)
        fail("UI_SET_ABSBIT");
    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    strcpy(setup.name, "GS Virtual Gamepad");
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1209;
    setup.id.product = 0x4754;
    setup.id.version = 1;
    if (ioctl(device, UI_DEV_SETUP, &setup) < 0) fail("UI_DEV_SETUP gamepad");
    static const unsigned short axes[] = { ABS_X, ABS_Y, ABS_HAT0X, ABS_HAT0Y };
    for (unsigned int i = 0; i < sizeof axes / sizeof axes[0]; ++i) {
        int hat = i >= 2;
        struct uinput_abs_setup axis_setup;
        memset(&axis_setup, 0, sizeof axis_setup);
        axis_setup.code = axes[i];
        axis_setup.absinfo.minimum = hat ? -1 : 0;
        axis_setup.absinfo.maximum = hat ? 1 : 255;
        axis_setup.absinfo.value = hat ? 0 : 128;
        axis_setup.absinfo.flat = hat ? 0 : 8;
        if (ioctl(device, UI_ABS_SETUP, &axis_setup) < 0) fail("UI_ABS_SETUP");
    }
    if (ioctl(device, UI_DEV_CREATE) < 0) fail("UI_DEV_CREATE gamepad");
    printf("created GS Virtual Gamepad\n");
}

static void console(int keyboard)
{
    char line[64];
    sleep(1);
    puts("ready");
    fflush(stdout);
    while (fgets(line, sizeof line, stdin)) {
        char name[32];
        int milliseconds = 100;
        if (!strncmp(line, "raw ", 4)) {
            unsigned int raw;
            if (sscanf(line + 4, "%u %d", &raw, &milliseconds) == 2 && milliseconds >= 0 && milliseconds <= 10000) {
                key((unsigned short)raw, 1);
                usleep((useconds_t)milliseconds * 1000);
                key((unsigned short)raw, 0);
                fflush(stdout);
            }
            continue;
        }
        unsigned int input_code;
        int value;
        if (sscanf(line, "down %u", &input_code) == 1 && input_code <= 65535) {
            key((unsigned short)input_code, 1);
            fflush(stdout);
            continue;
        }
        if (sscanf(line, "up %u", &input_code) == 1 && input_code <= 65535) {
            key((unsigned short)input_code, 0);
            fflush(stdout);
            continue;
        }
        if (sscanf(line, "axis %u %d", &input_code, &value) == 2 && input_code <= 65535) {
            axis((unsigned short)input_code, value);
            fflush(stdout);
            continue;
        }
        if (sscanf(line, "%31s %d", name, &milliseconds) < 1) continue;
        if (!strcmp(name, "quit")) break;
        if (milliseconds < 0 || milliseconds > 10000) continue;
        if (!strcmp(name, "sleep")) {
            usleep((useconds_t)milliseconds * 1000);
            fflush(stdout);
            continue;
        }
        unsigned short code = 0;
        if (!strcmp(name, "confirm")) code = keyboard ? KEY_Z : BTN_A;
        else if (!strcmp(name, "cancel")) code = keyboard ? KEY_X : BTN_B;
        else if (!strcmp(name, "magic")) code = keyboard ? KEY_A : BTN_X;
        else if (!strcmp(name, "jump")) code = keyboard ? KEY_C : BTN_Y;
        else if (!strcmp(name, "pause")) code = keyboard ? KEY_Q : BTN_START;
        else if (!strcmp(name, "wake") && keyboard) code = KEY_POWER;
        if (code) {
            key(code, 1);
            usleep((useconds_t)milliseconds * 1000);
            key(code, 0);
        } else if (!strcmp(name, "left") || !strcmp(name, "right") || !strcmp(name, "up") || !strcmp(name, "down")) {
            unsigned short axis_code = !strcmp(name, "left") || !strcmp(name, "right") ? ABS_HAT0X : ABS_HAT0Y;
            int value = !strcmp(name, "left") || !strcmp(name, "up") ? -1 : 1;
            axis(axis_code, value);
            usleep((useconds_t)milliseconds * 1000);
            axis(axis_code, 0);
        } else if (!strcmp(name, "release")) {
            axis(ABS_X, 128); axis(ABS_Y, 128); axis(ABS_HAT0X, 0); axis(ABS_HAT0Y, 0);
        }
        fflush(stdout);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "keyboard") && strcmp(argv[1], "gamepad") && strcmp(argv[1], "story") &&
        strcmp(argv[1], "console") && strcmp(argv[1], "keys"))) {
        fprintf(stderr, "usage: virtual_hid keyboard|gamepad|story|console|keys\n");
        return 64;
    }
    int keyboard = !strcmp(argv[1], "keyboard") || !strcmp(argv[1], "keys");
    if (keyboard) create_keyboard(); else create_gamepad();
    if (!strcmp(argv[1], "console") || !strcmp(argv[1], "keys")) {
        console(keyboard);
        ioctl(device, UI_DEV_DESTROY);
        close(device);
        return 0;
    }
    sleep(1);
    for (int i = 0; i < (!strcmp(argv[1], "story") ? 6 : 3); ++i) {
        if (!strcmp(argv[1], "story")) {
            key(BTN_A, 1);
            usleep(300000);
            key(BTN_A, 0);
            sleep(3);
        } else {
            tap(keyboard ? KEY_Z : BTN_A);
            sleep(2);
        }
    }
    if (!strcmp(argv[1], "story")) {
        sleep(8);
    } else if (keyboard) {
        key(KEY_RIGHT, 1);
        sleep(1);
        key(KEY_RIGHT, 0);
        tap(KEY_C);
        sleep(1);
        tap(KEY_Z);
        sleep(1);
        tap(KEY_X);
        sleep(1);
        tap(KEY_A);
        sleep(1);
        tap(KEY_Q);
    } else {
        axis(ABS_X, 255);
        sleep(1);
        axis(ABS_X, 128);
        axis(ABS_HAT0X, 1);
        usleep(300000);
        axis(ABS_HAT0X, 0);
        tap(BTN_Y);
        sleep(1);
        tap(BTN_A);
        sleep(1);
        tap(BTN_B);
        sleep(1);
        tap(BTN_X);
        sleep(1);
        tap(BTN_START);
    }
    sleep(1);
    if (ioctl(device, UI_DEV_DESTROY) < 0) fail("UI_DEV_DESTROY");
    close(device);
    return 0;
}

#include "host.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define CONFIG_FILE_SIZE 60
#define KEY_COUNT 57
#define KEY_FILE_SIZE (KEY_COUNT * 4)

enum {
    CFG_GET_VOLUME = 1, CFG_SET_VOLUME,
    CFG_GET_FULLSCREEN, CFG_SET_FULLSCREEN,
    CFG_GET_FPS, CFG_SET_FPS,
    CFG_GET_BINDING, CFG_SET_BINDING,
    CFG_DEFAULTS, CFG_COMMIT, CFG_CANCEL,
    CFG_TOUCH_LAYOUT, CFG_GET_RUSSIAN, CFG_TOUCH_LABELS = 16,
    CFG_GET_UNLOCK_STAGES = 18, CFG_SET_UNLOCK_STAGES,
    CFG_GET_AA, CFG_SET_AA,
};

static int touch_labels = 1;
static int unlock_stages, saved_unlock_stages, aa_mode, saved_aa_mode;
static int qa_flags, qa_configured;
int void_grief_lan_active(void);

int void_grief_qa_flags(void)
{
    return (__atomic_load_n(&qa_flags, __ATOMIC_ACQUIRE) & 6) |
        (__atomic_load_n(&unlock_stages, __ATOMIC_ACQUIRE) ? 1 : 0);
}

void void_grief_qa_configure(int flags)
{
    if (flags && void_grief_lan_active()) return;
    __atomic_store_n(&qa_flags, flags & 7, __ATOMIC_RELEASE);
    __atomic_store_n(&unlock_stages, flags & 1, __ATOMIC_RELEASE);
    saved_unlock_stages = flags & 1;
    qa_configured = 1;
}
int void_grief_config_touch_labels(void) { return touch_labels; }
void void_grief_config_touch_labels_set(int value) { touch_labels = value != 0; }
int void_grief_config_aa(void) { return aa_mode; }

static uint8_t config_data[4096], saved_config_data[4096];
static size_t config_size = CONFIG_FILE_SIZE, saved_config_size = CONFIG_FILE_SIZE;
static int32_t keys[KEY_COUNT], saved_keys[KEY_COUNT];
static float fps_choice, fps_rates[32];
static int fps_rate_count;
static int loaded;

static const int32_t default_keys[KEY_COUNT] = {
    200,208,203,205,44,45,46,30,16,
    23,37,36,38,49,50,51,35,22,
    72,80,75,77,82,83,156,78,74,
    0,0,0,
    524,526,527,525,768,769,770,771,772,
    773,774,775,776,777,778,779,780,781,
    782,783,784,785,786,787,788,789,790,
};

static uint32_t config_word(unsigned index) { uint32_t value; memcpy(&value, config_data + index * 4, 4); return value; }
static void set_config_word(unsigned index, uint32_t value) { memcpy(config_data + index * 4, &value, 4); }

static size_t read_data(const char *name, uint8_t *out, size_t capacity)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", void_data_dir(), name);
    FILE *file = fopen(path, "rb");
    if (!file) {
        void_log("[config] cannot read %s: %s", path, strerror(errno));
        return 0;
    }
    size_t size = fread(out, 1, capacity, file);
    int extra = fgetc(file);
    int failed = ferror(file) || extra != EOF;
    fclose(file);
    return failed ? 0 : size;
}

static int write_data(const char *name, const void *data, size_t size)
{
    char path[1200], temporary[1200];
    snprintf(path, sizeof path, "%s/%s", void_data_dir(), name);
    snprintf(temporary, sizeof temporary, "%s/%s.part", void_data_dir(), name);
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0664);
    if (fd < 0) return 0;
    size_t done = 0;
    while (done < size) {
        ssize_t result = write(fd, (const uint8_t *)data + done, size - done);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) { close(fd); unlink(temporary); return 0; }
        done += (size_t)result;
    }
    int ok = !fsync(fd) && !close(fd) && !rename(temporary, path);
    if (!ok) { close(fd); unlink(temporary); }
    return ok;
}

static void config_defaults(void)
{
    memset(config_data, 0, CONFIG_FILE_SIZE);
    config_size = CONFIG_FILE_SIZE;
    set_config_word(0, 100);
    set_config_word(1, 100);
    set_config_word(2, 100);
    set_config_word(5, 50);
    set_config_word(6, 1);
    unlock_stages = 0;
    aa_mode = 0;
    memcpy(keys, default_keys, sizeof keys);
}

void void_dsound_master_volume(int value);
int void_grief_config_fullscreen(void) { return 1; }
int32_t void_grief_config_binding(unsigned index) { return index < KEY_COUNT ? keys[index] : -1; }
void void_android_config_event(int event, float value) __attribute__((weak));
void void_android_config_event(int event, float value) { (void)event; (void)value; }

static void apply_settings(void)
{
    void_dsound_master_volume((int)config_word(0));
    void_android_config_event(CFG_SET_FPS, fps_choice);
    void_android_config_event(27, (float)void_grief_qa_flags());
    if (void_vm_readable(0x6A8470u, 4)) MEM32(0x6A8470u) = config_word(0);
    if (void_vm_readable(0x6A8488u, 4)) MEM32(0x6A8488u) = 1;
}

static void load_settings(void)
{
    if (loaded) return;
    loaded = 1;
    size_t size = read_data("config.dat", config_data, sizeof config_data);
    if (size < CONFIG_FILE_SIZE) config_defaults();
    else config_size = size;
    set_config_word(6, 1);
    size_t key_size = read_data("keyconfig.dat", (uint8_t *)keys, sizeof keys);
    if (key_size == KEY_FILE_SIZE - 3 * sizeof(int32_t)) {
        memmove(keys + 30, keys + 27, 27 * sizeof(int32_t));
        memset(keys + 27, 0, 3 * sizeof(int32_t));
        key_size = KEY_FILE_SIZE;
        if (!write_data("keyconfig.dat", keys, sizeof keys))
            void_log("[config] cannot migrate keyconfig.dat: %s", strerror(errno));
    }
    if (key_size != KEY_FILE_SIZE) memcpy(keys, default_keys, sizeof keys);
    if (size < CONFIG_FILE_SIZE && !write_data("config.dat", config_data, config_size))
        void_log("[config] cannot create default config.dat: %s", strerror(errno));
    if (key_size != KEY_FILE_SIZE && !write_data("keyconfig.dat", keys, sizeof keys))
        void_log("[config] cannot create default keyconfig.dat: %s", strerror(errno));
    uint8_t enabled = 0;
    unlock_stages = qa_configured ? (__atomic_load_n(&qa_flags, __ATOMIC_ACQUIRE) & 1) :
        (read_data("unlock-stages", &enabled, 1) == 1 && enabled == 1);
    aa_mode = read_data("aa-mode", &enabled, 1) == 1 && enabled <= 3 ? enabled : 0;
    memcpy(saved_config_data, config_data, config_size);
    saved_config_size = config_size;
    memcpy(saved_keys, keys, sizeof keys);
    saved_unlock_stages = unlock_stages;
    saved_aa_mode = aa_mode;
    apply_settings();
}

void void_grief_config_init(void) { load_settings(); }
void void_grief_config_apply_guest(void) { load_settings(); apply_settings(); }

static int fps_supported(float value)
{
    if (value == 0.0f) return 1;
    for (int i = 0; i < fps_rate_count; ++i) if (fps_rates[i] == value) return 1;
    return 0;
}

void void_grief_config_fps_choice(float choice)
{
    if (choice != 0.0f && (!(choice > 0.0f) || choice > 1000.0f || !fps_supported(choice)))
        choice = fps_rate_count ? fps_rates[fps_rate_count - 1] : 0.0f;
    fps_choice = choice;
}

float void_grief_config_fps(void)
{
    load_settings();
    return fps_choice;
}

void void_grief_config_fps_rates(const float *rates, int count)
{
    fps_rate_count = 0;
    for (int i = 0; rates && i < count && fps_rate_count < 32; ++i) {
        float value = (float)(int)(rates[i] + 0.5f);
        if (!(value > 0.0f) || value > 1000.0f || fps_supported(value)) continue;
        int at = fps_rate_count;
        while (at > 0 && fps_rates[at - 1] > value) {
            fps_rates[at] = fps_rates[at - 1];
            --at;
        }
        fps_rates[at] = value;
        ++fps_rate_count;
    }
    if (!fps_supported(fps_choice)) fps_choice = fps_rate_count ? fps_rates[fps_rate_count - 1] : 0.0f;
}

float void_grief_config_fps_cycle(int delta)
{
    load_settings();
    if (!fps_rate_count) return fps_choice;
    int current = fps_rate_count;
    for (int i = 0; i < fps_rate_count; ++i) if (fps_rates[i] == fps_choice) current = i;
    current = (current + delta + fps_rate_count + 1) % (fps_rate_count + 1);
    void_grief_config_fps_choice(current == fps_rate_count ? 0.0f : fps_rates[current]);
    void_android_config_event(CFG_SET_FPS, fps_choice);
    return fps_choice;
}

int void_grief_config_command(int command, int32_t argument)
{
    load_settings();
    switch (command) {
    case CFG_GET_VOLUME: return (int)config_word(0);
    case CFG_SET_VOLUME:
        if (argument < 0) argument = 0;
        if (argument > 100) argument = 100;
        set_config_word(0, (uint32_t)argument);
        apply_settings();
        return argument;
    case CFG_GET_FULLSCREEN: return 1;
    case CFG_SET_FULLSCREEN:
        set_config_word(6, 1);
        apply_settings();
        return 1;
    case CFG_GET_FPS: return (int)fps_choice;
    case CFG_SET_FPS:
        void_grief_config_fps_choice((float)argument);
        void_android_config_event(CFG_SET_FPS, fps_choice);
        return (int)fps_choice;
    case CFG_GET_BINDING:
        return argument >= 0 && argument < KEY_COUNT ? keys[argument] : -1;
    case CFG_SET_BINDING: {
        unsigned index = (uint32_t)argument >> 16;
        if (index >= KEY_COUNT) return -1;
        int32_t code = (int32_t)(uint32_t)(argument & 0xFFFF);
        if (code == 0xFFFF) code = -1;
        keys[index] = code;
        return code;
    }
    case CFG_DEFAULTS: {
        uint8_t unknown[sizeof config_data];
        size_t size = config_size;
        memcpy(unknown, config_data, size);
        config_defaults();
        if (size > CONFIG_FILE_SIZE) {
            memcpy(config_data + CONFIG_FILE_SIZE, unknown + CONFIG_FILE_SIZE, size - CONFIG_FILE_SIZE);
            config_size = size;
        }
        apply_settings();
        return 1;
    }
    case CFG_COMMIT: {
        uint8_t enabled = unlock_stages != 0, aa = (uint8_t)aa_mode;
        if (write_data("config.dat", config_data, config_size) &&
            write_data("keyconfig.dat", keys, sizeof keys) &&
            write_data("unlock-stages", &enabled, 1) &&
            write_data("aa-mode", &aa, 1)) {
            memcpy(saved_config_data, config_data, config_size);
            saved_config_size = config_size;
            memcpy(saved_keys, keys, sizeof keys);
            saved_unlock_stages = unlock_stages;
            saved_aa_mode = aa_mode;
            void_android_config_event(27, (float)void_grief_qa_flags());
            return 1;
        }
        void_log("[config] cannot save settings: %s", strerror(errno));
        write_data("config.dat", saved_config_data, saved_config_size);
        write_data("keyconfig.dat", saved_keys, sizeof saved_keys);
        enabled = saved_unlock_stages != 0;
        aa = (uint8_t)saved_aa_mode;
        write_data("unlock-stages", &enabled, 1);
        write_data("aa-mode", &aa, 1);
        memcpy(config_data, saved_config_data, saved_config_size);
        config_size = saved_config_size;
        memcpy(keys, saved_keys, sizeof keys);
        unlock_stages = saved_unlock_stages;
        aa_mode = saved_aa_mode;
        apply_settings();
        return 0;
    }
    case CFG_CANCEL:
        memcpy(config_data, saved_config_data, saved_config_size);
        config_size = saved_config_size;
        memcpy(keys, saved_keys, sizeof keys);
        unlock_stages = saved_unlock_stages;
        aa_mode = saved_aa_mode;
        apply_settings();
        return 1;
    case CFG_TOUCH_LAYOUT:
        void_android_config_event(CFG_TOUCH_LAYOUT, 0);
        return 1;
    case CFG_TOUCH_LABELS:
        touch_labels = !touch_labels;
        void_android_config_event(CFG_TOUCH_LABELS, touch_labels);
        return touch_labels;
    case CFG_GET_RUSSIAN: return void_ansi_codepage() == 1251;
    case CFG_GET_UNLOCK_STAGES: return unlock_stages;
    case CFG_SET_UNLOCK_STAGES: return unlock_stages = argument != 0;
    case CFG_GET_AA: return aa_mode;
    case CFG_SET_AA: return aa_mode = argument >= 0 && argument <= 3 ? argument : 0;
    default: return -1;
    }
}

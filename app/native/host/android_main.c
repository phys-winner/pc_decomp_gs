#ifdef VOID_ANDROID_APP
#define _GNU_SOURCE
#include "host.h"
#include "input.h"
#include "grief_menu.h"
#include "android_assets.h"
#include "aaudio_compat.h"
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

int void_game_main(const char *game, const char *data, const char *images, int selftest);
void void_process_exit(uint32_t code, int run_detach);
void void_port_set_fps_limit(float value);

static ANativeWindow *window;
static pthread_mutex_t window_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t window_cond = PTHREAD_COND_INITIALIZER;
static int display_width, display_height;
static pthread_t game_thread;
static int game_started;
static char game_dir[512], data_dir[512], images_dir[512];
static JavaVM *jvm;
static jclass activity_class;
static jclass menu_class;
static jmethodID menu_method;
static uint32_t window_serial, window_acked;
static uint64_t audio_pause_state;
static int load_ready;

static JNIEnv *attach_env(int *attached)
{
    JNIEnv *env = NULL;
    *attached = jvm && (*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED;
    if (*attached && (*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK) return NULL;
    return env;
}

void void_android_process_exit(uint32_t code)
{
    if (!jvm || !activity_class) return;
    int attached;
    JNIEnv *env = attach_env(&attached);
    if (!env) return;
    jmethodID callback = (*env)->GetStaticMethodID(env, activity_class, "onGameExit", "(I)V");
    if (callback) (*env)->CallStaticVoidMethod(env, activity_class, callback, (jint)code);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (attached) (*jvm)->DetachCurrentThread(jvm);
}

static void remember_display_size_locked(int w, int h)
{
    if (w > 0 && h > 0 && w >= h) { display_width = w; display_height = h; }
}

int void_android_display_size(int *w, int *h)
{
    pthread_mutex_lock(&window_lock);
    if (window) remember_display_size_locked(ANativeWindow_getWidth(window), ANativeWindow_getHeight(window));
    *w = display_width;
    *h = display_height;
    int valid = *w > 0 && *h > 0;
    pthread_mutex_unlock(&window_lock);
    return valid;
}

int void_android_render_ready(void)
{
    pthread_mutex_lock(&window_lock);
    int ready = window != NULL && !(__atomic_load_n(&audio_pause_state, __ATOMIC_ACQUIRE) & 1u);
    pthread_mutex_unlock(&window_lock);
    return ready;
}

void *void_android_window(void)
{
    pthread_mutex_lock(&window_lock);
    while (!window) pthread_cond_wait(&window_cond, &window_lock);
    ANativeWindow *w = window;
    pthread_mutex_unlock(&window_lock);
    return w;
}

void *void_android_window_current(uint32_t *serial, int wait)
{
    pthread_mutex_lock(&window_lock);
    while (wait && !window) pthread_cond_wait(&window_cond, &window_lock);
    ANativeWindow *w = window;
    *serial = window_serial;
    pthread_mutex_unlock(&window_lock);
    return w;
}

void void_android_window_ack(uint32_t serial)
{
    pthread_mutex_lock(&window_lock);
    window_acked = serial;
    pthread_cond_broadcast(&window_cond);
    pthread_mutex_unlock(&window_lock);
}

uint64_t void_audio_pause_state(void)
{
    return __atomic_load_n(&audio_pause_state, __ATOMIC_ACQUIRE);
}

static void *game_main(void *arg)
{
    (void)arg;
    int code = void_game_main(game_dir, data_dir, images_dir, 0);
    void_log("[android] game thread finished with %d", code);
    void_process_exit((uint32_t)code, 0);
    return NULL;
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;
    jvm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativeStart(
    JNIEnv *env, jobject self, jstring game, jstring data, jstring images,
    jstring edition, jint screen_w, jint screen_h, jboolean skip_logo, jboolean lan,
    jboolean lan_host, jstring lan_address, jint lan_port, jint lan_players, jboolean verbose)
{
    if (game_started) return;
    const char *g = NULL, *d = NULL, *i = NULL, *e = NULL, *a = NULL;
    if (!(g = (*env)->GetStringUTFChars(env, game, NULL))) goto strings_failed;
    if (!(d = (*env)->GetStringUTFChars(env, data, NULL))) goto strings_failed;
    if (!(i = (*env)->GetStringUTFChars(env, images, NULL))) goto strings_failed;
    if (edition && !(e = (*env)->GetStringUTFChars(env, edition, NULL))) goto strings_failed;
    if (lan_address && !(a = (*env)->GetStringUTFChars(env, lan_address, NULL))) goto strings_failed;
    snprintf(game_dir, sizeof game_dir, "%s", g);
    snprintf(data_dir, sizeof data_dir, "%s", d);
    snprintf(images_dir, sizeof images_dir, "%s", i);
    void_module_select_image("griefsyndrome.exe");
    void_ansi_set_codepage(e && !strcmp(e, "ru") ? 1251u : 932u);
    void_grief_set_skip_logo(skip_logo != JNI_FALSE);
    void void_grief_lan_request(int enabled);
    void_grief_lan_request(lan != JNI_FALSE);
    void void_grief_lan_configure(int host, const char *address, int port, int players);
    void_grief_lan_configure(lan_host != JNI_FALSE, a, lan_port, lan_players);
    (*env)->ReleaseStringUTFChars(env, game, g);
    (*env)->ReleaseStringUTFChars(env, data, d);
    (*env)->ReleaseStringUTFChars(env, images, i);
    if (e) (*env)->ReleaseStringUTFChars(env, edition, e);
    if (a) (*env)->ReleaseStringUTFChars(env, lan_address, a);
    jclass cls = (*env)->GetObjectClass(env, self);
    activity_class = (*env)->NewGlobalRef(env, cls);
    void void_gdi_init(JNIEnv *env);
    void_gdi_init(env);
    jclass bridge = (*env)->FindClass(env, "org/griefsyndrome/port/GdiBridge");
    if (bridge) {
        menu_class = (*env)->NewGlobalRef(env, bridge);
        menu_method = (*env)->GetStaticMethodID(env, bridge, "menu", "([I)[I");
        (*env)->DeleteLocalRef(env, bridge);
    }
    jmethodID get_assets = (*env)->GetMethodID(env, cls, "getAssets", "()Landroid/content/res/AssetManager;");
    jobject assets = get_assets ? (*env)->CallObjectMethod(env, self, get_assets) : NULL;
    (*env)->DeleteLocalRef(env, cls);
    void_android_assets_init(env, assets);
    if (assets) (*env)->DeleteLocalRef(env, assets);
    (void)verbose;
    void_verbose = 0;
    pthread_mutex_lock(&window_lock);
    if (!display_width && screen_w > 0 && screen_h > 0 && screen_w >= screen_h)
        remember_display_size_locked(screen_w, screen_h);
    pthread_mutex_unlock(&window_lock);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64u << 20);
    game_started = pthread_create(&game_thread, &attr, game_main, NULL) == 0;
    pthread_attr_destroy(&attr);
    return;

strings_failed:
    if (a) (*env)->ReleaseStringUTFChars(env, lan_address, a);
    if (e) (*env)->ReleaseStringUTFChars(env, edition, e);
    if (i) (*env)->ReleaseStringUTFChars(env, images, i);
    if (d) (*env)->ReleaseStringUTFChars(env, data, d);
    if (g) (*env)->ReleaseStringUTFChars(env, game, g);
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativeSurface(
    JNIEnv *env, jobject self, jobject surface)
{
    (void)self;
    pthread_mutex_lock(&window_lock);
    ANativeWindow *old = window;
    window = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (window) remember_display_size_locked(ANativeWindow_getWidth(window), ANativeWindow_getHeight(window));
    uint32_t serial = ++window_serial;
    pthread_cond_broadcast(&window_cond);
    if (!surface && old) {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += 1;
        while (window_acked != serial && pthread_cond_timedwait(&window_cond, &window_lock, &until) == 0) {}
    }
    pthread_mutex_unlock(&window_lock);
    if (old) ANativeWindow_release(old);
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativePause(
    JNIEnv *env, jobject self, jboolean paused)
{
    (void)env;
    (void)self;
    void_clock_set_paused(paused != JNI_FALSE);
    uint64_t state = void_audio_pause_state(), next;
    do {
        if ((state & 1u) == (paused ? 1u : 0u)) break;
        next = ((state & ~UINT64_C(1)) + 2u) | (paused ? 1u : 0u);
    } while (!__atomic_compare_exchange_n(&audio_pause_state, &state, next, 0,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativeKey(
    JNIEnv *env, jobject self, jint vk, jboolean down)
{
    (void)env;
    (void)self;
    if (vk > 0 && vk < 256) void_input_key((uint32_t)vk, down != JNI_FALSE);
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativeKeyFromSource(
    JNIEnv *env, jclass cls, jint vk, jboolean down, jint source)
{
    (void)env; (void)cls;
    if (vk > 0 && vk < 256) void_input_key_ex((uint32_t)vk, down != JNI_FALSE, source);
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativeReleaseAll(
    JNIEnv *env, jobject self)
{
    (void)env;
    (void)self;
    void_grief_menu_touch(3, 0, 0);
    for (uint32_t vk = 1; vk < 256; ++vk)
        if (void_input_key_state(vk) & 0x8000) void_input_key(vk, 0);
    for (int button = 0; button < 3; ++button) void_input_mouse_button(button, 0);
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativePointer(
    JNIEnv *env, jobject self, jint x, jint y)
{
    (void)env;
    (void)self;
    uint32_t old_x, old_y;
    void_input_cursor(&old_x, &old_y);
    void_input_mouse_warp(x, y);
    (void)old_x;
    (void)old_y;
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativeMouseButton(
    JNIEnv *env, jobject self, jint button, jboolean down)
{
    (void)env;
    (void)self;
    if (button >= 0 && button < 8) void_input_mouse_button(button, down != JNI_FALSE);
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativeFpsLimit(
    JNIEnv *env, jobject self, jfloat value)
{
    (void)env;
    (void)self;
    void_port_set_fps_limit(value);
}

void void_android_load_ready(void)
{
    __atomic_store_n(&load_ready, 1, __ATOMIC_RELEASE);
}

void void_android_frame_presented(void)
{
    void_android_load_ready();
}

JNIEXPORT jint JNICALL Java_org_griefsyndrome_port_GameActivity_nativeLoadWork(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    return (jint)(void_perf_calls(VOID_PERF_FILE_OPEN) + void_perf_calls(VOID_PERF_FILE_MAP));
}

JNIEXPORT jboolean JNICALL Java_org_griefsyndrome_port_GameActivity_nativeLoadReady(JNIEnv *env, jclass type)
{
    (void)env;
    (void)type;
    return __atomic_load_n(&load_ready, __ATOMIC_ACQUIRE) ? JNI_TRUE : JNI_FALSE;
}

static void *stream;
static pthread_mutex_t audio_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t stream_pause_state;
static void audio_close_stream(void)
{
    aaudio_compat_close(stream);
    stream = NULL;
}

uint32_t void_audio_output(const int16_t *frames, uint32_t count)
{
    pthread_mutex_lock(&audio_lock);
    uint64_t pause = void_audio_pause_state();
    if (stream && pause != stream_pause_state) audio_close_stream();
    if (pause & 1u) {
        pthread_mutex_unlock(&audio_lock);
        return 0;
    }
    if (!stream) {
        stream = aaudio_compat_open(jvm);
        if (!stream) {
            pthread_mutex_unlock(&audio_lock);
            return 0;
        }
        stream_pause_state = pause;
    }
    uint32_t written = 0;
    while (written < count) {
        int32_t result = aaudio_compat_write(stream, frames + written * 2, (int32_t)(count - written));
        if (result < 0) {
            void_log("[audio] write failed %d", (int)result);
            audio_close_stream();
            break;
        }
        written += (uint32_t)result;
        if (!result) usleep(1000);
    }
    uint32_t wanted = aaudio_compat_available(stream);
    pthread_mutex_unlock(&audio_lock);
    return wanted;
}

int void_android_menu_render(const int state[64], uint32_t *pixels, int count)
{
    if (!jvm || !pixels) return 0;
    int attached;
    JNIEnv *env = attach_env(&attached);
    if (!env) return 0;
    jintArray input = (*env)->NewIntArray(env, 64);
    if (input) (*env)->SetIntArrayRegion(env, input, 0, 64, (const jint *)state);
    jintArray output = menu_class && menu_method && input ?
        (*env)->CallStaticObjectMethod(env, menu_class, menu_method, input) : NULL;
    int ok = output && (*env)->GetArrayLength(env, output) == count;
    if (ok) {
        (*env)->GetIntArrayRegion(env, output, 0, count, (jint *)pixels);
        for (int i = 0; i < count; ++i) {
            uint32_t color = pixels[i];
            pixels[i] = (color & 0xFF00FF00u) | ((color >> 16) & 0xFFu) | ((color & 0xFFu) << 16);
        }
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); ok = 0; }
    if (output) (*env)->DeleteLocalRef(env, output);
    if (input) (*env)->DeleteLocalRef(env, input);
    if (attached) (*jvm)->DetachCurrentThread(jvm);
    return ok;
}

void void_android_config_event(int event, float value)
{
    if (!jvm || !activity_class) return;
    int attached;
    JNIEnv *env = attach_env(&attached);
    if (!env) return;
    jmethodID callback = (*env)->GetStaticMethodID(env, activity_class, "onPortSetting", "(IF)V");
    if (callback) (*env)->CallStaticVoidMethod(env, activity_class, callback, event, value);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (attached) (*jvm)->DetachCurrentThread(jvm);
}

JNIEXPORT jboolean JNICALL Java_org_griefsyndrome_port_GameActivity_nativeMenuTouch(
    JNIEnv *env, jclass cls, jint action, jfloat x, jfloat y)
{
    (void)env; (void)cls;
    int void_grief_menu_touch(int action, float x, float y);
    return void_grief_menu_touch(action, x, y) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_org_griefsyndrome_port_GameActivity_nativeQaFlags(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    int void_grief_qa_flags(void);
    return void_grief_qa_flags();
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativeQaSet(JNIEnv *env, jclass cls, jint flags)
{
    (void)env; (void)cls;
    void void_grief_qa_configure(int flags);
    void_grief_qa_configure(flags);
}

JNIEXPORT jboolean JNICALL Java_org_griefsyndrome_port_GameActivity_nativeBack(
    JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return void_grief_menu_back() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativeMenuConfirm(
    JNIEnv *env, jclass cls, jint kind, jboolean accepted)
{
    (void)env; (void)cls;
    void_grief_menu_confirm(kind, accepted != JNI_FALSE);
}

JNIEXPORT jboolean JNICALL Java_org_griefsyndrome_port_GameActivity_nativeBindingEvent(
    JNIEnv *env, jclass cls, jint source, jint code)
{
    (void)env; (void)cls;
    int void_grief_menu_capture(int source, int code);
    return void_grief_menu_capture(source, code) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativeFpsChoice(
    JNIEnv *env, jclass cls, jfloat value)
{
    (void)env; (void)cls;
    void void_grief_config_fps_choice(float choice);
    void_grief_config_fps_choice(value);
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativeFpsRates(
    JNIEnv *env, jclass cls, jfloatArray values)
{
    (void)cls;
    jsize count = (*env)->GetArrayLength(env, values);
    jfloat *rates = (*env)->GetFloatArrayElements(env, values, NULL);
    if (rates) {
        void void_grief_config_fps_rates(const float *rates, int count);
        void_grief_config_fps_rates(rates, count);
        (*env)->ReleaseFloatArrayElements(env, values, rates, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL Java_org_griefsyndrome_port_GameActivity_nativeControlLabels(
    JNIEnv *env, jclass cls, jboolean value)
{
    (void)env; (void)cls;
    void void_grief_config_touch_labels_set(int value);
    void_grief_config_touch_labels_set(value != JNI_FALSE);
}

JNIEXPORT jint JNICALL Java_org_griefsyndrome_port_GameActivity_nativeTouchBinding(
    JNIEnv *env, jclass cls, jint action)
{
    (void)env; (void)cls;
    return void_grief_touch_binding(action);
}

JNIEXPORT jint JNICALL Java_org_griefsyndrome_port_GameActivity_nativePadKey(
    JNIEnv *env, jclass cls, jint code, jint player)
{
    (void)env; (void)cls;
    int void_grief_menu_pad_key(int code, int player);
    return void_grief_menu_pad_key(code, player);
}

JNIEXPORT jint JNICALL Java_org_griefsyndrome_port_GameActivity_nativePadDirection(
    JNIEnv *env, jclass cls, jint direction, jint player)
{
    (void)env; (void)cls;
    int void_grief_menu_pad_direction(int direction, int player);
    return void_grief_menu_pad_direction(direction, player);
}

JNIEXPORT jint JNICALL Java_org_griefsyndrome_port_GameActivity_nativeAndroidKey(
    JNIEnv *env, jclass cls, jint code)
{
    (void)env; (void)cls;
    int void_grief_menu_android_vk(int code);
    return void_grief_menu_android_vk(code);
}

JNIEXPORT jint JNICALL Java_org_griefsyndrome_port_GameActivity_nativeLanControl(
    JNIEnv *env, jclass cls, jint action)
{
    (void)env; (void)cls;
    int void_lan_control(int action);
    return void_lan_control(action);
}

JNIEXPORT jstring JNICALL Java_org_griefsyndrome_port_GameActivity_nativeLanStatus(
    JNIEnv *env, jclass cls)
{
    (void)cls;
    const char *void_lan_status(void);
    const char *text = void_lan_status();
    return (*env)->NewStringUTF(env, text);
}

JNIEXPORT jboolean JNICALL Java_org_griefsyndrome_port_GameActivity_nativeLanCanStart(
    JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    int void_lan_can_start(void);
    return void_lan_can_start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_org_griefsyndrome_port_GameActivity_nativeLanCanConnect(
    JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    int void_lan_can_connect(void);
    return void_lan_can_connect() ? JNI_TRUE : JNI_FALSE;
}
#endif

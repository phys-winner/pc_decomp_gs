/* Check production JNI cleanup on allocation/method/render failures without a JVM. */
#define VOID_ANDROID_APP
#include "../app/native/host/aaudio_compat.c"
#include "../app/native/host/gdi_game.c"
#include <assert.h>

#if defined(__arm__)
static uint8_t guest_memory[16];
uint8_t *g_guest_mem = guest_memory;
#endif
static int local_refs, global_refs, fail, exception, detached, attaches, detaches, releases;
static uintptr_t next_ref = 10;
static const struct JNINativeInterface native_functions;
static JNIEnv test_env = &native_functions;
static jobject local_ref(void) { ++local_refs; return (jobject)++next_ref; }
static jclass object_class(JNIEnv *env, jobject object)
{ (void)env; (void)object; if (fail == 3) { exception = 1; return NULL; } return (jclass)local_ref(); }
static jmethodID method(JNIEnv *env, jclass cls, const char *name, const char *signature)
{
    (void)env; (void)signature; assert(cls);
    if (fail == 1) { exception = 1; return NULL; }
    return (jmethodID)(uintptr_t)(!strcmp(name, "stop") ? 2 : !strcmp(name, "release") ? 3 : 1);
}
static jbyteArray byte_array(JNIEnv *env, jsize count)
{ (void)env; assert(count > 0); if (fail == 2) { exception = 1; return NULL; } return (jbyteArray)local_ref(); }
static void array_region(JNIEnv *env, jbyteArray array, jsize first, jsize count, const jbyte *bytes)
{ (void)env; (void)first; (void)count; (void)bytes; assert(array); }
static jint write_frames(JNIEnv *env, jobject object, jmethodID id, ...)
{ (void)env; (void)object; assert(id); return 1920; }
static void delete_local(JNIEnv *env, jobject object)
{ (void)env; assert(object && local_refs > 0); --local_refs; }
static void delete_global(JNIEnv *env, jobject object)
{ (void)env; assert(object && global_refs > 0); --global_refs; }
static jboolean has_exception(JNIEnv *env) { (void)env; return exception ? JNI_TRUE : JNI_FALSE; }
static void clear_exception(JNIEnv *env) { (void)env; exception = 0; }
static jstring string_utf(JNIEnv *env, const char *text)
{ (void)env; assert(text); return (jstring)local_ref(); }
static jobject render_failed(JNIEnv *env, jclass cls, jmethodID id, ...)
{ (void)env; assert(cls && id); exception = 1; return NULL; }
static void track_method(JNIEnv *env, jobject object, jmethodID id, ...)
{
    (void)env; (void)object;
    if ((uintptr_t)id == 2) exception = 1; /* A failed stop must still allow release. */
    if ((uintptr_t)id == 3) { assert(!exception); ++releases; }
}
static const struct JNINativeInterface native_functions = {
    .GetObjectClass = object_class, .GetMethodID = method,
    .NewByteArray = byte_array, .SetByteArrayRegion = array_region,
    .CallIntMethod = write_frames, .DeleteLocalRef = delete_local,
    .DeleteGlobalRef = delete_global, .ExceptionCheck = has_exception,
    .ExceptionClear = clear_exception, .NewStringUTF = string_utf,
    .CallStaticObjectMethod = render_failed, .CallVoidMethod = track_method,
};
static jint get_env(JavaVM *vm, void **env, jint version)
{ (void)vm; assert(version == JNI_VERSION_1_6); *env = detached ? NULL : &test_env; return detached ? JNI_EDETACHED : JNI_OK; }
static jint attach(JavaVM *vm, JNIEnv **env, void *args)
{ (void)vm; (void)args; detached = 0; ++attaches; *env = &test_env; return JNI_OK; }
static jint detach(JavaVM *vm) { (void)vm; ++detaches; detached = 1; return JNI_OK; }
static const struct JNIInvokeInterface vm_functions = {
    .GetEnv = get_env, .AttachCurrentThread = attach, .DetachCurrentThread = detach,
};
static JavaVM test_vm = &vm_functions;

void void_log(const char *fmt, ...) { (void)fmt; }
uint32_t void_ansi_codepage(void) { return 932; }

int main(void)
{
    static const int16_t silence[960];
    audio_stream stream = {.track = (jobject)1};
    gdi_vm = &test_vm;
    gdi_class = (jclass)1;
    gdi_render_method = (jmethodID)1;
    gdi_dc dc = {0}; gdi_dib dib = {.width = 1, .height = 1}; gdi_font font = {.height = 16};
    for (unsigned i = 0; i < 2048; ++i) {
        for (fail = 0; fail <= 3; ++fail) {
            assert(java_write(&stream, &test_env, silence, 480) == (fail ? -1 : 480));
            assert(!local_refs && !exception);
        }
        fail = 0;
        assert(!gdi_render(&dc, &dib, &font, 0, 0, (const uint8_t *)"x", 1));
        assert(!local_refs && !exception);
    }
    audio_stream *closing = calloc(1, sizeof *closing);
    closing->track = (jobject)1; closing->vm = &test_vm;
    global_refs = 1; detached = 1;
    aaudio_compat_close(closing);
    assert(!global_refs && !local_refs && !exception && attaches == 1 && detaches == 1 && releases == 1);
    puts("PASS: 2048 JNI allocation/method/render failure cycles release local refs; detached AudioTrack close releases global ref and device after failed stop");
    return 0;
}

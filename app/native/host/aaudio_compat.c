#ifdef VOID_ANDROID_APP
#include "aaudio_compat.h"
#include "host.h"
#include <aaudio/AAudio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    JavaVM *vm;
    void *stream;
    jobject track;
    uint64_t written;
    int32_t capacity;
} audio_stream;

static aaudio_result_t (*create_builder)(AAudioStreamBuilder **);
static void (*builder_format)(AAudioStreamBuilder *, int32_t);
static void (*builder_channels)(AAudioStreamBuilder *, int32_t);
static void (*builder_rate)(AAudioStreamBuilder *, int32_t);
static void (*builder_capacity)(AAudioStreamBuilder *, int32_t);
static void (*builder_performance)(AAudioStreamBuilder *, int32_t);
static aaudio_result_t (*builder_open)(AAudioStreamBuilder *, AAudioStream **);
static void (*builder_delete)(AAudioStreamBuilder *);
static aaudio_result_t (*stream_start)(AAudioStream *);
static aaudio_result_t (*stream_stop)(AAudioStream *);
static aaudio_result_t (*stream_close)(AAudioStream *);
static aaudio_result_t (*stream_write)(AAudioStream *, const void *, int32_t, int64_t);
static int32_t (*stream_capacity)(AAudioStream *);
static int32_t (*stream_burst)(AAudioStream *);
static int64_t (*stream_written)(AAudioStream *);
static int64_t (*stream_read)(AAudioStream *);
static int loaded;

static int load_native(void)
{
    if (loaded) return create_builder != NULL;
    loaded = 1;
    void *library = dlopen("libaaudio.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) return 0;
#define LOAD(field, name) do { *(void **)(&field) = dlsym(library, name); if (!field) return 0; } while (0)
    LOAD(create_builder, "AAudio_createStreamBuilder");
    LOAD(builder_format, "AAudioStreamBuilder_setFormat");
    LOAD(builder_channels, "AAudioStreamBuilder_setChannelCount");
    LOAD(builder_rate, "AAudioStreamBuilder_setSampleRate");
    LOAD(builder_capacity, "AAudioStreamBuilder_setBufferCapacityInFrames");
    LOAD(builder_performance, "AAudioStreamBuilder_setPerformanceMode");
    LOAD(builder_open, "AAudioStreamBuilder_openStream");
    LOAD(builder_delete, "AAudioStreamBuilder_delete");
    LOAD(stream_start, "AAudioStream_requestStart");
    LOAD(stream_stop, "AAudioStream_requestStop");
    LOAD(stream_close, "AAudioStream_close");
    LOAD(stream_write, "AAudioStream_write");
    LOAD(stream_capacity, "AAudioStream_getBufferSizeInFrames");
    LOAD(stream_burst, "AAudioStream_getFramesPerBurst");
    LOAD(stream_written, "AAudioStream_getFramesWritten");
    LOAD(stream_read, "AAudioStream_getFramesRead");
#undef LOAD
    return 1;
}

static void *open_native(void)
{
    audio_stream *stream = (audio_stream *)calloc(1, sizeof *stream);
    if (!stream) return NULL;
    AAudioStreamBuilder *builder = NULL;
    aaudio_result_t result = create_builder(&builder);
    if (result != AAUDIO_OK || !builder) {
        void_log("[audio] AAudio builder creation failed: %d", result);
        free(stream);
        return NULL;
    }
    builder_format(builder, AAUDIO_FORMAT_PCM_I16);
    builder_channels(builder, 2);
    builder_rate(builder, 48000);
    builder_capacity(builder, 1920);
    builder_performance(builder, AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStream *native = NULL;
    result = builder_open(builder, &native);
    if (result == AAUDIO_OK && native) result = stream_start(native);
    if (result != AAUDIO_OK) {
        void_log("[audio] AAudio open/start failed: %d; using AudioTrack", result);
        if (native) stream_close(native);
        builder_delete(builder);
        free(stream);
        return NULL;
    }
    builder_delete(builder);
    stream->stream = native;
    stream->capacity = stream_capacity(native);
    void_log("[audio] AAudio started: burst %d, buffer %d", stream_burst(native), stream->capacity);
    return stream;
}

static int32_t java_write(audio_stream *stream, JNIEnv *env, const int16_t *frames, int32_t count)
{
    if (count <= 0) return 0;
    jclass cls = (*env)->GetObjectClass(env, stream->track);
    jmethodID write = cls ? (*env)->GetMethodID(env, cls, "write", "([BII)I") : NULL;
    jbyteArray data = write ? (*env)->NewByteArray(env, count * 4) : NULL;
    int result = -1;
    if (data) {
        (*env)->SetByteArrayRegion(env, data, 0, count * 4, (const jbyte *)frames);
        result = (*env)->CallIntMethod(env, stream->track, write, data, 0, count * 4);
        (*env)->DeleteLocalRef(env, data);
    }
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return -1;
    }
    if (result >= 0) stream->written += (uint32_t)(result / 4);
    return result >= 0 ? result / 4 : result;
}

static void *open_java(JavaVM *vm)
{
    JNIEnv *env = NULL;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return NULL;
    jclass cls = (*env)->FindClass(env, "android/media/AudioTrack");
    jmethodID minimum = cls ? (*env)->GetStaticMethodID(env, cls, "getMinBufferSize", "(III)I") : NULL;
    jint capacity = minimum ? (*env)->CallStaticIntMethod(env, cls, minimum, 48000, 3, 2) : -1;
    if (capacity < 1920) capacity = 1920;
    jmethodID constructor = cls ? (*env)->GetMethodID(env, cls, "<init>", "(IIIIII)V") : NULL;
    jobject track = constructor ? (*env)->NewObject(env, cls, constructor, 3, 48000, 3, 2, capacity * 4, 1) : NULL;
    audio_stream *stream = (audio_stream *)calloc(1, sizeof *stream);
    if (track && stream) {
        stream->track = (*env)->NewGlobalRef(env, track);
        stream->capacity = capacity;
        jmethodID play = (*env)->GetMethodID(env, cls, "play", "()V");
        (*env)->CallVoidMethod(env, stream->track, play);
        if (!(*env)->ExceptionCheck(env)) {
            static const int16_t silence[480] = {0};
            java_write(stream, env, silence, 480);
            void_log("[audio] AudioTrack started: buffer %d", capacity);
        } else {
            (*env)->ExceptionClear(env);
        }
    }
    if (track && stream && stream->track) {
        (*env)->DeleteLocalRef(env, track);
        (*env)->DeleteLocalRef(env, cls);
        return stream;
    }
    if (stream) {
        if (stream->track) (*env)->DeleteGlobalRef(env, stream->track);
        free(stream);
    }
    if (track) (*env)->DeleteLocalRef(env, track);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    return NULL;
}

void *aaudio_compat_open(JavaVM *vm)
{
    audio_stream *stream = load_native() ? open_native() : NULL;
    if (!stream) stream = open_java(vm);
    if (stream) stream->vm = vm;
    return stream;
}

void aaudio_compat_close(void *opaque)
{
    audio_stream *stream = (audio_stream *)opaque;
    if (!stream) return;
    if (stream->stream) {
        stream_stop(stream->stream);
        stream_close(stream->stream);
    }
    if (stream->track) {
        JavaVM *vm = stream->vm;
        JNIEnv *env = NULL;
        int attached = 0;
        if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED)
            attached = (*vm)->AttachCurrentThread(vm, &env, NULL) == JNI_OK;
        if (env) {
            jclass cls = (*env)->GetObjectClass(env, stream->track);
            jmethodID stop = (*env)->GetMethodID(env, cls, "stop", "()V");
            jmethodID release = (*env)->GetMethodID(env, cls, "release", "()V");
            if (stop) (*env)->CallVoidMethod(env, stream->track, stop);
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
            if (release) (*env)->CallVoidMethod(env, stream->track, release);
            (*env)->DeleteLocalRef(env, cls);
            (*env)->DeleteGlobalRef(env, stream->track);
            if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        }
        if (attached) (*vm)->DetachCurrentThread(vm);
    }
    free(stream);
}

int32_t aaudio_compat_write(void *opaque, const int16_t *frames, int32_t count)
{
    audio_stream *stream = (audio_stream *)opaque;
    if (!stream) return -1;
    if (stream->stream) {
        int32_t result = stream_write(stream->stream, frames, count, 20000000);
        if (result > 0) stream->written += (uint32_t)result;
        return result;
    }
    JavaVM *vm = stream->vm;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return -1;
    return java_write(stream, env, frames, count);
}

int32_t aaudio_compat_capacity(void *opaque)
{
    audio_stream *stream = (audio_stream *)opaque;
    return stream ? stream->capacity : 0;
}

int32_t aaudio_compat_burst(void *opaque)
{
    audio_stream *stream = (audio_stream *)opaque;
    return stream && stream->stream ? stream_burst(stream->stream) : aaudio_compat_capacity(stream);
}

uint32_t aaudio_compat_available(void *opaque)
{
    audio_stream *stream = (audio_stream *)opaque;
    if (!stream || !stream->capacity) return 0;
    uint64_t read = 0;
    if (stream->stream) read = (uint64_t)stream_read(stream->stream);
    else {
        JavaVM *vm = stream->vm;
        JNIEnv *env = NULL;
        if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return 0;
        jclass cls = (*env)->GetObjectClass(env, stream->track);
        jmethodID position = (*env)->GetMethodID(env, cls, "getPlaybackHeadPosition", "()I");
        if (position) read = (uint32_t)(*env)->CallIntMethod(env, stream->track, position);
        (*env)->DeleteLocalRef(env, cls);
    }
    uint64_t queued = stream->written >= read ? stream->written - read : 0;
    return queued < (uint64_t)stream->capacity ? (uint32_t)(stream->capacity - queued) : 0;
}

#endif

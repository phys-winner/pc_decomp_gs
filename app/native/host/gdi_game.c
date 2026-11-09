#include "host.h"
#include <jni.h>

#define GDI_ERROR_VALUE 0xFFFFFFFFu
#define GDI_MAX_FONTS 64
#define GDI_MAX_DCS 16
#define GDI_MAX_DIBS 32

typedef struct { int used, height, weight, italic, charset; } gdi_font;
typedef struct { int used, font, dib, text_color, background, mode; } gdi_dc;
typedef struct { int used, width, height, top_down; uint32_t bits; } gdi_dib;

static gdi_font fonts[GDI_MAX_FONTS];
static gdi_dc dcs[GDI_MAX_DCS];
static gdi_dib dibs[GDI_MAX_DIBS];
static JavaVM *gdi_vm;
static jclass gdi_class;
static jmethodID gdi_metrics_method, gdi_measure_method, gdi_render_method, gdi_glyph_method;

void void_gdi_init(JNIEnv *env)
{
    (*env)->GetJavaVM(env, &gdi_vm);
    jclass local = (*env)->FindClass(env, "org/griefsyndrome/port/GdiBridge");
    if (!local) return;
    gdi_class = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    gdi_metrics_method = (*env)->GetStaticMethodID(env, gdi_class, "metrics", "(IIZ)[I");
    gdi_measure_method = (*env)->GetStaticMethodID(env, gdi_class, "measure", "([BLjava/lang/String;IIZ)[I");
    gdi_render_method = (*env)->GetStaticMethodID(env, gdi_class, "render", "([BLjava/lang/String;IIZIIIIIII)[I");
    gdi_glyph_method = (*env)->GetStaticMethodID(env, gdi_class, "glyph", "(IIIZ)[I");
}

static JNIEnv *gdi_env(int *attached)
{
    JNIEnv *env = NULL;
    *attached = 0;
    if (!gdi_vm) return NULL;
    if ((*gdi_vm)->GetEnv(gdi_vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if ((*gdi_vm)->AttachCurrentThread(gdi_vm, &env, NULL) != JNI_OK) return NULL;
        *attached = 1;
    }
    return env;
}

static void gdi_detach(int attached)
{
    if (attached) (*gdi_vm)->DetachCurrentThread(gdi_vm);
}

static jbyteArray gdi_bytes(JNIEnv *env, const uint8_t *data, int size)
{
    jbyteArray result = (*env)->NewByteArray(env, size);
    if (result) (*env)->SetByteArrayRegion(env, result, 0, size, (const jbyte *)data);
    return result;
}

static jstring gdi_charset(JNIEnv *env)
{
    return (*env)->NewStringUTF(env, void_ansi_codepage() == 932 ? "SJIS" : "windows-1251");
}

static int gdi_ints(JNIEnv *env, jintArray array, int *values, int count)
{
    if (!array) return 0;
    int length = (*env)->GetArrayLength(env, array);
    if (length > count) length = count;
    (*env)->GetIntArrayRegion(env, array, 0, length, values);
    (*env)->DeleteLocalRef(env, array);
    return length;
}

static int gdi_measure(const uint8_t *text, int count, gdi_font *font, int *size)
{
    int attached;
    JNIEnv *env = gdi_env(&attached);
    if (!env || !gdi_measure_method) { if (env) gdi_detach(attached); return 0; }
    jbyteArray bytes = gdi_bytes(env, text, count);
    jstring charset = gdi_charset(env);
    jintArray array = (jintArray)(*env)->CallStaticObjectMethod(env, gdi_class, gdi_measure_method,
        bytes, charset, font->height, font->weight, font->italic != 0);
    int values[4];
    int result = gdi_ints(env, array, values, 4);
    if (result == 4) { size[0] = values[0]; size[1] = values[1]; }
    if (bytes) (*env)->DeleteLocalRef(env, bytes);
    if (charset) (*env)->DeleteLocalRef(env, charset);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); result = 0; }
    gdi_detach(attached);
    return result == 4;
}

static uint32_t android_color(uint32_t color)
{
    return 0xFF000000u | ((color & 0xFFu) << 16) | (color & 0xFF00u) | ((color >> 16) & 0xFFu);
}

static int gdi_render(gdi_dc *dc, gdi_dib *dib, gdi_font *font,
    int x, int y, const uint8_t *text, int count)
{
    int attached;
    JNIEnv *env = gdi_env(&attached);
    if (!env || !gdi_render_method) { if (env) gdi_detach(attached); return 0; }
    jbyteArray bytes = gdi_bytes(env, text, count);
    jstring charset = gdi_charset(env);
    jintArray array = (jintArray)(*env)->CallStaticObjectMethod(env, gdi_class, gdi_render_method,
        bytes, charset, font->height, font->weight, font->italic,
        dib->width, dib->height, x, y, (jint)android_color(dc->text_color),
        (jint)android_color(dc->background), dc->mode);
    if (!array) {
        if (bytes) (*env)->DeleteLocalRef(env, bytes);
        if (charset) (*env)->DeleteLocalRef(env, charset);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        gdi_detach(attached);
        return 0;
    }
    int length = (*env)->GetArrayLength(env, array);
    if (length == dib->width * dib->height) {
        jint *pixels = (*env)->GetIntArrayElements(env, array, NULL);
        if (pixels) {
            for (int row = 0; row < dib->height; ++row) {
                int source = dib->top_down ? row : dib->height - 1 - row;
                memcpy(GPTR(dib->bits + 4 * dib->width * row), pixels + source * dib->width,
                    4 * dib->width);
            }
            (*env)->ReleaseIntArrayElements(env, array, pixels, JNI_ABORT);
        }
    }
    (*env)->DeleteLocalRef(env, array);
    if (bytes) (*env)->DeleteLocalRef(env, bytes);
    if (charset) (*env)->DeleteLocalRef(env, charset);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); gdi_detach(attached); return 0; }
    gdi_detach(attached);
    return 1;
}

static gdi_font *gdi_current_font(gdi_dc *dc)
{
    static gdi_font fallback = {1, 16, 400, 0, 0};
    return dc && dc->font >= 0 && dc->font < GDI_MAX_FONTS && fonts[dc->font].used ? &fonts[dc->font] : &fallback;
}

uint32_t gdi_create_font(int height, int weight, int italic, int charset)
{
    for (int i = 0; i < GDI_MAX_FONTS; ++i) if (!fonts[i].used) {
        fonts[i] = (gdi_font){1, height < 1 ? 1 : height, weight, italic, charset};
        return 0x00060001u + (uint32_t)i;
    }
    return 0;
}

uint32_t gdi_select_object(uint32_t handle, uint32_t object)
{
    int index = (int)(handle - 0x00040001u);
    if (index < 0 || index >= GDI_MAX_DCS || !dcs[index].used) return 0;
    gdi_dc *dc = &dcs[index];
    if (object >= 0x00060001u && object < 0x00060001u + GDI_MAX_FONTS && fonts[object - 0x00060001u].used)
        dc->font = (int)(object - 0x00060001u);
    else if (object >= 0x00070001u && object < 0x00070001u + GDI_MAX_DIBS && dibs[object - 0x00070001u].used)
        dc->dib = (int)(object - 0x00070001u);
    return object;
}

uint32_t gdi_delete_object(uint32_t object)
{
    if (object >= 0x00060001u && object < 0x00060001u + GDI_MAX_FONTS) {
        gdi_font *font = &fonts[object - 0x00060001u];
        font->used = 0;
        return 1;
    }
    if (object >= 0x00070001u && object < 0x00070001u + GDI_MAX_DIBS) {
        gdi_dib *dib = &dibs[object - 0x00070001u];
        if (dib->bits) void_gfree(dib->bits);
        dib->used = 0;
        return 1;
    }
    return 0;
}

uint32_t gdi_text_color(uint32_t handle, uint32_t color)
{
    int index = (int)(handle - 0x00040001u);
    if (index < 0 || index >= GDI_MAX_DCS || !dcs[index].used) return color;
    uint32_t old = dcs[index].text_color;
    dcs[index].text_color = color;
    return old;
}

uint32_t gdi_background_color(uint32_t handle, uint32_t color)
{
    int index = (int)(handle - 0x00040001u);
    if (index < 0 || index >= GDI_MAX_DCS || !dcs[index].used) return color;
    uint32_t old = dcs[index].background;
    dcs[index].background = color;
    return old;
}

int gdi_background_mode(uint32_t handle, int mode)
{
    int index = (int)(handle - 0x00040001u);
    if (index < 0 || index >= GDI_MAX_DCS || !dcs[index].used) return 0;
    int old = dcs[index].mode;
    dcs[index].mode = mode;
    return old;
}

WINAPI_EXPORT("gdi32.dll", CreateCompatibleDC, 1)
{
    for (int i = 0; i < GDI_MAX_DCS; ++i) if (!dcs[i].used) {
        dcs[i] = (gdi_dc){1, -1, -1, 0, 0xFFFFFF, 1};
        return 0x00040001u + (uint32_t)i;
    }
    return 0;
}

WINAPI_EXPORT("gdi32.dll", DeleteDC, 1)
{
    int index = (int)(ARG(0) - 0x00040001u);
    if (index < 0 || index >= GDI_MAX_DCS || !dcs[index].used) return 0;
    dcs[index].used = 0;
    return 1;
}

WINAPI_EXPORT("gdi32.dll", CreateDIBSection, 6)
{
    uint32_t info = ARG(1), out = ARG(3);
    if (!info || MEM32(info) < 40 || ARG(4)) return 0;
    int width = (int32_t)MEM32(info + 4), height = (int32_t)MEM32(info + 8);
    int top_down = height < 0;
    if (height < 0) height = -height;
    if (width <= 0 || height <= 0 || MEM16(info + 14) != 32 || MEM32(info + 16) != 0) return 0;
    for (int i = 0; i < GDI_MAX_DIBS; ++i) if (!dibs[i].used) {
        uint32_t bits = void_galloc((uint32_t)(width * height * 4));
        if (!bits) return 0;
        dibs[i] = (gdi_dib){1, width, height, top_down, bits};
        if (out) MEM32(out) = bits;
        return 0x00070001u + (uint32_t)i;
    }
    return 0;
}

WINAPI_EXPORT("gdi32.dll", TextOutA, 5)
{
    int index = (int)(ARG(0) - 0x00040001u);
    if (index < 0 || index >= GDI_MAX_DCS || !dcs[index].used) return 0;
    gdi_dc *dc = &dcs[index];
    if (dc->dib < 0) return 1;
    gdi_dib *dib = &dibs[dc->dib];
    if (!dib->used) return 0;
    int count = (int32_t)ARG(4);
    if (count < 0) count = (int)void_gstrlen(ARG(3));
    return gdi_render(dc, dib, gdi_current_font(dc), (int)ARG(1), (int)ARG(2), GPTR(ARG(3)), count);
}

WINAPI_EXPORT("gdi32.dll", GetTextExtentPoint32A, 4)
{
    int index = (int)(ARG(0) - 0x00040001u);
    if (index < 0 || index >= GDI_MAX_DCS || !dcs[index].used) return 0;
    int count = (int32_t)ARG(2);
    if (count < 0) count = (int)void_gstrlen(ARG(1));
    int size[2];
    if (!gdi_measure(GPTR(ARG(1)), count, gdi_current_font(&dcs[index]), size)) {
        size[0] = count * gdi_current_font(&dcs[index])->height / 2;
        size[1] = gdi_current_font(&dcs[index])->height;
    }
    MEM32(ARG(3)) = (uint32_t)size[0];
    MEM32(ARG(3) + 4) = (uint32_t)size[1];
    return 1;
}

WINAPI_EXPORT("gdi32.dll", GetTextMetricsA, 2)
{
    int index = (int)(ARG(0) - 0x00040001u);
    if (index < 0 || index >= GDI_MAX_DCS || !dcs[index].used) return 0;
    gdi_font *font = gdi_current_font(&dcs[index]);
    int values[11] = {font->height, font->height * 4 / 5, font->height / 5, 0, 0,
        font->height / 2, font->height, font->weight, 0, font->height, font->height};
    int attached;
    JNIEnv *env = gdi_env(&attached);
    if (env && gdi_metrics_method) {
        jintArray array = (jintArray)(*env)->CallStaticObjectMethod(env, gdi_class, gdi_metrics_method,
            font->height, font->weight, font->italic != 0);
        gdi_ints(env, array, values, 11);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        gdi_detach(attached);
    }
    uint32_t out = ARG(1);
    memset(GPTR(out), 0, 53);
    for (int i = 0; i < 11; ++i) MEM32(out + 4 * i) = (uint32_t)values[i];
    MEM8(out + 44) = 32;
    MEM8(out + 45) = 255;
    MEM8(out + 46) = 0;
    MEM8(out + 47) = 32;
    MEM8(out + 48) = (uint8_t)font->italic;
    MEM8(out + 51) = 0x22;
    MEM8(out + 52) = (uint8_t)font->charset;
    return 1;
}

WINAPI_EXPORT("gdi32.dll", AddFontMemResourceEx, 4) { return 1; }

WINAPI_EXPORT("gdi32.dll", GetGlyphOutlineA, 7)
{
    int index = (int)(ARG(0) - 0x00040001u);
    if (index < 0 || index >= GDI_MAX_DCS || !dcs[index].used) return GDI_ERROR_VALUE;
    int format = (int)ARG(2);
    if (format != 1 && format != 4 && format != 5 && format != 6) return GDI_ERROR_VALUE;
    int attached;
    JNIEnv *env = gdi_env(&attached);
    if (!env || !gdi_glyph_method) return GDI_ERROR_VALUE;
    jintArray array = (jintArray)(*env)->CallStaticObjectMethod(env, gdi_class, gdi_glyph_method,
        (jint)ARG(1), gdi_current_font(&dcs[index])->height,
        gdi_current_font(&dcs[index])->weight, gdi_current_font(&dcs[index])->italic != 0);
    if (!array) { gdi_detach(attached); return GDI_ERROR_VALUE; }
    int count = (*env)->GetArrayLength(env, array);
    jint *values = (*env)->GetIntArrayElements(env, array, NULL);
    if (!values || count < 5) { gdi_detach(attached); return GDI_ERROR_VALUE; }
    int width = values[0], height = values[1];
    int row_bytes = (width * (format == 1 ? 1 : format == 4 ? 2 : format == 5 ? 4 : 8) + 7) / 8;
    int stride = (row_bytes + 3) & ~3;
    uint32_t size = (uint32_t)(stride * height);
    if (ARG(4) && ARG(5)) {
        if (ARG(4) < size) {
            (*env)->ReleaseIntArrayElements(env, array, values, JNI_ABORT);
            (*env)->DeleteLocalRef(env, array);
            gdi_detach(attached);
            return GDI_ERROR_VALUE;
        }
        memset(GPTR(ARG(5)), 0, size);
        uint8_t *out = (uint8_t *)GPTR(ARG(5));
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            int alpha = values[5 + y * width + x];
            if (format == 1) {
                if (alpha >= 128) out[y * stride + x / 8] |= (uint8_t)(0x80 >> (x % 8));
            } else if (format == 4) {
                out[y * stride + x / 4] |= (uint8_t)((alpha * 3 / 255) << (6 - 2 * (x % 4)));
            } else if (format == 5) {
                out[y * stride + x / 2] |= (uint8_t)((alpha * 15 / 255) << (4 - 4 * (x % 2)));
            } else {
                out[y * stride + x] = (uint8_t)(alpha * 63 / 255);
            }
        }
    }
    if (ARG(3)) {
        memset(GPTR(ARG(3)), 0, 24);
        MEM32(ARG(3)) = (uint32_t)width;
        MEM32(ARG(3) + 4) = (uint32_t)height;
        MEM32(ARG(3) + 8) = (uint32_t)values[2];
        MEM32(ARG(3) + 12) = (uint32_t)values[3];
        MEM32(ARG(3) + 16) = (uint32_t)values[4];
        MEM32(ARG(3) + 20) = 0;
    }
    (*env)->ReleaseIntArrayElements(env, array, values, JNI_ABORT);
    (*env)->DeleteLocalRef(env, array);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); gdi_detach(attached); return GDI_ERROR_VALUE; }
    gdi_detach(attached);
    return size;
}

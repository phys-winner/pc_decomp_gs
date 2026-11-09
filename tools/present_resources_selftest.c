/* Exercise production presenter teardown without creating a GL context. */
#include "../app/native/host/d3d9_present.c"
#include "../app/native/host/d3d9_gl.c"
#include <assert.h>

static unsigned deleted_programs, deleted_textures, deleted_buffers, deleted_samplers, deleted_fbos;
static unsigned destroyed_contexts, destroyed_surfaces, released_threads;
static __thread EGLContext test_current_context;
static int require_current;
d3d_device *g_d3d_device;

void void_log(const char *format, ...) { (void)format; }
_Noreturn void void_fatal(const char *format, ...) { (void)format; abort(); }
EGLContext EGLAPIENTRY eglGetCurrentContext(void) { return test_current_context; }
EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay display, EGLSurface draw, EGLSurface read, EGLContext context)
{ (void)display; (void)draw; (void)read; test_current_context = context; return EGL_TRUE; }
EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay display, EGLSurface surface)
{ (void)display; assert(surface == worker_surface); ++destroyed_surfaces; return EGL_TRUE; }
EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay display, EGLContext context)
{ (void)display; assert(context == worker_context); ++destroyed_contexts; return EGL_TRUE; }
EGLBoolean EGLAPIENTRY eglReleaseThread(void)
{ assert(test_current_context == EGL_NO_CONTEXT); ++released_threads; return EGL_TRUE; }
EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint which) { (void)which; return worker_surface; }
EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay display, EGLConfig config, EGLContext share, const EGLint *attributes)
{ (void)display; (void)config; (void)share; (void)attributes; return (EGLContext)(uintptr_t)3; }
EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay display, EGLConfig config, const EGLint *attributes)
{ (void)display; (void)config; (void)attributes; return (EGLSurface)(uintptr_t)103; }
EGLint EGLAPIENTRY eglGetError(void) { return EGL_SUCCESS; }
uint32_t void_thread_id(void) { return 1; }
uint32_t void_com_release(void_com_object *object) { (void)object; abort(); }
void d3d_shader_forget_current(void) {}
void d3d_shader_reset(void) {}
void d3d_ff_reset(void) {}
static void check_context(void) { assert(!require_current || test_current_context != EGL_NO_CONTEXT); }
void GL_APIENTRY glGenVertexArrays(GLsizei count, GLuint *objects)
{ check_context(); for (GLsizei i = 0; i < count; ++i) objects[i] = 100 + i; }
void GL_APIENTRY glBindVertexArray(GLuint object) { (void)object; check_context(); }
void GL_APIENTRY glDeleteVertexArrays(GLsizei count, const GLuint *objects)
{ (void)count; (void)objects; check_context(); }
void GL_APIENTRY glUseProgram(GLuint object) { (void)object; check_context(); }
void GL_APIENTRY glBindFramebuffer(GLenum target, GLuint object) { (void)target; (void)object; check_context(); }
void GL_APIENTRY glBindBuffer(GLenum target, GLuint object) { (void)target; (void)object; check_context(); }
GLboolean GL_APIENTRY glUnmapBuffer(GLenum target) { (void)target; check_context(); return GL_TRUE; }
void GL_APIENTRY glDeleteSync(GLsync object) { (void)object; check_context(); }
void GL_APIENTRY glActiveTexture(GLenum unit) { (void)unit; check_context(); }
void GL_APIENTRY glBindTexture(GLenum target, GLuint object) { (void)target; (void)object; check_context(); }
void GL_APIENTRY glBindSampler(GLuint unit, GLuint sampler) { (void)unit; (void)sampler; check_context(); }

static void *worker_with_context(void *argument)
{
    worker_gl_resource *resource = malloc(sizeof *resource);
    assert(resource);
    worker_context = test_current_context = argument;
    worker_surface = (EGLSurface)((uintptr_t)argument + 100);
    *resource = (worker_gl_resource){worker_context, worker_surface, NULL};
    pthread_once(&worker_gl_key_once, worker_gl_key_init);
    assert(!pthread_setspecific(worker_gl_key, resource));
    return NULL; /* The real production pthread-key destructor runs here. */
}

static void finish_worker(EGLContext context)
{
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, worker_with_context, context));
    assert(!pthread_join(thread, NULL));
}

void GL_APIENTRY glDeleteProgram(GLuint program) { check_context(); if (program) ++deleted_programs; }
void GL_APIENTRY glDeleteTextures(GLsizei count, const GLuint *objects)
{ check_context(); for (int i = 0; i < count; ++i) if (objects[i]) ++deleted_textures; }
void GL_APIENTRY glDeleteBuffers(GLsizei count, const GLuint *objects)
{ check_context(); for (int i = 0; i < count; ++i) if (objects[i]) ++deleted_buffers; }
void GL_APIENTRY glDeleteSamplers(GLsizei count, const GLuint *objects)
{ check_context(); for (int i = 0; i < count; ++i) if (objects[i]) ++deleted_samplers; }
void GL_APIENTRY glDeleteFramebuffers(GLsizei count, const GLuint *objects)
{ check_context(); for (int i = 0; i < count; ++i) if (objects[i]) ++deleted_fbos; }

int main(void)
{
    programs[P_EASU] = 11; programs[P_RCAS] = 12; programs[P_WIDESCREEN] = 13;
    quad_buffer = 21;
    samplers[0] = 31; samplers[1] = 32;
    intermediate = 41; intermediate_fbo = 51; artwork_texture = 61;
    intermediate_w = 1920; intermediate_h = 1080;
    artwork_width = 1800; artwork_height = 600; artwork_id = 2; last_filter = 1;
    d3d_present_reset();
    assert(deleted_programs == 3 && deleted_textures == 2 && deleted_buffers == 1 &&
        deleted_samplers == 2 && deleted_fbos == 1);
    gl_states = calloc(2, sizeof *gl_states);
    assert(gl_states);
    gl_state_count = 2;
    gl_states[0].context = (EGLContext)(uintptr_t)1;
    gl_states[1].context = (EGLContext)(uintptr_t)2;
    gl_state = gl_states;
    finish_worker(gl_states[1].context);
    assert(destroyed_contexts == 1 && destroyed_surfaces == 1 && released_threads == 1);
    assert(gl_state_count == 2); /* Cache mutation waits for the serialized GL call. */
    d3d_gl_retire_contexts();
    assert(gl_state_count == 1 && gl_state == gl_states && gl_state->context == (EGLContext)(uintptr_t)1);
    finish_worker(gl_states[0].context);
    d3d_gl_retire_contexts();
    assert(destroyed_contexts == 2 && destroyed_surfaces == 2 && released_threads == 2);
    assert(!gl_state_count && !gl_state && !retired_contexts);
    free(gl_states); gl_states = NULL;
    for (unsigned i = 0; i < P_COUNT; ++i) assert(!programs[i]);
    assert(!quad_buffer && !samplers[0] && !samplers[1] && !intermediate &&
        !intermediate_fbo && !artwork_texture);
    assert(!intermediate_w && !intermediate_h && !artwork_width && !artwork_height);
    assert(artwork_id == -2 && last_filter == -1);
    /* A second teardown is harmless and the next device can reload the same art. */
    d3d_present_reset();
    assert(deleted_programs == 3 && deleted_textures == 2 && deleted_buffers == 1 &&
        deleted_samplers == 2 && deleted_fbos == 1);
    require_current = 1;
    egl_context = test_current_context = (EGLContext)(uintptr_t)1;
    artwork_texture = 71; programs[P_WIDESCREEN] = 72;
    d3d_gl_device_destroy();
    assert(test_current_context == egl_context); /* Preserve the caller's current context. */
    assert(deleted_programs == 4 && deleted_textures == 3);
    test_current_context = EGL_NO_CONTEXT;
    artwork_texture = 73; programs[P_WIDESCREEN] = 74;
    d3d_gl_device_destroy();
    assert(test_current_context == EGL_NO_CONTEXT); /* Release only the context acquired here. */
    assert(deleted_programs == 5 && deleted_textures == 4);
    worker_gl_resource *resource = pthread_getspecific(worker_gl_key);
    assert(resource && !pthread_setspecific(worker_gl_key, NULL));
    worker_gl_destroy(resource);
    d3d_gl_retire_contexts();
    free(gl_states); gl_states = gl_state = NULL; gl_state_count = 0;
    puts("PASS: presenter resets all GL objects; device teardown acquires missing context and preserves caller ownership; worker exit destroys EGL objects and retires CPU cache safely");
    return 0;
}

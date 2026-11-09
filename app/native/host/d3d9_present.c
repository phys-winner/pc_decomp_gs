#include "d3d9_present.h"
#include "fsr1.h"
#include "grief_menu.h"
#include "grief_widescreen.h"
#include "widescreen_layout.h"

#define P_BILINEAR 0
#define P_PIXELS 1
#define P_BICUBIC 2
#define P_EASU 3
#define P_RCAS 4
#define P_WIDESCREEN 5
#define P_COUNT 6

static GLuint programs[P_COUNT], quad_buffer, samplers[2], intermediate, intermediate_fbo;
static int intermediate_w, intermediate_h, last_filter = -1;
static GLuint artwork_texture;
static int artwork_id = -2;
static uint32_t artwork_width, artwork_height;

void d3d_present_reset(void)
{
    for (unsigned i = 0; i < P_COUNT; ++i) {
        if (programs[i]) glDeleteProgram(programs[i]);
        programs[i] = 0;
    }
    if (quad_buffer) glDeleteBuffers(1, &quad_buffer);
    glDeleteSamplers(2, samplers);
    if (intermediate) glDeleteTextures(1, &intermediate);
    if (intermediate_fbo) glDeleteFramebuffers(1, &intermediate_fbo);
    if (artwork_texture) glDeleteTextures(1, &artwork_texture);
    quad_buffer = samplers[0] = samplers[1] = intermediate = intermediate_fbo = artwork_texture = 0;
    intermediate_w = intermediate_h = artwork_width = artwork_height = 0;
    artwork_id = -2;
    last_filter = -1;
}

typedef struct {
    GLint image, dest, source, output, flip, source_size, texture_size, outscale, sharp;
} locations;
static locations uniforms[P_COUNT];

static GLuint shader(GLenum type, int count, const char **parts, const char *name)
{
    char *joined = NULL;
    if (count > 1) {
        size_t bytes = 1;
        for (int i = 0; i < count; ++i) bytes += strlen(parts[i]);
        joined = (char *)malloc(bytes);
        if (!joined) return 0;
        char *out = joined;
        for (int i = 0; i < count; ++i) {
            size_t length = strlen(parts[i]);
            memcpy(out, parts[i], length);
            out += length;
        }
        *out = 0;
        parts = (const char **)&joined;
        count = 1;
    }
    GLuint id = glCreateShader(type);
    GLint source_length = (GLint)strlen(parts[0]);
    glShaderSource(id, 1, parts, &source_length);
    glCompileShader(id);
    free(joined);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        GLint written = 0;
        glGetShaderInfoLog(id, sizeof log - 1, &written, log);
        if (written < 0 || written >= (GLint)sizeof log) written = sizeof log - 1;
        log[written] = 0;
        void_log("[present] %s: %s", name, log);
        glDeleteShader(id);
        return 0;
    }
    return id;
}

static const char vertex_source[] =
    "#version 300 es\nprecision highp float;\n"
    "layout(location=0) in vec2 p;uniform vec4 dest;uniform vec4 source_rect;"
    "uniform vec2 output_size;uniform float flip_y;out highp vec2 source_position;"
    "void main(){vec2 d=mix(dest.xy,dest.zw,p);source_position=mix(source_rect.xy,source_rect.zw,p);"
    "float y=d.y/output_size.y;gl_Position=vec4(d.x/output_size.x*2.0-1.0,"
    "mix(y*2.0-1.0,1.0-y*2.0,flip_y),0.0,1.0);}\n";

static const char fragment_common[] =
    "precision mediump float;precision highp int;precision highp sampler2D;"
    "uniform highp sampler2D image;uniform highp vec2 source_size;uniform highp vec2 texture_size;"
    "uniform highp vec2 outscale;uniform mediump float sharp;in highp vec2 source_position;"
    "layout(location=0) out mediump vec4 color;"
    "vec4 tap(highp vec2 p){return texture(image,(clamp(p,vec2(0.0),source_size-0.5)+0.5)/texture_size);}\n"
    "#if MODE==3\n"
    "#define FSR_ALPHA 0\n"
    "#define FSR_EASU_BASE(fp) highp vec2 fb=fp;\n"
    "#define T(x,y) texture(image,(clamp(fb+vec2(float(x),float(y)),vec2(0.0),source_size-1.0)+0.5)/texture_size)\n"
    "#elif MODE==4\n"
    "#define FSR_ALPHA 0\n"
    "#define FSR_RCAS_BASE highp vec2 q=source_position;\n"
    "#define L(x,y) texture(image,(clamp(q+vec2(float(x),float(y)),vec2(0.0),source_size-1.0)+0.5)/texture_size)\n"
    "#endif\n";

static const char fragment_main[] =
    "#if MODE==5\n"
    "highp float edge(highp float x){highp float w=texture_size.x,e=min(256.0,w*0.25);"
    "highp float d=x<0.0?-x:x>w?x-w:0.0;"
    "highp float f=e-abs(mod(d,e*2.0)-e);return x<0.0?f:x>w?w-f:x;}"
    "void main(){highp vec2 p=vec2(edge(source_position.x),source_position.y);"
    "color=vec4(texture(image,(clamp(p,vec2(0.5),texture_size-0.5))/texture_size).rgb*sharp,1.0);}\n"
    "#elif MODE==0\n"
    "void main(){color=vec4(texture(image,clamp(source_position,vec2(0.5),source_size-0.5)/texture_size).rgb,1.0);}\n"
    "#elif MODE==1\n"
    "vec4 filtered(highp vec2 p){highp vec2 f=floor(p),c=p-f-0.5,r=max(vec2(0.0),0.5-0.5/outscale);"
    "return tap(f+(c-clamp(c,-r,r))*outscale+0.5);}\n"
    "void main(){color=vec4(clamp(filtered(source_position).rgb,0.0,1.0),1.0);}\n"
    "#elif MODE==2\n"
    "vec4 filtered(highp vec2 p){highp vec2 t1=floor(p-0.5)+0.5,f=p-t1;"
    "vec2 w0=f*(-0.5+f*(1.0-0.5*f)),w1=1.0+f*f*(-2.5+1.5*f),w2=f*(0.5+f*(2.0-1.5*f)),w3=f*f*(-0.5+0.5*f);"
    "vec2 w12=w1+w2,t0=t1-1.0,t3=t1+2.0,tc=t1+w2/w12;"
    "return tap(t0)*w0.x*w0.y+tap(vec2(tc.x,t0.y))*w12.x*w0.y+tap(vec2(t3.x,t0.y))*w3.x*w0.y"
    "+tap(vec2(t0.x,tc.y))*w0.x*w12.y+tap(tc)*w12.x*w12.y+tap(vec2(t3.x,tc.y))*w3.x*w12.y"
    "+tap(vec2(t0.x,t3.y))*w0.x*w3.y+tap(vec2(tc.x,t3.y))*w12.x*w3.y+tap(t3)*w3.x*w3.y;}\n"
    "void main(){color=vec4(clamp(filtered(source_position).rgb,0.0,1.0),1.0);}\n"
    "#elif MODE==3\n"
    "void main(){color=vec4(clamp(FsrEasuH(source_position).rgb,0.0,1.0),1.0);}\n"
    "#else\n"
    "void main(){color=vec4(clamp(FsrRcasH(sharp).rgb,0.0,1.0),1.0);}\n"
    "#endif\n";

static GLuint program(int mode)
{
    if (programs[mode]) return programs[mode];
    char define[32];
    snprintf(define, sizeof define, "#define MODE %d\n", mode);
    const char *parts[6] = { "#version 300 es\n", define, fragment_common,
        mode == P_EASU ? fsr1_easu_glsl : "", mode == P_RCAS ? fsr1_rcas_glsl : "", fragment_main };
    char name[24];
    snprintf(name, sizeof name, "filter %d", mode);
    const char *vertex = vertex_source;
    GLuint vs = shader(GL_VERTEX_SHADER, 1, &vertex, name);
    GLuint fs = shader(GL_FRAGMENT_SHADER, 6, parts, name);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint id = glCreateProgram();
    glAttachShader(id, vs);
    glAttachShader(id, fs);
    glBindAttribLocation(id, 0, "p");
    glLinkProgram(id);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        GLint written = 0;
        glGetProgramInfoLog(id, sizeof log - 1, &written, log);
        if (written < 0 || written >= (GLint)sizeof log) written = sizeof log - 1;
        log[written] = 0;
        void_log("[present] link %d: %s", mode, log);
        glDeleteProgram(id);
        return 0;
    }
    locations *u = &uniforms[mode];
#define U(field, text) u->field = glGetUniformLocation(id, text)
    U(image, "image"); U(dest, "dest"); U(source, "source_rect"); U(output, "output_size");
    U(flip, "flip_y"); U(source_size, "source_size"); U(texture_size, "texture_size");
    U(outscale, "outscale"); U(sharp, "sharp");
#undef U
    glUseProgram(id);
    glUniform1i(u->image, 0);
    programs[mode] = id;
    return id;
}

static int ensure_intermediate(int w, int h)
{
    if (intermediate && intermediate_w == w && intermediate_h == h) return 1;
    if (intermediate) {
        glDeleteTextures(1, &intermediate);
        glDeleteFramebuffers(1, &intermediate_fbo);
        intermediate = intermediate_fbo = 0;
    }
    glGenTextures(1, &intermediate);
    glBindTexture(GL_TEXTURE_2D, intermediate);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &intermediate_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, intermediate_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, intermediate, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteTextures(1, &intermediate);
        glDeleteFramebuffers(1, &intermediate_fbo);
        intermediate = intermediate_fbo = 0;
        return 0;
    }
    intermediate_w = w;
    intermediate_h = h;
    return 1;
}

static void prepare_quad(void)
{
    if (quad_buffer) return;
    static const GLfloat points[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    glGenBuffers(1, &quad_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, quad_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof points, points, GL_STATIC_DRAW);
    glGenSamplers(2, samplers);
    for (int i = 0; i < 2; ++i) {
        glSamplerParameteri(samplers[i], GL_TEXTURE_MIN_FILTER, i ? GL_LINEAR : GL_NEAREST);
        glSamplerParameteri(samplers[i], GL_TEXTURE_MAG_FILTER, i ? GL_LINEAR : GL_NEAREST);
        glSamplerParameteri(samplers[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(samplers[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

static void draw_pass(int mode, GLuint texture, int source_w, int source_h, int texture_w, int texture_h,
    int target_w, int target_h, int dx, int dy, int dw, int dh, int flip, float sharp,
    float source_left, float source_right)
{
    locations *u = &uniforms[mode];
    glUseProgram(programs[mode]);
    glViewport(0, 0, target_w, target_h);
    glUniform4f(u->dest, (float)dx, (float)dy, (float)(dx + dw), (float)(dy + dh));
    glUniform4f(u->source, source_left, 0, source_right, (float)source_h);
    glUniform2f(u->output, (float)target_w, (float)target_h);
    glUniform1f(u->flip, (float)flip);
    glUniform2f(u->source_size, (float)source_w, (float)source_h);
    glUniform2f(u->texture_size, (float)texture_w, (float)texture_h);
    glUniform2f(u->outscale, (float)dw / source_w, (float)dh / source_h);
    glUniform1f(u->sharp, sharp);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindSampler(0, samplers[mode == P_EASU || mode == P_RCAS ? 0 : 1]);
    glBindBuffer(GL_ARRAY_BUFFER, quad_buffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    for (int i = 1; i < 16; ++i) glDisableVertexAttribArray(i);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int d3d_present_filtered(d3d_surface *surface, int source_w, int sw, int sh,
    int dx, int dy, int dw, int dh, int filter)
{
    if (!surface || filter < P_BILINEAR || filter > P_RCAS - 1 || source_w <= 0 ||
        source_w > (int)surface->width || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return 0;
    // Framebuffer blits sample outside a source crop at its last output pixel.
    if (filter == P_BILINEAR && source_w == (int)surface->width) return 0;
    GLenum target;
    GLint level, layer;
    GLuint texture = d3d_surface_gl_texture(surface, &target, &level, &layer);
    if (!texture || target != GL_TEXTURE_2D || level != 0) return 0;
    int fsr = filter == 1;
    int easu = filter == 0 ? P_BILINEAR : fsr ? P_EASU : filter == 3 ? P_BICUBIC : P_PIXELS;
    if (!program(easu) || (fsr && !program(P_RCAS))) {
        if (!program(P_BICUBIC)) return 0;
        filter = 3;
        fsr = 0;
        easu = P_BICUBIC;
    }
    prepare_quad();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (fsr) {
        if (!ensure_intermediate(sw, sh)) return 0;
        glBindFramebuffer(GL_FRAMEBUFFER, intermediate_fbo);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        draw_pass(P_EASU, texture, source_w, surface->height, surface->width, surface->height,
            sw, sh, dx, dy, dw, dh, 0, 0, 0, (float)source_w);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        /* EASU already placed the image and its bars in display coordinates. */
        draw_pass(P_RCAS, intermediate, sw, sh, sw, sh, sw, sh, 0, 0, sw, sh, 1, fsr1_rcas_con(1.0f), 0, (float)sw);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        draw_pass(easu, texture, source_w, surface->height, surface->width, surface->height,
            sw, sh, dx, dy, dw, dh, 1, 0, 0, (float)source_w);
    }
    glBindSampler(0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (last_filter != filter) {
        static const char *names[] = { "bilinear", "FSR 1.0 EASU+RCAS", "sharp pixels", "bicubic" };
        void_log("[present] filter: %s", names[filter]);
        last_filter = filter;
    }
    return 1;
}

void d3d_present_widescreen(int sw, int sh, int dx, int dy, int dw, int dh)
{
    int alpha = 0, id = void_grief_widescreen_state(&alpha);
    if (!void_grief_widescreen_mounted() || id < 0 || id > 6 || alpha <= 0 || dx <= 0 || dh <= 0) return;
    if (id != artwork_id) {
        if (artwork_texture) glDeleteTextures(1, &artwork_texture);
        artwork_texture = 0;
        artwork_id = id;
        uint8_t *pixels = void_grief_widescreen_image((unsigned)id, &artwork_width, &artwork_height);
        if (pixels) {
            glGenTextures(1, &artwork_texture);
            glBindTexture(GL_TEXTURE_2D, artwork_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, artwork_width, artwork_height, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            free(pixels);
        }
    }
    if (!artwork_texture || !program(P_WIDESCREEN)) return;
    prepare_quad();
    float left, right;
    widescreen_source_span(sw, dh, artwork_width, artwork_height, &left, &right);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_SCISSOR_TEST);
    /* FSR's final pass writes black bars; paint the two side bands afterwards
     * and leave every pixel of the original 800-pixel scene untouched. */
    glScissor(0, sh - dy - dh, dx, dh);
    draw_pass(P_WIDESCREEN, artwork_texture, artwork_width, artwork_height, artwork_width, artwork_height,
        sw, sh, 0, dy, sw, dh, 1, alpha * 0.001f, left, right);
    glScissor(dx + dw, sh - dy - dh, sw - dx - dw, dh);
    draw_pass(P_WIDESCREEN, artwork_texture, artwork_width, artwork_height, artwork_width, artwork_height,
        sw, sh, 0, dy, sw, dh, 1, alpha * 0.001f, left, right);
    glDisable(GL_SCISSOR_TEST);
    glBindSampler(0, 0);
}

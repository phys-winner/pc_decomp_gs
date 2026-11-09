/* Production resource destructors must select a context even between frames. */
#include "../app/native/host/d3d9_resource.c"
#include <assert.h>

static int current, attaches, detaches, textures_deleted, buffers_deleted, guest_frees, mips_generated, fail_attach;
int d3d_gl_make_current(void)
{ if (fail_attach) return 0; if (current) return 1; current = 1; ++attaches; return 2; }
void d3d_gl_release_current(void) { assert(current); current = 0; ++detaches; }
void d3d_gl_forget_texture(GLuint name) { assert(current && name); }
void d3d_gl_forget_buffer(GLuint name) { assert(current && name); }
void glDeleteTextures(GLsizei count, const GLuint *names)
{ assert(current && count == 1 && names[0]); ++textures_deleted; }
void glDeleteBuffers(GLsizei count, const GLuint *names)
{ assert(current && count == 1 && names[0]); ++buffers_deleted; }
void d3d_gl_upload_unit(void) { assert(current); }
void glBindTexture(GLenum target, GLuint name) { assert(current && target == GL_TEXTURE_2D && name == 14); }
void glGenerateMipmap(GLenum target) { assert(current && target == GL_TEXTURE_2D); ++mips_generated; }
void void_gfree(uint32_t address) { assert(address); ++guest_frees; }
uint32_t void_com_release(void_com_object *o)
{ assert(o->refs == 1 && o->destroy); o->destroy(o); return 0; }

static void cycle(int already_current)
{
    current = already_current;
    int old_attaches = attaches, old_detaches = detaches;
    d3d_surface *surface = calloc(1, sizeof *surface);
    surface->gl = 11; surface->sysmem = 1;
    surface_destroy(&surface->com);
    assert(current == already_current);

    d3d_texture *texture = calloc(1, sizeof *texture);
    texture->gl = 12; texture->faces = texture->levels = 1;
    texture->surfaces = calloc(1, sizeof *texture->surfaces);
    texture->sysmem = calloc(1, sizeof *texture->sysmem);
    surface = calloc(1, sizeof *surface);
    surface->container = texture; surface->com.destroy = surface_destroy;
    texture->surfaces[0] = surface; texture->sysmem[0] = 2;
    texture_destroy(&texture->com);
    assert(current == already_current);

    d3d_buffer *buffer = calloc(1, sizeof *buffer);
    buffer->gl = 13; buffer->shadow = 3; buffer->dirty_pages = calloc(1, 1);
    buffer_destroy(&buffer->com);
    assert(current == already_current);

    d3d_texture mip = {.gl = 14, .levels = 4, .target = GL_TEXTURE_2D, .mips_dirty = 1};
    d3d_generate_mips(&mip);
    assert(!mip.mips_dirty && current == already_current);
    assert(attaches - old_attaches == (already_current ? 0 : 4));
    assert(detaches - old_detaches == (already_current ? 0 : 4));
    mip.native = (const d3d_native_format *)1;
    mip.mips_dirty = 1;
    d3d_generate_mips(&mip);
    assert(mip.mips_dirty && attaches - old_attaches == (already_current ? 0 : 4));
}

int main(void)
{
    for (unsigned i = 0; i < 2048; ++i) cycle(i & 1);
    assert(textures_deleted == 4096 && buffers_deleted == 2048 && guest_frees == 6144 && mips_generated == 2048);
    current = 0; fail_attach = 1;
    d3d_texture mip = {.gl = 14, .levels = 4, .target = GL_TEXTURE_2D, .mips_dirty = 1};
    d3d_generate_mips(&mip);
    assert(mip.mips_dirty && mips_generated == 2048);
    puts("PASS: 2048 production resource destruction/mipmap cycles use a current GL context, preserve caller ownership, skip native mip chains and retain dirty state on attach failure");
    return 0;
}

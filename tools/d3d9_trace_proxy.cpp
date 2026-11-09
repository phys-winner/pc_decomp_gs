#include <windows.h>
#define WINBOOL BOOL
#define __MSABI_LONG(x) x
#include <d3d9.h>
#include <cstdio>

static FILE *log_file;
static HRESULT (WINAPI *create_device)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
static HRESULT (WINAPI *set_texture)(IDirect3DDevice9 *, DWORD, IDirect3DBaseTexture9 *);
static HRESULT (WINAPI *draw_primitive)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT);
static HRESULT (WINAPI *draw_up)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
static HRESULT (WINAPI *draw_indexed_up)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT, const void *, D3DFORMAT, const void *, UINT);
static HRESULT (WINAPI *present)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
static HRESULT (WINAPI *set_pixel_shader)(IDirect3DDevice9 *, IDirect3DPixelShader9 *);

static void log_texture(DWORD stage, IDirect3DBaseTexture9 *texture)
{
    if (!log_file) return;
    D3DSURFACE_DESC desc = {};
    if (texture && texture->GetType() == D3DRTYPE_TEXTURE)
        reinterpret_cast<IDirect3DTexture9 *>(texture)->GetLevelDesc(0, &desc);
    fprintf(log_file, "texture slot=%u ptr=%p size=%ux%u format=%u\n", stage, texture, desc.Width, desc.Height, desc.Format);
}

static void log_draw(IDirect3DDevice9 *device, const char *kind, const void *vertices, UINT stride)
{
    IDirect3DBaseTexture9 *texture = nullptr;
    device->GetTexture(0, &texture);
    D3DSURFACE_DESC desc = {};
    if (texture && texture->GetType() == D3DRTYPE_TEXTURE)
        reinterpret_cast<IDirect3DTexture9 *>(texture)->GetLevelDesc(0, &desc);
    if (log_file) {
        fprintf(log_file, "%s size=%ux%u format=%u\n", kind, desc.Width, desc.Height, desc.Format);
        if (vertices && ((desc.Width == 128 && desc.Height == 384) ||
            (desc.Format == D3DFMT_A1R5G5B5 && ((desc.Width == 128 && desc.Height == 128) ||
            (desc.Width == 96 && desc.Height == 156))))) {
            const DWORD *v = reinterpret_cast<const DWORD *>(vertices);
            fprintf(log_file, "sprite %ux%u fmt=%u stride=%u\n", desc.Width, desc.Height, desc.Format, stride);
            for (unsigned i = 0; i < 4; ++i) {
                float x, y, u, w;
                memcpy(&x, v, 4); memcpy(&y, v + 1, 4); memcpy(&u, v + 5, 4); memcpy(&w, v + 6, 4);
                fprintf(log_file, "vertex x=%g y=%g u=%g v=%g diffuse=%08X\n", x, y, u, w, v[4]);
                v += stride / 4;
            }
            fflush(log_file);
        }
    }
    if (texture) texture->Release();
}

static HRESULT WINAPI hook_set_texture(IDirect3DDevice9 *device, DWORD stage, IDirect3DBaseTexture9 *texture)
{
    log_texture(stage, texture);
    return set_texture(device, stage, texture);
}

static HRESULT WINAPI hook_set_pixel_shader(IDirect3DDevice9 *device, IDirect3DPixelShader9 *shader)
{
    if (log_file) fprintf(log_file, "SetPixelShader ptr=%p\n", shader);
    return set_pixel_shader(device, shader);
}

static HRESULT WINAPI hook_draw_primitive(IDirect3DDevice9 *device, D3DPRIMITIVETYPE type, UINT start, UINT count)
{
    log_draw(device, "DrawPrimitive", nullptr, 0);
    return draw_primitive(device, type, start, count);
}

static HRESULT WINAPI hook_draw_up(IDirect3DDevice9 *device, D3DPRIMITIVETYPE type, UINT count, const void *data, UINT stride)
{
    log_draw(device, "DrawPrimitiveUP", data, stride);
    return draw_up(device, type, count, data, stride);
}

static HRESULT WINAPI hook_draw_indexed_up(IDirect3DDevice9 *device, D3DPRIMITIVETYPE type, UINT min, UINT count, const void *indices, D3DFORMAT format, const void *data, UINT stride)
{
    log_draw(device, "DrawIndexedPrimitiveUP", data, stride);
    return draw_indexed_up(device, type, min, count, indices, format, data, stride);
}

static void capture_frame(IDirect3DDevice9 *device)
{
    static unsigned frame;
    ++frame;
    if (frame % 300 != 0) return;
    using SaveSurface = HRESULT (WINAPI *)(const char *, int, IDirect3DSurface9 *, const PALETTEENTRY *, const RECT *);
    auto save = reinterpret_cast<SaveSurface>(GetProcAddress(LoadLibraryA("d3dx9_43.dll"), "D3DXSaveSurfaceToFileA"));
    IDirect3DSurface9 *surface = nullptr;
    if (save && SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &surface)) && surface) {
        char path[64];
        sprintf_s(path, "capture_%04u.bmp", frame);
        save(path, 0, surface, nullptr, nullptr);
        surface->Release();
        if (log_file) fprintf(log_file, "capture=%s\n", path);
    }
}

static HRESULT (WINAPI *swapchain_present)(IDirect3DSwapChain9 *, const RECT *, const RECT *, HWND, const RGNDATA *, DWORD);

static HRESULT WINAPI hook_swapchain_present(IDirect3DSwapChain9 *chain, const RECT *src, const RECT *dst, HWND window,
    const RGNDATA *region, DWORD flags)
{
    IDirect3DDevice9 *device = nullptr;
    chain->GetDevice(&device);
    if (device) {
        capture_frame(device);
        device->Release();
    }
    if (log_file) { fputs("Present\n", log_file); fflush(log_file); }
    return swapchain_present(chain, src, dst, window, region, flags);
}

static HRESULT WINAPI hook_present(IDirect3DDevice9 *device, const RECT *src, const RECT *dst, HWND window, const RGNDATA *region)
{
    capture_frame(device);
    if (log_file) { fputs("Present\n", log_file); fflush(log_file); }
    return present(device, src, dst, window, region);
}

static void hook_method(void *object, int method, void *replacement, void **original)
{
    void **vtable = *reinterpret_cast<void ***>(object);
    DWORD old;
    if (!*original) *original = vtable[method];
    if (VirtualProtect(vtable + method, sizeof(void *), PAGE_READWRITE, &old)) {
        vtable[method] = replacement;
        VirtualProtect(vtable + method, sizeof(void *), old, &old);
    }
}

static HRESULT WINAPI hook_create_device(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
    D3DPRESENT_PARAMETERS *parameters, IDirect3DDevice9 **device)
{
    HRESULT result = create_device(d3d, adapter, type, window, flags, parameters, device);
    if (SUCCEEDED(result) && device && *device) {
        hook_method(*device, 65, hook_set_texture, reinterpret_cast<void **>(&set_texture));
        hook_method(*device, 107, hook_set_pixel_shader, reinterpret_cast<void **>(&set_pixel_shader));
        hook_method(*device, 81, hook_draw_primitive, reinterpret_cast<void **>(&draw_primitive));
        hook_method(*device, 83, hook_draw_up, reinterpret_cast<void **>(&draw_up));
        hook_method(*device, 84, hook_draw_indexed_up, reinterpret_cast<void **>(&draw_indexed_up));
        hook_method(*device, 17, hook_present, reinterpret_cast<void **>(&present));
        IDirect3DSwapChain9 *chain = nullptr;
        if (SUCCEEDED((*device)->GetSwapChain(0, &chain)) && chain) {
            hook_method(chain, 3, hook_swapchain_present, reinterpret_cast<void **>(&swapchain_present));
            chain->Release();
        }
        if (log_file) { fprintf(log_file, "device=%p result=%08X\n", *device, result); fflush(log_file); }
    }
    return result;
}

extern "C" IDirect3D9 *WINAPI Direct3DCreate9(UINT version)
{
    HMODULE library = LoadLibraryA("C:\\Windows\\SysWOW64\\d3d9.dll");
    if (!library) return nullptr;
    auto create = reinterpret_cast<decltype(&Direct3DCreate9)>(GetProcAddress(library, "Direct3DCreate9"));
    if (!create) return nullptr;
    if (!log_file) {
        fopen_s(&log_file, "d3dtrace.txt", "w");
        if (log_file) setvbuf(log_file, nullptr, _IOLBF, 4096);
    }
    IDirect3D9 *d3d = create(version);
    if (d3d) hook_method(d3d, 16, hook_create_device, reinterpret_cast<void **>(&create_device));
    return d3d;
}

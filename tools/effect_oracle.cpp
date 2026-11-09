#include <windows.h>
#define WINBOOL BOOL
#define __MSABI_LONG(x) x
#include <d3dx9.h>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")

using CreateEffectFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, const void *, UINT,
    const D3DXMACRO *, ID3DXInclude *, DWORD, ID3DXEffectPool *, ID3DXEffect **, ID3DXBuffer **);

static std::string json_string(const char *text)
{
    std::string out = "\"";
    for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; ++p) {
        if (*p == '\\' || *p == '\"') { out += '\\'; out += *p; }
        else if (*p < 0x20) { char buffer[8]; sprintf(buffer, "\\u%04x", *p); out += buffer; }
        else out += *p;
    }
    return out + "\"";
}

static std::string hex_value(const void *value)
{
    std::ostringstream out;
    out << "\"0x" << std::hex << std::uppercase << (uintptr_t)value << "\"";
    return out.str();
}

static std::vector<uint8_t> read_file(const char *path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot read effect input");
    std::streamsize size = input.tellg();
    input.seekg(0);
    std::vector<uint8_t> data((size_t)size);
    input.read((char *)data.data(), size);
    if (!input) throw std::runtime_error("short effect input");
    return data;
}

static size_t shader_size(const DWORD *shader)
{
    if (!shader) return 0;
    size_t tokens = 0;
    for (; tokens < 65536; ++tokens) if ((shader[tokens] & 0xFFFFu) == 0xFFFFu) return (tokens + 1) * 4;
    throw std::runtime_error("unterminated shader bytecode");
}

static void dump_shader(const std::string &directory, size_t technique, size_t pass,
    const char *kind, const DWORD *shader)
{
    if (!shader) return;
    size_t size = shader_size(shader);
    std::ostringstream name;
    name << directory << "/t" << technique << "p" << pass << "_" << kind << ".bin";
    std::ofstream output(name.str(), std::ios::binary);
    output.write((const char *)shader, (std::streamsize)size);
    if (!output) throw std::runtime_error("cannot write shader");
}

static std::string capture_state(IDirect3DDevice9 *device)
{
    std::ostringstream out;
    out << "\"render_states\":{";
    bool first = true;
    for (DWORD state = 0; state < 220; ++state) {
        DWORD value;
        if (FAILED(device->GetRenderState((D3DRENDERSTATETYPE)state, &value))) continue;
        if (!first) out << ',';
        first = false;
        out << '\"' << state << "\":" << value;
    }
    out << "},\"texture_stage_states\":{";
    first = true;
    for (DWORD stage = 0; stage < 16; ++stage) for (DWORD type = 0; type < 40; ++type) {
        DWORD value;
        if (FAILED(device->GetTextureStageState(stage, (D3DTEXTURESTAGESTATETYPE)type, &value))) continue;
        if (!first) out << ',';
        first = false;
        out << '\"' << stage << ':' << type << "\":" << value;
    }
    out << "},\"sampler_states\":{";
    first = true;
    for (DWORD stage = 0; stage < 16; ++stage) for (DWORD type = 0; type < 16; ++type) {
        DWORD value;
        if (FAILED(device->GetSamplerState(stage, (D3DSAMPLERSTATETYPE)type, &value))) continue;
        if (!first) out << ',';
        first = false;
        out << '\"' << stage << ':' << type << "\":" << value;
    }
    out << "},\"textures\":{";
    first = true;
    for (DWORD stage = 0; stage < 16; ++stage) {
        IDirect3DBaseTexture9 *texture = nullptr;
        if (FAILED(device->GetTexture(stage, &texture))) continue;
        if (!first) out << ',';
        first = false;
        out << '\"' << stage << "\":" << hex_value(texture);
        if (texture) texture->Release();
    }
    out << '}';
    DWORD fvf = 0;
    if (SUCCEEDED(device->GetFVF(&fvf))) out << ",\"fvf\":" << fvf;
    return out.str();
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        std::cerr << "usage: effect_oracle.exe EFFECT.bin OUTPUT_DIR D3DX9_43.dll\n";
        return 64;
    }
    try {
        std::vector<uint8_t> bytes = read_file(argv[1]);
        std::string output_dir = argv[2];
        CreateDirectoryA(output_dir.c_str(), nullptr);
        HMODULE d3dx = LoadLibraryA(argv[3]);
        HMODULE d3d = LoadLibraryA("d3d9.dll");
        if (!d3dx || !d3d) throw std::runtime_error("cannot load DirectX runtime");
        auto create_effect = (CreateEffectFn)GetProcAddress(d3dx, "D3DXCreateEffect");
        auto create9 = (IDirect3D9 *(WINAPI *)(UINT))GetProcAddress(d3d, "Direct3DCreate9");
        if (!create_effect || !create9) throw std::runtime_error("missing DirectX exports");
        HWND window = CreateWindowExA(0, "static", "FX oracle", WS_OVERLAPPED,
            0, 0, 64, 64, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
        if (!window) throw std::runtime_error("CreateWindow failed");
        IDirect3D9 *d3d9 = create9(D3D_SDK_VERSION);
        if (!d3d9) throw std::runtime_error("Direct3DCreate9 failed");
        D3DPRESENT_PARAMETERS pp = {};
        pp.BackBufferWidth = 64;
        pp.BackBufferHeight = 64;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.BackBufferCount = 1;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.Windowed = TRUE;
        pp.EnableAutoDepthStencil = TRUE;
        pp.AutoDepthStencilFormat = D3DFMT_D16;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        IDirect3DDevice9 *device = nullptr;
        HRESULT hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
            D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
        if (FAILED(hr)) hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
        if (FAILED(hr) || !device) {
            std::ostringstream message; message << "CreateDevice failed " << std::hex << hr;
            throw std::runtime_error(message.str());
        }
        ID3DXEffect *effect = nullptr;
        hr = create_effect(device, bytes.data(), (UINT)bytes.size(), nullptr, nullptr, 0, nullptr, &effect, nullptr);
        if (FAILED(hr) || !effect) {
            std::ostringstream message; message << "D3DXCreateEffect failed " << std::hex << hr;
            throw std::runtime_error(message.str());
        }
        D3DXEFFECT_DESC effect_desc = {};
        if (FAILED(effect->GetDesc(&effect_desc))) throw std::runtime_error("GetDesc failed");
        std::string baseline = capture_state(device);
        std::ostringstream json;
        json << "{\n  \"baseline\":{" << baseline << "},\n  \"techniques\":" << effect_desc.Techniques
             << ",\n  \"parameters\":" << effect_desc.Parameters
             << ",\n  \"functions\":" << effect_desc.Functions << ",\n  \"parameter_items\":[\n";
        for (UINT index = 0; index < effect_desc.Parameters; ++index) {
            D3DXHANDLE parameter = effect->GetParameter(nullptr, index);
            D3DXPARAMETER_DESC desc = {};
            if (!parameter || FAILED(effect->GetParameterDesc(parameter, &desc)))
                throw std::runtime_error("GetParameterDesc failed");
            if (index) json << ",\n";
            json << "    {\"index\":" << index << ",\"name\":" << json_string(desc.Name)
                 << ",\"semantic\":" << json_string(desc.Semantic)
                 << ",\"semantic_null\":" << (desc.Semantic ? "false" : "true") << ",\"class\":" << desc.Class
                 << ",\"type\":" << desc.Type << ",\"rows\":" << desc.Rows << ",\"columns\":" << desc.Columns
                 << ",\"elements\":" << desc.Elements << ",\"bytes\":" << desc.Bytes << '}';
        }
        json << "\n  ],\n  \"items\":[\n";
        for (UINT ti = 0; ti < effect_desc.Techniques; ++ti) {
            D3DXHANDLE technique = effect->GetTechnique(ti);
            D3DXTECHNIQUE_DESC technique_desc = {};
            if (!technique || FAILED(effect->GetTechniqueDesc(technique, &technique_desc)))
                throw std::runtime_error("GetTechniqueDesc failed");
            if (FAILED(effect->SetTechnique(technique))) throw std::runtime_error("SetTechnique failed");
            if (ti) json << ",\n";
            json << "    {\"index\":" << ti << ",\"name\":" << json_string(technique_desc.Name)
                 << ",\"passes\":" << technique_desc.Passes << ",\"pass_items\":[\n";
            for (UINT pi = 0; pi < technique_desc.Passes; ++pi) {
                D3DXHANDLE pass = effect->GetPass(technique, pi);
                D3DXPASS_DESC pass_desc = {};
                if (!pass || FAILED(effect->GetPassDesc(pass, &pass_desc))) throw std::runtime_error("GetPassDesc failed");
                dump_shader(output_dir, ti, pi, "vs", pass_desc.pVertexShaderFunction);
                dump_shader(output_dir, ti, pi, "ps", pass_desc.pPixelShaderFunction);
                UINT passes = 0;
                if (FAILED(effect->Begin(&passes, 0))) throw std::runtime_error("Begin failed");
                if (FAILED(effect->BeginPass(pi))) throw std::runtime_error("BeginPass failed");
                if (pi) json << ",\n";
                json << "      {\"index\":" << pi << ",\"name\":" << json_string(pass_desc.Name)
                     << ",\"has_vs\":" << (pass_desc.pVertexShaderFunction ? "true" : "false")
                     << ",\"has_ps\":" << (pass_desc.pPixelShaderFunction ? "true" : "false")
                     << ",\"state\":{" << capture_state(device) << "}}";
                if (FAILED(effect->EndPass())) throw std::runtime_error("EndPass failed");
                if (FAILED(effect->End())) throw std::runtime_error("End failed");
            }
            json << "\n    ]}";
        }
        json << "\n  ]\n}\n";
        std::ofstream manifest(output_dir + "/effect-manifest.json");
        manifest << json.str();
        if (!manifest) throw std::runtime_error("cannot write manifest");
        effect->Release();
        device->Release();
        d3d9->Release();
        DestroyWindow(window);
        std::cout << "Exported " << effect_desc.Techniques << " techniques to " << output_dir << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

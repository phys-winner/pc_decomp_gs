#include "d3d9_internal.h"
#include "effect_data.h"
#include <strings.h>

extern uint32_t d3d_constant_generation;

#define D3DXERR_INVALIDDATA 0x88760B59u
#define FX_PARAM_MAGIC 0xA0000000u
#define FX_TECHNIQUE_MAGIC 0x80000000u
#define FX_PASS_MAGIC 0x90000000u
#define FX_ELEMENT_MAGIC 0xB0000000u

typedef struct {
    const fx_param_desc *desc;
    uint8_t *value;
    uint32_t texture;
} fx_parameter;

typedef struct { int parameter, pixel, cls, set, reg, count, rows, columns, elements; } fx_mapping;

typedef struct {
    void_com_object com;
    const fx_effect_def *def;
    uint32_t device, pool;
    int technique, in_begin, in_pass;
    uint32_t state_block;
    fx_parameter *parameters;
    uint32_t (*shader_objects)[2];
    uint32_t (*shader_bytecode)[2];
    uint32_t pass_slots;
    fx_mapping *mappings;
    int mapping_count, mapping_capacity;
} fx_effect;

typedef struct { void_com_object com; } fx_pool;

static const uint8_t IID_EFFECT[16] = {0xB3,0xB4,0xCE,0xF6,0x4C,0x4E,0xDD,0x40,0xB8,0x83,0x8D,0x8D,0xE5,0xEA,0x0C,0xD5};
static const uint8_t IID_BASE[16] = {0xAC,0x18,0x7C,0x01,0x3F,0x10,0x17,0x44,0x8C,0x51,0x6B,0xF6,0xEF,0x1E,0x56,0xBE};
static const uint8_t IID_POOL[16] = {0x04,0xAB,0x37,0x95,0x50,0x32,0x2E,0x41,0x82,0x13,0xFC,0xD2,0xF8,0x67,0x79,0x33};

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t host_guest(const void *p)
{
    if (!p) return 0;
    typedef struct { const void *key; uint32_t value; } string_cache;
    static string_cache cache[512];
    static int count;
    for (int i = 0; i < count; ++i) if (cache[i].key == p) return cache[i].value;
    uint32_t value = void_gstrdup(p ? (const char *)p : "");
    if (count < 512) { cache[count].key = p; cache[count].value = value; ++count; }
    return value;
}
#define FX ((fx_effect *)void_com_host(THIS_))

static void fx_trace(fx_effect *e, const char *name, uint32_t handle)
{
    static int count;
    if (count++ < 120) void_log("[fx] %u %s %08X", e->def->size, name, handle);
}

static uint32_t fx_not_implemented(fx_effect *e, const char *name)
{
    static int count;
    if (count++ < 200) void_log("[fx] unimplemented %s in effect %u", name, e->def->size);
    return E_NOTIMPL_;
}

static uint32_t fx_call(fx_effect *e, int method, uint32_t nargs, const uint32_t *args)
{
    uint32_t all[16];
    all[0] = e->device;
    for (uint32_t i = 0; i < nargs && i < 15; ++i) all[i + 1] = args[i];
    return void_call_guest(MEM32(MEM32(e->device) + 4u * (uint32_t)method), nargs + 1, all, 1);
}

static int fx_parameter_index(fx_effect *e, uint32_t handle)
{
    uint32_t kind = handle & 0xFFFF0000u;
    int index = (kind == FX_PARAM_MAGIC || kind == FX_ELEMENT_MAGIC) ? (int)(handle & 0xFFFFu) - 1 : -1;
    return index >= 0 && (uint32_t)index < e->def->param_count ? index : -1;
}

static uint32_t fx_element_offset(fx_effect *e, uint32_t handle)
{
    if ((handle & 0xFFFF0000u) != FX_ELEMENT_MAGIC) return 0;
    uint32_t element = (handle >> 16) & 0xFFFFu;
    int index = fx_parameter_index(e, handle);
    const fx_param_desc *desc = index >= 0 ? e->parameters[index].desc : NULL;
    return desc && element < desc->elements ? element * desc->rows * desc->columns * 4 : 0;
}

static int fx_technique_index(fx_effect *e, uint32_t handle)
{
    int index = (handle & 0xFFFF0000u) == FX_TECHNIQUE_MAGIC ? (int)(handle & 0xFFFFu) - 1 : -1;
    return index >= 0 && (uint32_t)index < e->def->technique_count ? index : -1;
}

static int fx_pass_indices(fx_effect *e, uint32_t handle, int *pass)
{
    if ((handle & 0xFFFF0000u) != FX_PASS_MAGIC) return -1;
    int technique = (int)((handle >> 16) & 0xFFu);
    *pass = (int)(handle & 0xFFFFu) - 1;
    return technique >= 0 && (uint32_t)technique < e->def->technique_count && *pass >= 0 &&
        (uint32_t)*pass < fx_techniques[e->def->first_technique + technique].pass_count ? technique : -1;
}

static uint32_t fx_param_bytes(const fx_param_desc *desc)
{
    if (desc->type == 5 || desc->type == 15) return 4;
    uint32_t elements = desc->elements ? desc->elements : 1;
    uint32_t size = desc->rows * desc->columns * elements * 4;
    return size ? size : 4;
}

static int fx_ctab(const uint8_t *code, uint32_t size, uint32_t *offset, uint32_t *blob_size)
{
    uint32_t p = 4;
    while (p + 8 <= size) {
        uint32_t token = rd32(code + p);
        if (token == 0x0000FFFFu) break;
        if ((token & 0xFFFFu) != 0xFFFE) break;
        uint32_t length = (token >> 16) & 0x7FFF;
        if (length >= 1 && p + 4 + length * 4 <= size && rd32(code + p + 4) == FOURCC('C','T','A','B')) {
            *offset = p + 8;
            *blob_size = (length - 1) * 4;
            return 1;
        }
        p += 4 + length * 4;
    }
    return 0;
}

static void fx_add_mappings(fx_effect *e, int pixel, const fx_blob *blob)
{
    uint32_t offset, size;
    if (!fx_ctab(blob->data, blob->size, &offset, &size) || size < 28) return;
    const uint8_t *ctab = blob->data + offset;
    uint32_t count = rd32(ctab + 12), infos = rd32(ctab + 16);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *info = ctab + infos + 20 * i;
        const char *name = (const char *)(ctab + rd32(info));
        const uint8_t *type = ctab + rd32(info + 12);
        int parameter = -1;
        for (uint32_t k = 0; k < e->def->param_count; ++k)
            if (!strcmp(fx_params[e->def->first_param + k].name, name)) { parameter = (int)k; break; }
        if (parameter < 0) continue;
        if (e->mapping_count == e->mapping_capacity) {
            e->mapping_capacity = e->mapping_capacity ? e->mapping_capacity * 2 : 64;
            e->mappings = (fx_mapping *)realloc(e->mappings, (size_t)e->mapping_capacity * sizeof *e->mappings);
            if (!e->mappings) return;
        }
        fx_mapping *m = &e->mappings[e->mapping_count++];
        m->parameter = parameter;
        m->pixel = pixel;
        m->cls = rd16(type);
        m->set = rd16(info + 4);
        m->reg = rd16(info + 6);
        m->count = rd16(info + 8);
        m->rows = rd16(type + 4);
        m->columns = rd16(type + 6);
        m->elements = rd16(type + 8) ? rd16(type + 8) : 1;
    }
}

static void fx_release_textures(fx_effect *e)
{
    for (uint32_t i = 0; i < e->def->param_count; ++i) {
        if (e->parameters[i].texture) void_com_release((void_com_object *)void_com_host(e->parameters[i].texture));
        e->parameters[i].texture = 0;
    }
}

static void fx_destroy(void_com_object *object)
{
    fx_effect *e = (fx_effect *)object;
    fx_release_textures(e);
    for (uint32_t i = 0; i < e->def->param_count; ++i) free(e->parameters[i].value);
    free(e->parameters);
    for (uint32_t slot = 0; slot < e->pass_slots; ++slot) for (int pixel = 0; pixel < 2; ++pixel) {
        if (e->shader_objects[slot][pixel]) void_com_release((void_com_object *)void_com_host(e->shader_objects[slot][pixel]));
        if (e->shader_bytecode[slot][pixel]) void_gfree(e->shader_bytecode[slot][pixel]);
    }
    free(e->shader_objects);
    free(e->shader_bytecode);
    free(e->mappings);
    void_com_release((void_com_object *)void_com_host(e->device));
    free(e);
}

COM_METHOD(fx, QueryInterface, 3)
{
    if (void_guid_equal(ARG(1), IID_EFFECT) || void_guid_equal(ARG(1), IID_BASE) || void_guid_equal(ARG(1), IID_IUnknown)) {
        void_com_addref(&FX->com);
        MEM32(ARG(2)) = THIS_;
        return 0;
    }
    MEM32(ARG(2)) = 0;
    return E_NOINTERFACE_;
}
COM_METHOD(fx, AddRef, 1) { return void_com_addref(&FX->com); }
COM_METHOD(fx, Release, 1) { return void_com_release(&FX->com); }

COM_METHOD(fx, GetDesc, 2)
{
    memset(GPTR(ARG(1)), 0, 16);
    MEM32(ARG(1) + 4) = FX->def->param_count;
    MEM32(ARG(1) + 8) = FX->def->technique_count;
    MEM32(ARG(1) + 12) = 0;
    return D3D_OK;
}

COM_METHOD(fx, GetParameterDesc, 3)
{
    int index = fx_parameter_index(FX, ARG(1));
    fx_trace(FX, "param", ARG(1));
    if (index < 0 || !ARG(2)) return D3DERR_INVALIDCALL;
    const fx_param_desc *d = &fx_params[FX->def->first_param + index];
    memset(GPTR(ARG(2)), 0, 44);
    uint32_t out = ARG(2);
    MEM32(out) = host_guest(d->name);
    MEM32(out + 4) = host_guest(d->semantic);
    MEM32(out + 8) = d->cls;
    MEM32(out + 12) = d->type;
    MEM32(out + 16) = d->rows;
    MEM32(out + 20) = d->columns;
    MEM32(out + 24) = (ARG(1) & 0xFFFF0000u) == FX_ELEMENT_MAGIC ? 0 : d->elements;
    MEM32(out + 28) = 0;
    MEM32(out + 32) = 0;
    MEM32(out + 36) = 0;
    MEM32(out + 40) = fx_param_bytes(d);
    static int logged;
    if (logged++ < 40) void_log("[fx] desc %s/%s -> %08X/%08X class %u type %u rows %u cols %u",
        d->name, d->semantic, MEM32(out), MEM32(out + 4), d->cls, d->type, d->rows, d->columns);
    return D3D_OK;
}

COM_METHOD(fx, GetTechniqueDesc, 3)
{
    int index = fx_technique_index(FX, ARG(1));
    if (index < 0 || !ARG(2)) return D3DERR_INVALIDCALL;
    const fx_technique *technique = &fx_techniques[FX->def->first_technique + index];
    MEM32(ARG(2)) = host_guest(technique->name);
    MEM32(ARG(2) + 4) = technique->pass_count;
    MEM32(ARG(2) + 8) = 0;
    return D3D_OK;
}

COM_METHOD(fx, GetPassDesc, 3)
{
    int pass, technique = fx_pass_indices(FX, ARG(1), &pass);
    if (technique < 0 || !ARG(2)) return D3DERR_INVALIDCALL;
    const fx_technique *t = &fx_techniques[FX->def->first_technique + technique];
    int slot = (int)t->first_pass + pass;
    MEM32(ARG(2)) = host_guest(t->passes[pass].name);
    MEM32(ARG(2) + 4) = 0;
    MEM32(ARG(2) + 8) = FX->shader_bytecode[slot][0];
    MEM32(ARG(2) + 12) = FX->shader_bytecode[slot][1];
    return D3D_OK;
}
COM_METHOD(fx, GetFunctionDesc, 3) { return fx_not_implemented(FX, __func__); }

COM_METHOD(fx, GetParameter, 3)
{
    int index = ARG(1) ? -1 : (int)ARG(2);
    return index >= 0 && (uint32_t)index < FX->def->param_count ? FX_PARAM_MAGIC + (uint32_t)index + 1 : 0;
}

COM_METHOD(fx, GetParameterByName, 3)
{
    if (ARG(1) || !ARG(2)) return 0;
    fx_trace(FX, "by-name", ARG(2));
    if (!void_vm_readable(ARG(2), 1)) {
        void_log("[fx] invalid parameter name pointer %08X caller %08X esp %08X ebp %08X this %08X", ARG(2),
            g_icall_from[(g_icall_trace_idx - 1) & (ICALL_TRACE_SIZE - 1)], g_esp, g_ebp, THIS_);
        return 0;
    }
    const char *name = (const char *)GPTR(ARG(2));
    for (uint32_t i = 0; i < FX->def->param_count; ++i)
        if (!strcmp(fx_params[FX->def->first_param + i].name, name)) return FX_PARAM_MAGIC + i + 1;
    return 0;
}

COM_METHOD(fx, GetParameterBySemantic, 3)
{
    if (ARG(1) || !ARG(2)) return 0;
    const char *semantic = (const char *)GPTR(ARG(2));
    for (uint32_t i = 0; i < FX->def->param_count; ++i)
        if (!strcasecmp(fx_params[FX->def->first_param + i].semantic, semantic)) return FX_PARAM_MAGIC + i + 1;
    return 0;
}
COM_METHOD(fx, GetParameterElement, 3)
{
    int index = fx_parameter_index(FX, ARG(1));
    if (index < 0) return 0;
    const fx_param_desc *desc = FX->parameters[index].desc;
    return ARG(2) < desc->elements ? FX_ELEMENT_MAGIC + (ARG(2) << 16) + (uint32_t)index + 1 : 0;
}
COM_METHOD(fx, GetTechnique, 2) { return ARG(1) < FX->def->technique_count ? FX_TECHNIQUE_MAGIC + ARG(1) + 1 : 0; }

COM_METHOD(fx, GetTechniqueByName, 2)
{
    const char *name = (const char *)GPTR(ARG(1));
    for (uint32_t i = 0; i < FX->def->technique_count; ++i)
        if (!strcmp(fx_techniques[FX->def->first_technique + i].name, name)) return FX_TECHNIQUE_MAGIC + i + 1;
    return 0;
}

COM_METHOD(fx, GetPass, 3)
{
    int technique = fx_technique_index(FX, ARG(1));
    return technique >= 0 && ARG(2) < fx_techniques[FX->def->first_technique + technique].pass_count ?
        FX_PASS_MAGIC + ((uint32_t)technique << 16) + ARG(2) + 1 : 0;
}

COM_METHOD(fx, GetPassByName, 3)
{
    int technique = fx_technique_index(FX, ARG(1));
    const char *name = (const char *)GPTR(ARG(2));
    if (technique >= 0) {
        const fx_technique *t = &fx_techniques[FX->def->first_technique + technique];
        for (uint32_t i = 0; i < t->pass_count; ++i)
            if (!strcmp(t->passes[i].name, name)) return FX_PASS_MAGIC + ((uint32_t)technique << 16) + i + 1;
    }
    return 0;
}
COM_METHOD(fx, GetFunction, 2) { return 0; }
COM_METHOD(fx, GetFunctionByName, 2) { return 0; }
COM_METHOD(fx, GetAnnotation, 3) { return 0; }
COM_METHOD(fx, GetAnnotationByName, 3) { return 0; }

static uint32_t fx_set_value(fx_effect *e, uint32_t handle, const void *data, uint32_t bytes)
{
    int index = fx_parameter_index(e, handle);
    fx_trace(e, "set", handle);
    if (index < 0 || !data) return D3DERR_INVALIDCALL;
    uint32_t offset = fx_element_offset(e, handle);
    if (offset + bytes > fx_param_bytes(e->parameters[index].desc)) return D3DERR_INVALIDCALL;
    if ((e->parameters[index].desc->type == 5 || e->parameters[index].desc->type == 10) && bytes >= 4)
        VLOG("[fx] SetValue texture %u %s = %08X", e->def->size, e->parameters[index].desc->name,
            *(const uint32_t *)data);
    memcpy(e->parameters[index].value + offset, data, bytes);
    return D3D_OK;
}

COM_METHOD(fx, SetValue, 4) { return fx_set_value(FX, ARG(1), GPTR(ARG(2)), ARG(3)); }
COM_METHOD(fx, GetValue, 4)
{
    int index = fx_parameter_index(FX, ARG(1));
    fx_trace(FX, "param", ARG(1));
    if (index < 0 || !ARG(2)) return D3DERR_INVALIDCALL;
    uint32_t offset = fx_element_offset(FX, ARG(1));
    if (offset + ARG(3) > fx_param_bytes(FX->parameters[index].desc)) return D3DERR_INVALIDCALL;
    memcpy(GPTR(ARG(2)), FX->parameters[index].value + offset, ARG(3));
    return D3D_OK;
}
COM_METHOD(fx, SetBool, 3) { uint32_t value = ARG(2); return fx_set_value(FX, ARG(1), &value, 4); }
COM_METHOD(fx, GetBool, 3)
{
    int index = fx_parameter_index(FX, ARG(1));
    fx_trace(FX, "param", ARG(1));
    if (index < 0 || !ARG(2)) return D3DERR_INVALIDCALL;
    uint32_t offset = fx_element_offset(FX, ARG(1));
    if (offset + 4 > fx_param_bytes(FX->parameters[index].desc)) return D3DERR_INVALIDCALL;
    MEM32(ARG(2)) = *(const uint32_t *)(FX->parameters[index].value + offset);
    return D3D_OK;
}
COM_METHOD(fx, SetBoolArray, 4) { return fx_set_value(FX, ARG(1), GPTR(ARG(2)), 4 * ARG(3)); }
COM_METHOD(fx, GetBoolArray, 4) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetInt, 3) { uint32_t value = ARG(2); return fx_set_value(FX, ARG(1), &value, 4); }
COM_METHOD(fx, GetInt, 3) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetIntArray, 4) { return fx_set_value(FX, ARG(1), GPTR(ARG(2)), 4 * ARG(3)); }
COM_METHOD(fx, GetIntArray, 4) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetFloat, 3) { uint32_t value = ARG(2); return fx_set_value(FX, ARG(1), &value, 4); }
COM_METHOD(fx, GetFloat, 3) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetFloatArray, 4) { return fx_set_value(FX, ARG(1), GPTR(ARG(2)), 4 * ARG(3)); }
COM_METHOD(fx, GetFloatArray, 4) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetVector, 3) { return fx_set_value(FX, ARG(1), GPTR(ARG(2)), 16); }
COM_METHOD(fx, GetVector, 3) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetVectorArray, 4) { return fx_set_value(FX, ARG(1), GPTR(ARG(2)), 16 * ARG(3)); }
COM_METHOD(fx, GetVectorArray, 4) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetMatrix, 3) { return fx_set_value(FX, ARG(1), GPTR(ARG(2)), 64); }
COM_METHOD(fx, GetMatrix, 3) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetMatrixArray, 4) { return fx_set_value(FX, ARG(1), GPTR(ARG(2)), 64 * ARG(3)); }
COM_METHOD(fx, GetMatrixArray, 4) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetMatrixPointerArray, 4) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, GetMatrixPointerArray, 4) { return fx_not_implemented(FX, __func__); }

static uint32_t fx_set_transpose(fx_effect *e, uint32_t handle, const float *source)
{
    float transposed[16];
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) transposed[row * 4 + col] = source[col * 4 + row];
    return fx_set_value(e, handle, transposed, sizeof transposed);
}
COM_METHOD(fx, SetMatrixTranspose, 3) { return fx_set_transpose(FX, ARG(1), (const float *)GPTR(ARG(2))); }
COM_METHOD(fx, GetMatrixTranspose, 3) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetMatrixTransposeArray, 4) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, GetMatrixTransposeArray, 4) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetMatrixTransposePointerArray, 4) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, GetMatrixTransposePointerArray, 4) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetString, 3) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, GetString, 3) { return fx_not_implemented(FX, __func__); }

COM_METHOD(fx, SetTexture, 3)
{
    fx_effect *e = FX;
    int index = fx_parameter_index(e, ARG(1));
    if (index < 0) return D3DERR_INVALIDCALL;
    const fx_param_desc *desc = e->parameters[index].desc;
    if (desc->type != 5 && desc->type != 10) return D3DERR_INVALIDCALL;
    if (ARG(2)) {
        d3d_texture *t = (d3d_texture *)void_com_host(ARG(2));
        VLOG("[fx] SetTexture %u %s = %08X %ux%u format %u", e->def->size, desc->name, ARG(2),
            t->width, t->height, t->format);
    }
    for (uint32_t i = 0; i < e->def->param_count; ++i) {
        const fx_param_desc *d = e->parameters[i].desc;
        int related = (int)i == index || (d->type == 10 && !strcmp(d->semantic, desc->name)) ||
            (desc->type == 10 && d->type == 5 && !strcmp(d->name, desc->semantic));
        if (related && e->parameters[i].texture) {
            void_com_release((void_com_object *)void_com_host(e->parameters[i].texture));
            e->parameters[i].texture = 0;
        }
    }
    if (ARG(2)) for (uint32_t i = 0; i < e->def->param_count; ++i) {
        const fx_param_desc *d = e->parameters[i].desc;
        int related = (int)i == index || (d->type == 10 && !strcmp(d->semantic, desc->name)) ||
            (desc->type == 10 && d->type == 5 && !strcmp(d->name, desc->semantic));
        if (related) {
            void_com_addref((void_com_object *)void_com_host(ARG(2)));
            e->parameters[i].texture = ARG(2);
        }
    }
    return D3D_OK;
}

COM_METHOD(fx, GetTexture, 3)
{
    int index = fx_parameter_index(FX, ARG(1));
    fx_trace(FX, "param", ARG(1));
    if (index < 0) return D3DERR_INVALIDCALL;
    uint32_t texture = FX->parameters[index].texture;
    VLOG("[fx] GetTexture %u %s = %08X", FX->def->size, FX->parameters[index].desc->name, texture);
    if (texture) void_com_addref((void_com_object *)void_com_host(texture));
    MEM32(ARG(2)) = texture;
    return D3D_OK;
}

COM_METHOD(fx, GetPixelShader, 3)
{
    fx_effect *e = FX;
    int index = fx_parameter_index(e, ARG(1));
    const char *name = NULL;
    if (index < 0 && ARG(1) && void_vm_readable(ARG(1), 1)) {
        name = (const char *)GPTR(ARG(1));
        for (uint32_t i = 0; i < e->def->param_count; ++i)
            if (!strcmp(fx_params[e->def->first_param + i].name, name)) { index = (int)i; break; }
    }
    if (getenv("VOID_TRACE_EFFECTS")) {
        static int logged;
        if (logged++ < 64) void_log("[fx] GetPixelShader effect %u name %s index %d handle %08X", e->def->size,
            name ? name : "(handle)", index, ARG(1));
    }
    if (e->def->size != 26640 || index < 8 || index > 15) return D3DERR_INVALIDCALL;
    int technique = index < 12, pass = index & 3;
    uint32_t shader = e->shader_objects[technique * 6 + pass][1];
    if (shader) void_com_addref((void_com_object *)void_com_host(shader));
    MEM32(ARG(2)) = shader;
    return D3D_OK;
}
COM_METHOD(fx, GetVertexShader, 3)
{
    fx_effect *e = FX;
    int pass, technique = fx_pass_indices(e, ARG(1), &pass);
    if (technique < 0) return D3DERR_INVALIDCALL;
    int slot = (int)fx_techniques[e->def->first_technique + technique].first_pass + pass;
    uint32_t shader = e->shader_objects[slot][0];
    if (getenv("VOID_TRACE_EFFECTS")) {
        static int logged;
        if (logged++ < 64) void_log("[fx] GetVertexShader effect %u handle %08X slot %d shader %08X",
            e->def->size, ARG(1), slot, shader);
    }
    if (shader) void_com_addref((void_com_object *)void_com_host(shader));
    MEM32(ARG(2)) = shader;
    return D3D_OK;
}
COM_METHOD(fx, SetArrayRange, 4) { return D3D_OK; }
COM_METHOD(fx, GetPool, 2) { MEM32(ARG(1)) = FX->pool; return D3D_OK; }

COM_METHOD(fx, SetTechnique, 2)
{
    int index = fx_technique_index(FX, ARG(1));
    if (index < 0) return D3DERR_INVALIDCALL;
    FX->technique = index;
    return D3D_OK;
}
COM_METHOD(fx, GetCurrentTechnique, 1) { return FX_TECHNIQUE_MAGIC + (uint32_t)FX->technique + 1; }
COM_METHOD(fx, ValidateTechnique, 2) { return fx_technique_index(FX, ARG(1)) < 0 ? D3DERR_INVALIDCALL : D3D_OK; }
COM_METHOD(fx, FindNextValidTechnique, 3)
{
    fx_effect *e = FX;
    int current = fx_technique_index(e, ARG(1));
    int next = current < 0 ? 0 : current + 1;
    if (ARG(2)) MEM32(ARG(2)) = (uint32_t)next < e->def->technique_count ? FX_TECHNIQUE_MAGIC + (uint32_t)next + 1 : 0;
    return (uint32_t)next < e->def->technique_count ? D3D_OK : 1;
}
COM_METHOD(fx, IsParameterUsed, 3) { return fx_parameter_index(FX, ARG(1)) >= 0; }

static void fx_apply_constants(fx_effect *e)
{
    d3d_device *device = (d3d_device *)void_com_host(e->device);
    for (int i = 0; i < e->mapping_count; ++i) {
        const fx_mapping *m = &e->mappings[i];
        const fx_parameter *p = &e->parameters[m->parameter];
        if (m->set == 3) {
            uint32_t args[2] = {(uint32_t)m->reg, p->texture};
            fx_call(e, 65, 2, args);
            continue;
        }
        const float *source = (const float *)p->value;
        if (m->cls == 2 && m->elements > 1 && getenv("VOID_TRACE_EFFECTS")) {
            static int logged;
            if (logged++ < 48) void_log("[fx] matrix effect %u %s pixel %d reg %d count %d: %g %g %g %g / %g %g %g %g",
                e->def->size, p->desc->name, m->pixel, m->reg, m->count,
                source[0], source[1], source[2], source[3], source[12], source[13], source[14], source[15]);
        }
        float values[4 * 256];
        memset(values, 0, sizeof values);
        uint32_t registers = m->count < 256 ? (uint32_t)m->count : 256;
        uint32_t per = (uint32_t)(m->rows * m->columns);
        for (int element = 0; element < m->elements; ++element) {
            const float *matrix = source + element * per;
            uint32_t regs_per = m->elements ? registers / (uint32_t)m->elements : registers;
            for (uint32_t reg = 0; reg < regs_per; ++reg) for (int component = 0; component < 4; ++component) {
                float value = 0;
                if (m->cls == 3) {
                    if ((int)reg < m->columns && component < m->rows) value = matrix[component * m->columns + reg];
                } else if (m->cls == 2) {
                    if ((int)reg < m->rows && component < m->columns) value = matrix[reg * m->columns + component];
                } else if (reg == 0 && component < m->columns) {
                    value = matrix[component];
                }
                values[4 * (element * regs_per + reg) + component] = value;
            }
        }
        if (m->pixel) {
            if (m->set == 2 && m->reg + registers <= 224) memcpy(device->st.ps_f[m->reg], values, registers * 16);
            else if (m->set == 1 && m->reg + registers <= 16) for (uint32_t k = 0; k < registers * 4; ++k) device->st.ps_i[m->reg + k / 4][k % 4] = (int32_t)values[k];
            else if (m->set == 0 && m->reg < 16) for (uint32_t k = 0; k < registers; ++k) device->st.ps_b[m->reg + k] = values[4 * k] != 0;
        } else {
            if (m->set == 2 && m->reg + registers <= 256) memcpy(device->st.vs_f[m->reg], values, registers * 16);
            else if (m->set == 1 && m->reg + registers <= 16) for (uint32_t k = 0; k < registers * 4; ++k) device->st.vs_i[m->reg + k / 4][k % 4] = (int32_t)values[k];
            else if (m->set == 0 && m->reg < 16) for (uint32_t k = 0; k < registers; ++k) device->st.vs_b[m->reg + k] = values[4 * k] != 0;
        }
    }
    ++d3d_constant_generation;
    device->constants_dirty = 1;
}

COM_METHOD(fx, Begin, 3)
{
    fx_effect *e = FX;
    if (e->in_begin) return D3DERR_INVALIDCALL;
    if (ARG(1)) MEM32(ARG(1)) = fx_techniques[e->def->first_technique + e->technique].pass_count;
    if (!(ARG(2) & 1u)) {
        uint32_t out = void_galloc(4), args[2] = {0, out};
        if (fx_call(e, 59, 2, args)) {
            void_gfree(out);
            return D3DERR_INVALIDCALL;
        }
        e->state_block = MEM32(out);
        void_gfree(out);
        fx_call(e, 60, 0, NULL);
    }
    e->in_begin = 1;
    return D3D_OK;
}

COM_METHOD(fx, BeginPass, 2)
{
    fx_effect *e = FX;
    fx_trace(e, "begin-pass", ARG(1));
    int pass = (int)ARG(1);
    if (!e->in_begin || e->in_pass || pass < 0) return D3DERR_INVALIDCALL;
    const fx_technique *technique = &fx_techniques[e->def->first_technique + e->technique];
    if ((uint32_t)pass >= technique->pass_count) return D3DERR_INVALIDCALL;
    const fx_pass *definition = &technique->passes[pass];
    for (uint32_t i = 0; i < definition->state_count; ++i) {
        const fx_state_change *change = &definition->states[i];
        uint32_t args[2] = {change->first, change->value};
        if (change->kind == 0) fx_call(e, 57, 2, args);
        else if (change->kind == 1) { uint32_t state[3] = {change->first, change->second, change->value}; fx_call(e, 67, 3, state); }
        else if (change->kind == 2) { uint32_t state[3] = {change->first, change->second, change->value}; fx_call(e, 69, 3, state); }
        else if (change->kind == 3) fx_call(e, 89, 1, &change->value);
    }
    int slot = (int)technique->first_pass + pass;
    uint32_t vs = e->shader_objects[slot][0], ps = e->shader_objects[slot][1];
    fx_call(e, 92, 1, &vs);
    fx_call(e, 107, 1, &ps);
    e->in_pass = 1;
    fx_apply_constants(e);
    return D3D_OK;
}
COM_METHOD(fx, CommitChanges, 1) { fx_apply_constants(FX); return D3D_OK; }
COM_METHOD(fx, EndPass, 1) { if (!FX->in_pass) return D3DERR_INVALIDCALL; FX->in_pass = 0; return D3D_OK; }

COM_METHOD(fx, End, 1)
{
    fx_effect *e = FX;
    if (!e->in_begin || e->in_pass) return D3DERR_INVALIDCALL;
    e->in_begin = 0;
    if (e->state_block) {
        uint32_t out = void_galloc(4), args[1] = {out};
        fx_call(e, 61, 1, args);
        uint32_t recorded = MEM32(out);
        uint32_t block = e->state_block;
        e->state_block = 0;
        uint32_t apply = MEM32(MEM32(block) + 5 * 4);
        uint32_t release = MEM32(MEM32(block) + 2 * 4);
        void_call_guest(apply, 1, &block, 1);
        void_call_guest(release, 1, &block, 1);
        if (recorded) {
            uint32_t release_recorded = MEM32(MEM32(recorded) + 2 * 4);
            void_call_guest(release_recorded, 1, &recorded, 1);
        }
        void_gfree(out);
    }
    return D3D_OK;
}
COM_METHOD(fx, GetDevice, 2) { void_com_addref((void_com_object *)void_com_host(FX->device)); MEM32(ARG(1)) = FX->device; return D3D_OK; }
COM_METHOD(fx, OnLostDevice, 1) { return D3D_OK; }
COM_METHOD(fx, OnResetDevice, 1) { return D3D_OK; }
COM_METHOD(fx, SetStateManager, 2) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, GetStateManager, 2) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, BeginParameterBlock, 1) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, EndParameterBlock, 1) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, ApplyParameterBlock, 2) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, DeleteParameterBlock, 2) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, CloneEffect, 3) { return fx_not_implemented(FX, __func__); }
COM_METHOD(fx, SetRawValue, 5) { return fx_not_implemented(FX, __func__); }

static const void_fn effect_methods[] = {
    fx_QueryInterface, fx_AddRef, fx_Release, fx_GetDesc, fx_GetParameterDesc, fx_GetTechniqueDesc,
    fx_GetPassDesc, fx_GetFunctionDesc, fx_GetParameter, fx_GetParameterByName, fx_GetParameterBySemantic,
    fx_GetParameterElement, fx_GetTechnique, fx_GetTechniqueByName, fx_GetPass, fx_GetPassByName,
    fx_GetFunction, fx_GetFunctionByName, fx_GetAnnotation, fx_GetAnnotationByName, fx_SetValue, fx_GetValue,
    fx_SetBool, fx_GetBool, fx_SetBoolArray, fx_GetBoolArray, fx_SetInt, fx_GetInt, fx_SetIntArray, fx_GetIntArray,
    fx_SetFloat, fx_GetFloat, fx_SetFloatArray, fx_GetFloatArray, fx_SetVector, fx_GetVector, fx_SetVectorArray,
    fx_GetVectorArray, fx_SetMatrix, fx_GetMatrix, fx_SetMatrixArray, fx_GetMatrixArray, fx_SetMatrixPointerArray,
    fx_GetMatrixPointerArray, fx_SetMatrixTranspose, fx_GetMatrixTranspose, fx_SetMatrixTransposeArray,
    fx_GetMatrixTransposeArray, fx_SetMatrixTransposePointerArray, fx_GetMatrixTransposePointerArray,
    fx_SetString, fx_GetString, fx_SetTexture, fx_GetTexture, fx_GetPixelShader, fx_GetVertexShader,
    fx_SetArrayRange, fx_GetPool, fx_SetTechnique, fx_GetCurrentTechnique, fx_ValidateTechnique,
    fx_FindNextValidTechnique, fx_IsParameterUsed, fx_Begin, fx_BeginPass, fx_CommitChanges, fx_EndPass,
    fx_End, fx_GetDevice, fx_OnLostDevice, fx_OnResetDevice, fx_SetStateManager, fx_GetStateManager,
    fx_BeginParameterBlock, fx_EndParameterBlock, fx_ApplyParameterBlock, fx_DeleteParameterBlock,
    fx_CloneEffect, fx_SetRawValue,
};
static void_com_class effect_class = { "ID3DXEffect", sizeof effect_methods / sizeof effect_methods[0], effect_methods, 0 };

COM_METHOD(pool, QueryInterface, 3)
{
    if (void_guid_equal(ARG(1), IID_POOL) || void_guid_equal(ARG(1), IID_IUnknown)) {
        fx_pool *p = (fx_pool *)void_com_host(THIS_);
        void_com_addref(&p->com);
        MEM32(ARG(2)) = THIS_;
        return 0;
    }
    MEM32(ARG(2)) = 0;
    return E_NOINTERFACE_;
}
COM_METHOD(pool, AddRef, 1) { fx_pool *p = (fx_pool *)void_com_host(THIS_); return void_com_addref(&p->com); }
COM_METHOD(pool, Release, 1) { fx_pool *p = (fx_pool *)void_com_host(THIS_); return void_com_release(&p->com); }
static const void_fn pool_methods[] = { pool_QueryInterface, pool_AddRef, pool_Release };
static void_com_class pool_class = { "ID3DXEffectPool", 3, pool_methods, 0 };

WINAPI_EXPORT("d3dx9_43.dll", D3DXCreateEffectPool, 1)
{
    fx_pool *pool = (fx_pool *)calloc(1, sizeof *pool);
    MEM32(ARG(0)) = void_com_wrap(&pool->com, &pool_class, 0);
    return D3D_OK;
}

WINAPI_EXPORT("d3dx9_43.dll", D3DXCreateEffect, 9)
{
    const fx_effect_def *def = NULL;
    for (uint32_t i = 0; i < fx_effect_def_count; ++i) if (fx_effects[i].size == ARG(2)) { def = &fx_effects[i]; break; }
    if (!ARG(0) || !ARG(1) || !def || rd32(GPTR(ARG(1))) != 0xFEFF0901u || !ARG(7)) {
        void_log("[d3dx] unsupported compiled effect %08X size %u", ARG(1), ARG(2));
        return D3DXERR_INVALIDDATA;
    }
    fx_effect *e = (fx_effect *)calloc(1, sizeof *e);
    if (!e) return E_OUTOFMEMORY_;
    e->def = def;
    e->device = ARG(0);
    e->pool = ARG(6);
    void_com_addref((void_com_object *)void_com_host(e->device));
    e->parameters = (fx_parameter *)calloc(def->param_count, sizeof *e->parameters);
    for (uint32_t i = 0; i < def->technique_count; ++i) {
        const fx_technique *t = &fx_techniques[def->first_technique + i];
        if (t->first_pass + t->pass_count > e->pass_slots) e->pass_slots = t->first_pass + t->pass_count;
    }
    e->shader_objects = (uint32_t (*)[2])calloc(e->pass_slots, sizeof *e->shader_objects);
    e->shader_bytecode = (uint32_t (*)[2])calloc(e->pass_slots, sizeof *e->shader_bytecode);
    for (uint32_t i = 0; i < def->param_count; ++i) {
        e->parameters[i].desc = &fx_params[def->first_param + i];
        e->parameters[i].value = (uint8_t *)calloc(1, fx_param_bytes(e->parameters[i].desc));
    }
    for (uint32_t technique = 0; technique < def->technique_count; ++technique) {
        const fx_technique *t = &fx_techniques[def->first_technique + technique];
        for (uint32_t pass = 0; pass < t->pass_count; ++pass) {
            int slot = (int)t->first_pass + (int)pass;
            int indices[2] = {t->passes[pass].vs, t->passes[pass].ps};
            for (int pixel = 0; pixel < 2; ++pixel) {
                int blob = indices[pixel];
                if (blob < 0) continue;
                uint32_t code = void_galloc(fx_blobs[blob].size);
                memcpy(GPTR(code), fx_blobs[blob].data, fx_blobs[blob].size);
                e->shader_bytecode[slot][pixel] = code;
                uint32_t out = void_galloc(4), args[2] = {code, out};
                if (fx_call(e, pixel ? 106 : 91, 2, args)) {
                    void_log("[d3dx] cannot create %s shader for effect %u technique %u pass %u", pixel ? "pixel" : "vertex", def->size, technique, pass);
                    void_gfree(out);
                    return D3DERR_INVALIDCALL;
                }
                e->shader_objects[slot][pixel] = MEM32(out);
                void_gfree(out);
            }
        }
    }
    for (uint32_t i = 0; i < fx_blob_count; ++i)
        fx_add_mappings(e, (rd32(fx_blobs[i].data) & 0xFFFF0000u) == 0xFFFF0000u, &fx_blobs[i]);
    MEM32(ARG(7)) = void_com_wrap(&e->com, &effect_class, 0);
    e->com.destroy = fx_destroy;
    void d3d_gl_release_current(void);
    d3d_gl_release_current();
    if (ARG(8)) MEM32(ARG(8)) = 0;
    void_log("[d3dx] compiled effect %u: %u techniques, %u parameters, %d mappings", def->size,
        def->technique_count, def->param_count, e->mapping_count);
    return D3D_OK;
}

WINAPI_EXPORT("d3dx9_43.dll", D3DXGetShaderSamplers, 3)
{
    if (!ARG(0) || !ARG(2)) return D3DERR_INVALIDCALL;
    uint32_t offset, size;
    if (!fx_ctab(GPTR(ARG(0)), d3d_shader_bytecode_size(ARG(0)), &offset, &size)) {
        MEM32(ARG(2)) = 0;
        return D3D_OK;
    }
    const uint8_t *ctab = GPTR(ARG(0)) + offset;
    uint32_t count = rd32(ctab + 12), infos = rd32(ctab + 16), written = 0, capacity = MEM32(ARG(2));
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *info = ctab + infos + 20 * i;
        if (rd16(info + 4) != 3) continue;
        if (ARG(1) && written < capacity) MEM32(ARG(1) + 4 * written) = ARG(0) + offset + rd32(info);
        ++written;
    }
    MEM32(ARG(2)) = written;
    return written <= capacity ? D3D_OK : D3DERR_MOREDATA;
}

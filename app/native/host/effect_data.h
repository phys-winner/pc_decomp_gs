#ifndef GRIEF_EFFECT_DATA_H
#define GRIEF_EFFECT_DATA_H
#include <stdint.h>

typedef struct { const uint8_t *data; uint32_t size; } fx_blob;
typedef struct { uint8_t kind; uint16_t first, second; uint32_t value; } fx_state_change;
typedef struct { const char *name, *semantic; uint16_t cls, type, rows, columns, elements; } fx_param_desc;
typedef struct { const char *name; int vs, ps; const fx_state_change *states; uint32_t state_count; } fx_pass;
typedef struct { const char *name; const fx_pass *passes; uint32_t first_pass, pass_count; } fx_technique;
typedef struct { uint32_t size, first_technique, technique_count, first_param, param_count, pass_count; } fx_effect_def;
extern const fx_blob fx_blobs[];
extern const uint32_t fx_blob_count;
extern const fx_param_desc fx_params[];
extern const uint32_t fx_param_count;
extern const fx_technique fx_techniques[];
extern const uint32_t fx_technique_count;
extern const fx_effect_def fx_effects[];
extern const uint32_t fx_effect_def_count;
#endif

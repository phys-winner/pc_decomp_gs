#ifndef GRIEF_TV_KERNEL_H
#define GRIEF_TV_KERNEL_H
#include <stdint.h>
#include <string.h>
#include <math.h>

typedef struct { uint32_t type, bits; } tv_value;
typedef struct { tv_value *values; uint32_t count; } tv_array;
typedef struct {
    tv_array chips[4], source[4], bars, frames;
    uint32_t main_layer, bar_layer, capture, light, bar_image, bar_light;
    float camera_x, camera_y, main_x, main_y, bar_x, bar_y;
    int shader, alpha_blend, subtract_blend;
} tv_scene;
enum { TV_CAPTURE, TV_BLEND, TV_STRETCH, TV_BLIT };
typedef struct {
    int operation;
    uint32_t layer, resource;
    int32_t x, y, width, height, sx, sy, sw, sh, blend;
    float alpha;
} tv_command;
typedef struct {
    void *context;
    void (*emit)(void *, const tv_command *);
    int32_t (*random)(void *);
    int (*active)(void *);
} tv_callbacks;

static float tv_number(tv_value value)
{
    float result;
    if (value.type == 0x05000002u) return (float)(int32_t)value.bits;
    memcpy(&result, &value.bits, 4);
    return result;
}

/* The original maps keep fixed integer chip positions; unusual/modded inputs use the original Update. */
static int tv_scene_valid(const tv_scene *s)
{
    uint32_t total = 0;
    for (unsigned group = 0; group < 4; ++group) {
        const tv_array *a = &s->chips[group];
        if ((a->count & 1) || a->count > 16384) return 0;
        total += a->count / 2;
        for (uint32_t i = 0; i < a->count; ++i)
            if (a->values[i].type != 0x05000002u ||
                (int32_t)a->values[i].bits < -1048576 || (int32_t)a->values[i].bits > 1048576) return 0;
    }
    for (unsigned group = 0; group < 4; ++group) {
        const tv_array *a = &s->source[group];
        if (a->count < total) return 0;
        for (uint32_t i = 0; i < total; ++i) {
            if (a->values[i].type != 0x05000002u && a->values[i].type != 0x05000004u) return 0;
            float n = tv_number(a->values[i]);
            if (!isfinite(n) || n < -1048576.0f || n > 1048576.0f) return 0;
        }
    }
    if ((s->bars.count & 1) || s->bars.count > 16384 || s->frames.count < s->bars.count / 2) return 0;
    for (uint32_t i = 0; i < s->bars.count; ++i)
        if (s->bars.values[i].type != 0x05000002u ||
            (int32_t)s->bars.values[i].bits < -1048576 || (int32_t)s->bars.values[i].bits > 1048576) return 0;
    for (uint32_t i = 0; i < s->bars.count / 2; ++i)
        if (s->frames.values[i].type != 0x05000002u || s->frames.values[i].bits > 0x7ffffffdu) return 0;
    const tv_array *readonly[]={&s->chips[0],&s->chips[1],&s->chips[2],&s->chips[3],
        &s->source[0],&s->source[1],&s->source[2],&s->source[3],&s->bars};
    uintptr_t begin=(uintptr_t)s->frames.values,end=begin+s->frames.count*sizeof(tv_value);
    for (unsigned i=0;i<sizeof readonly/sizeof readonly[0];++i) {
        uintptr_t a=(uintptr_t)readonly[i]->values,b=a+readonly[i]->count*sizeof(tv_value);
        if (begin<end && a<b && begin<b && a<end) return 0;
    }
    const float positions[] = {s->camera_x,s->camera_y,s->main_x,s->main_y,s->bar_x,s->bar_y};
    for (unsigned i = 0; i < sizeof positions / sizeof positions[0]; ++i)
        if (!isfinite(positions[i]) || fabsf(positions[i]) > 1048576.0f) return 0;
    return 1;
}

static int tv_visible(int32_t x, int32_t y, float dx, float dy, const tv_scene *s)
{
    float xx = (float)x + dx;
    xx = xx - s->camera_x;
    if (fabsf(xx) > 900.0f) return 0;
    float yy = (float)y + dy;
    yy = yy - s->camera_y;
    return !(fabsf(yy) > 700.0f);
}

static void tv_frame(tv_scene *s, const tv_callbacks *cb)
{
    tv_command c = {.operation=TV_CAPTURE, .resource=s->capture};
    cb->emit(cb->context, &c);
    c = (tv_command){.operation=TV_BLEND, .layer=s->main_layer, .blend=s->alpha_blend, .alpha=1.0f};
    cb->emit(cb->context, &c);
    uint32_t source = 0;
    for (unsigned group = 0; group < 4; ++group) {
        tv_array *a = &s->chips[group];
        for (uint32_t i = 0; i < a->count; i += 2) {
            int32_t x = (int32_t)a->values[i].bits, y = (int32_t)a->values[i+1].bits;
            if (!tv_visible(x,y,s->main_x,s->main_y,s)) continue;
            c = (tv_command){.operation=TV_STRETCH,.layer=s->main_layer,.resource=s->capture,
                .x=x+15,.y=y+18,.width=98,.height=82,.sw=800,.sh=600};
            if (group) {
                c.sx=(int32_t)tv_number(s->source[0].values[source]);
                c.sy=(int32_t)tv_number(s->source[1].values[source]);
                c.sw=(int32_t)tv_number(s->source[2].values[source]);
                c.sh=(int32_t)tv_number(s->source[3].values[source]);
            }
            cb->emit(cb->context,&c);
            float alpha = (float)(cb->random(cb->context) % 100) / 200.0f;
            alpha = alpha + (group ? 0.4f : 0.25f);
            c.operation=TV_BLIT; c.resource=s->light; c.sx=c.sy=c.sw=c.sh=0;
            c.blend=s->subtract_blend; c.alpha=alpha;
            cb->emit(cb->context,&c);
            ++source;
        }
    }
    if (s->shader == 1 && !cb->active(cb->context)) return;
    for (uint32_t i = 0; i < s->bars.count; i += 2) {
        int32_t x=(int32_t)s->bars.values[i].bits, y=(int32_t)s->bars.values[i+1].bits;
        if (!tv_visible(x,y,s->bar_x,s->bar_y,s)) continue;
        int32_t frame=(int32_t)s->frames.values[i/2].bits++;
        int32_t animation=(frame % 12)/2;
        c=(tv_command){.operation=TV_BLEND,.layer=s->bar_layer,.blend=s->subtract_blend,.alpha=0.3f};
        cb->emit(cb->context,&c);
        if (animation >= 0 && animation <= 5) {
            static const int columns[] = {0,1,2,3,2,1};
            c=(tv_command){.operation=TV_STRETCH,.layer=s->bar_layer,.resource=s->bar_image,
                .x=x-32,.y=y-32,.width=192,.height=192,.sx=128*columns[animation],.sw=128,.sh=128};
            cb->emit(cb->context,&c);
        }
        float alpha=(float)(cb->random(cb->context)%100)/100.0f;
        alpha=alpha*0.075f;
        alpha=alpha+0.01f;
        c=(tv_command){.operation=TV_BLEND,.layer=s->bar_layer,.blend=s->subtract_blend,.alpha=alpha};
        cb->emit(cb->context,&c);
        c=(tv_command){.operation=TV_STRETCH,.layer=s->bar_layer,.resource=s->bar_light,
            .x=x-11,.y=y-8,.width=152,.height=128,.sy=(frame/2)%128,.sw=16,.sh=128};
        cb->emit(cb->context,&c);
    }
}
#endif

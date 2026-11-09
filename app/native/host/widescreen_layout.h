#ifndef WIDESCREEN_LAYOUT_H
#define WIDESCREEN_LAYOUT_H

/* Keep 600 canvas pixels at the same scale as the original centered scene. */
static inline void widescreen_source_span(int screen_width, int scene_height,
    int image_width, int image_height, float *left, float *right)
{
    float visible = (float)screen_width * image_height / scene_height;
    *left = (image_width - visible) * 0.5f;
    *right = (image_width + visible) * 0.5f;
}

#endif

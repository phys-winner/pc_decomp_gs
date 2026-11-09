#ifndef D3D9_PRESENT_H
#define D3D9_PRESENT_H
#include "d3d9_internal.h"

int d3d_present_filtered(d3d_surface *surface, int source_w, int sw, int sh,
    int dx, int dy, int dw, int dh, int filter);
void d3d_present_widescreen(int sw, int sh, int dx, int dy, int dw, int dh);

#endif

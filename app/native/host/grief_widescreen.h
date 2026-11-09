#ifndef GRIEF_WIDESCREEN_H
#define GRIEF_WIDESCREEN_H
#include <stdint.h>

/* IDs: title, stage selection, then five victories. Returns malloc-owned top-down RGBA;
 * caller frees after upload. No retained pixel/index buffers; NULL leaves dimensions zero. */
uint8_t *void_grief_widescreen_image(unsigned id, uint32_t *width, uint32_t *height);
/* Validate every known entry before passing the bank to the original engine loader. */
int void_grief_widescreen_validate(void);
/* The engine mounted matching center crops as its highest-priority asset bank. */
int void_grief_widescreen_mounted(void);
#endif

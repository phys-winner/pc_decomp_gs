#ifndef GRIEF_AAUDIO_COMPAT_H
#define GRIEF_AAUDIO_COMPAT_H
#include <jni.h>
#include <stdint.h>

void *aaudio_compat_open(JavaVM *vm);
void aaudio_compat_close(void *stream);
int32_t aaudio_compat_write(void *stream, const int16_t *frames, int32_t count);
int32_t aaudio_compat_capacity(void *stream);
int32_t aaudio_compat_burst(void *stream);
uint32_t aaudio_compat_available(void *stream);

#endif

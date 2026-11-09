#ifndef GRIEF_ANDROID_ASSETS_H
#define GRIEF_ANDROID_ASSETS_H
#include <jni.h>
#include <sys/stat.h>
#include <sys/types.h>

void void_android_assets_init(JNIEnv *env, jobject manager);
int android_asset_exists(const char *path);
int android_asset_is_file(int fd);
int android_asset_open(const char *path, int flags);
int android_asset_close(int fd);
int android_asset_dup(int fd);
ssize_t android_asset_read(int fd, void *data, size_t size);
ssize_t android_asset_pread(int fd, void *data, size_t size, off_t offset);
off_t android_asset_seek(int fd, off_t offset, int whence);
int android_asset_fstat(int fd, struct stat *st);
int android_asset_stat(const char *path, struct stat *st);

#endif

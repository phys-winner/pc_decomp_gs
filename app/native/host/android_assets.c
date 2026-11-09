#ifdef VOID_ANDROID_APP
#include "android_assets.h"
#include "host.h"
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define ASSET_FD_BASE 1000000000
#define ASSET_FD_LIMIT 64

static AAssetManager *manager;
static pthread_mutex_t asset_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
    const uint8_t *data;
    size_t length;
    char *name;
    int refs;
    off_t position;
} asset_file;
typedef struct asset_blob {
    char *name;
    uint8_t *data;
    size_t length;
    struct asset_blob *next;
} asset_blob;
static asset_blob *blobs;
static asset_file *files[ASSET_FD_LIMIT];
static unsigned io_errors;

static int asset_index(int fd)
{
    return fd >= ASSET_FD_BASE && fd < ASSET_FD_BASE + ASSET_FD_LIMIT ? fd - ASSET_FD_BASE : -1;
}

int android_asset_is_file(int fd)
{
    return asset_index(fd) >= 0;
}

static const char *asset_path(const char *path)
{
    const char *name = !strncmp(path, "asset:/", 7) ? path + 7 : path;
    while (*name == '/' || *name == '\\') ++name;
    return name;
}

void void_android_assets_init(JNIEnv *env, jobject object)
{
    if (!manager && object) manager = AAssetManager_fromJava(env, object);
}

int android_asset_exists(const char *path)
{
    if (!manager) return 0;
    static const char *const embedded[] = {
        "gs00.dat", "gs01.dat", "gs02.dat", "gs03.dat", "griefsyndrome.exe",
    };
    path = asset_path(path);
    for (unsigned int i = 0; i < sizeof embedded / sizeof embedded[0]; ++i)
        if (strcmp(path, embedded[i])) continue;
        else {
            static uint32_t logged;
            if (!(__atomic_fetch_or(&logged, 1u << i, __ATOMIC_RELAXED) & (1u << i)))
                void_log("[assets] direct %s", path);
            AAsset *asset = AAssetManager_open(manager, path, AASSET_MODE_STREAMING);
            if (!asset) return 0;
            AAsset_close(asset);
            return 1;
        }
    return 0;
}

static const uint8_t *asset_load(const char *name, size_t *length)
{
    for (asset_blob *blob = blobs; blob; blob = blob->next) {
        if (!strcmp(blob->name, name)) {
            *length = blob->length;
            return blob->data;
        }
    }
    AAsset *asset = AAssetManager_open(manager, name, AASSET_MODE_STREAMING);
    if (!asset) { errno = ENOENT; return NULL; }
    off_t raw_length = AAsset_getLength(asset);
    size_t size = (size_t)raw_length;
    if (raw_length < 0 || (off_t)size != raw_length) {
        AAsset_close(asset);
        errno = EFBIG;
        return NULL;
    }
    uint8_t *data = (uint8_t *)malloc(size ? size : 1);
    if (!data) { AAsset_close(asset); errno = ENOMEM; return NULL; }
    size_t position = 0;
    while (position < size) {
        int got = AAsset_read(asset, data + position, size - position > (1u << 20) ? (1u << 20) : size - position);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) {
            free(data);
            AAsset_close(asset);
            errno = EIO;
            return NULL;
        }
        position += (size_t)got;
    }
    AAsset_close(asset);
    asset_blob *blob = (asset_blob *)calloc(1, sizeof *blob);
    if (!blob) { free(data); errno = ENOMEM; return NULL; }
    blob->name = strdup(name);
    if (!blob->name) { free(blob); free(data); errno = ENOMEM; return NULL; }
    blob->data = data;
    blob->length = size;
    blob->next = blobs;
    blobs = blob;
    *length = size;
    return data;
}

int android_asset_open(const char *path, int flags)
{
    if (strncmp(path, "asset:/", 7)) return open(path, flags, 0666);
    if ((flags & O_ACCMODE) != O_RDONLY) { errno = EACCES; return -1; }
    if (!manager) { errno = ENOENT; return -1; }
    asset_file *file = (asset_file *)calloc(1, sizeof *file);
    if (!file) { errno = ENOMEM; return -1; }
    file->name = strdup(asset_path(path));
    if (!file->name) { free(file); errno = ENOMEM; return -1; }
    pthread_mutex_lock(&asset_lock);
    file->data = asset_load(file->name, &file->length);
    if (!file->data) {
        pthread_mutex_unlock(&asset_lock);
        free(file->name);
        free(file);
        return -1;
    }
    file->refs = 1;
    for (int i = 0; i < ASSET_FD_LIMIT; ++i) if (!files[i]) {
        files[i] = file;
        pthread_mutex_unlock(&asset_lock);
        return ASSET_FD_BASE + i;
    }
    pthread_mutex_unlock(&asset_lock);
    if (io_errors++ < 32) void_log("[assets] open failed %s: %s", asset_path(path), strerror(EMFILE));
    free(file->name);
    free(file);
    errno = EMFILE;
    return -1;
}

int android_asset_close(int fd)
{
    int index = asset_index(fd);
    if (index < 0) return close(fd);
    pthread_mutex_lock(&asset_lock);
    asset_file *file = files[index];
    if (!file) { pthread_mutex_unlock(&asset_lock); errno = EBADF; return -1; }
    files[index] = NULL;
    if (!--file->refs) {
        free(file->name);
        free(file);
    }
    pthread_mutex_unlock(&asset_lock);
    return 0;
}

int android_asset_dup(int fd)
{
    int index = asset_index(fd);
    if (index < 0) return dup(fd);
    pthread_mutex_lock(&asset_lock);
    asset_file *file = files[index];
    if (!file) { pthread_mutex_unlock(&asset_lock); errno = EBADF; return -1; }
    for (int i = 0; i < ASSET_FD_LIMIT; ++i) if (!files[i]) {
        files[i] = file;
        ++file->refs;
        pthread_mutex_unlock(&asset_lock);
        return ASSET_FD_BASE + i;
    }
    pthread_mutex_unlock(&asset_lock);
    if (io_errors++ < 32) void_log("[assets] dup failed %s: %s", file->name, strerror(EMFILE));
    errno = EMFILE;
    return -1;
}

static void asset_io_error(asset_file *file, const char *operation, off_t offset, size_t size, ssize_t result)
{
    if (io_errors++ < 32)
        void_log("[assets] %s %s offset=%lld requested=%zu result=%zd errno=%d", operation,
            file->name, (long long)offset, size, result, errno);
}

/* Caller holds asset_lock. Sequential reads and dup share one open-file
 * description; pread does not change its cursor. */
static ssize_t asset_read_at(asset_file *file, void *data, size_t size, off_t offset, int positional)
{
    if (offset < 0) { errno = EINVAL; return -1; }
    if ((size_t)offset >= file->length) return 0;
    size_t available = file->length - (size_t)offset;
    if (size > available) size = available;
    memcpy(data, file->data + offset, size);
    if (!positional) file->position = offset + (off_t)size;
    return (ssize_t)size;
}

ssize_t android_asset_pread(int fd, void *data, size_t size, off_t offset)
{
    int index = asset_index(fd);
    if (index < 0) return pread(fd, data, size, offset);
    pthread_mutex_lock(&asset_lock);
    asset_file *file = files[index];
    ssize_t result = file ? asset_read_at(file, data, size, offset, 1) : -1;
    if (!file) errno = EBADF;
    pthread_mutex_unlock(&asset_lock);
    return result;
}

ssize_t android_asset_read(int fd, void *data, size_t size)
{
    int index = asset_index(fd);
    if (index < 0) return read(fd, data, size);
    pthread_mutex_lock(&asset_lock);
    asset_file *file = files[index];
    ssize_t result = file ? asset_read_at(file, data, size, file->position, 0) : -1;
    if (!file) errno = EBADF;
    pthread_mutex_unlock(&asset_lock);
    return result;
}

off_t android_asset_seek(int fd, off_t offset, int whence)
{
    int index = asset_index(fd);
    if (index < 0) return lseek(fd, offset, whence);
    pthread_mutex_lock(&asset_lock);
    asset_file *file = files[index];
    if (!file) { pthread_mutex_unlock(&asset_lock); errno = EBADF; return -1; }
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        pthread_mutex_unlock(&asset_lock);
        errno = EINVAL;
        return -1;
    }
    off_t base = whence == SEEK_CUR ? file->position : whence == SEEK_END ? file->length : 0;
    if ((offset > 0 && base > INT64_MAX - offset) || (offset < 0 && base < INT64_MIN - offset)) {
        pthread_mutex_unlock(&asset_lock);
        errno = EOVERFLOW;
        return -1;
    }
    off_t position = base + offset;
    if (position < 0) {
        pthread_mutex_unlock(&asset_lock);
        errno = EINVAL;
        return -1;
    }
    file->position = position;
    pthread_mutex_unlock(&asset_lock);
    return position;
}

int android_asset_fstat(int fd, struct stat *st)
{
    int index = asset_index(fd);
    if (index < 0) return fstat(fd, st);
    memset(st, 0, sizeof *st);
    pthread_mutex_lock(&asset_lock);
    asset_file *file = files[index];
    if (!file) { pthread_mutex_unlock(&asset_lock); errno = EBADF; return -1; }
    st->st_mode = S_IFREG | 0444;
    st->st_size = file->length;
    pthread_mutex_unlock(&asset_lock);
    return 0;
}

int android_asset_stat(const char *path, struct stat *st)
{
    if (strncmp(path, "asset:/", 7)) return stat(path, st);
    int fd = android_asset_open(path, O_RDONLY);
    if (fd < 0) return -1;
    int result = android_asset_fstat(fd, st);
    int error = errno;
    android_asset_close(fd);
    errno = error;
    return result;
}

#endif

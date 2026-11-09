#include "host.h"
#include "grief_widescreen.h"
#include <stdio.h>

#define WIDESCREEN_IMAGES 7
#define WIDESCREEN_ENTRIES 16
#define WIDESCREEN_INDEX_MAX 4096
#define WIDESCREEN_IMAGE_MAX (64u << 20)

static const char *const image_names[WIDESCREEN_ENTRIES] = {
    "data/widescreen/title.cv2", "data/widescreen/stage_select.cv2",
    "data/widescreen/clear1.cv2", "data/widescreen/clear2.cv2", "data/widescreen/clear3.cv2",
    "data/widescreen/clear4.cv2", "data/widescreen/clear5.cv2",
    "data/system/Title/title_back.cv2", "data/system/Title/title_back_B.cv2",
    "data/system/StageSelect/back.cv2", "data/actor/boss/clear1.dds",
    "data/actor/boss2/clear2.dds", "data/actor/boss3/clear3.dds",
    "data/actor/boss4/clear4.dds", "data/actor/boss5/clear5.dds",
    "data/system/Title/TitleBG.act"
};

typedef struct { uint32_t offset, size; } image_entry;
typedef struct { uint32_t state[624]; unsigned at; } package_random;

static uint32_t read_u32(const uint8_t *data)
{ uint32_t value; memcpy(&value, data, sizeof value); return value; }

static void package_seed(package_random *random, uint32_t seed)
{
    random->state[0] = seed;
    for (unsigned i = 1; i < 624; ++i)
        random->state[i] = 1812433253u * (random->state[i - 1] ^ (random->state[i - 1] >> 30)) + i;
    random->at = 624;
}

static uint32_t package_next(package_random *random)
{
    if (random->at == 624) {
        for (unsigned i = 0; i < 624; ++i) {
            uint32_t y = (random->state[i] & 0x80000000u) | (random->state[(i + 1) % 624] & 0x7FFFFFFFu);
            random->state[i] = random->state[(i + 397) % 624] ^ (y >> 1) ^ ((y & 1) ? 0x9908B0DFu : 0);
        }
        random->at = 0;
    }
    uint32_t y = random->state[random->at++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9D2C5680u;
    y ^= (y << 15) & 0xEFC60000u;
    return y ^ (y >> 18);
}

static void package_index_decode(uint8_t *index, uint32_t size)
{
    package_random random;
    package_seed(&random, size + 6);
    uint8_t key = 0xC5, delta = 0x89;
    for (uint32_t i = 0; i < size; ++i) {
        index[i] ^= (uint8_t)package_next(&random) ^ key;
        key += delta;
        delta += 0x49;
    }
}

static int package_index_valid(const uint8_t *index, uint32_t size, uint16_t count,
    uint64_t file_size, image_entry entries[WIDESCREEN_ENTRIES])
{
    if (count != WIDESCREEN_ENTRIES || size > WIDESCREEN_INDEX_MAX || size + 6ull > file_size) return 0;
    uint32_t at = 0, seen = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (at > size || size - at < 9) return 0;
        uint32_t offset = read_u32(index + at), bytes = read_u32(index + at + 4);
        unsigned length = index[at + 8], id;
        at += 9;
        if (length > size - at || offset < size + 6u || bytes < 17 || bytes > WIDESCREEN_IMAGE_MAX ||
            offset > file_size || bytes > file_size - offset) return 0;
        for (id = 0; id < WIDESCREEN_ENTRIES; ++id)
            if (strlen(image_names[id]) == length && !memcmp(image_names[id], index + at, length)) break;
        if (id == WIDESCREEN_ENTRIES || (seen & (1u << id))) return 0;
        entries[id] = (image_entry){offset, bytes};
        seen |= 1u << id;
        at += length;
    }
    if (at != size || seen != (1u << WIDESCREEN_ENTRIES) - 1) return 0;
    for (unsigned a = 0; a < WIDESCREEN_ENTRIES; ++a)
        for (unsigned b = a + 1; b < WIDESCREEN_ENTRIES; ++b)
            if (entries[a].offset < (uint64_t)entries[b].offset + entries[b].size &&
                entries[b].offset < (uint64_t)entries[a].offset + entries[a].size) return 0;
    return 1;
}

static int cv2_dimensions(const uint8_t header[17], uint32_t bytes, uint32_t *width, uint32_t *height)
{
    uint32_t w = read_u32(header + 1), h = read_u32(header + 5), pitch = read_u32(header + 9);
    if ((header[0] != 24 && header[0] != 32) || !w || !h || w > 8192 || h > 4096 || pitch != w ||
        read_u32(header + 13) || 17ull + (uint64_t)w * h * 4 != bytes) return 0;
    *width = w; *height = h;
    return 1;
}

static FILE *package_open(image_entry entries[WIDESCREEN_ENTRIES])
{
    char path[1200];
    if (snprintf(path, sizeof path, "%s/gs_widescreen.dat", void_root_dir()) >= (int)sizeof path) return NULL;
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    uint8_t header[6], index[WIDESCREEN_INDEX_MAX];
    if (fseek(file, 0, SEEK_END)) goto done;
    long file_size = ftell(file);
    if (file_size < 6 || (uint64_t)file_size > WIDESCREEN_ENTRIES * (uint64_t)WIDESCREEN_IMAGE_MAX + WIDESCREEN_INDEX_MAX + 6 ||
        fseek(file, 0, SEEK_SET) || fread(header, 1, 6, file) != 6) goto done;
    uint16_t count;
    memcpy(&count, header, 2);
    uint32_t index_size = read_u32(header + 2);
    if (count != WIDESCREEN_ENTRIES || index_size > WIDESCREEN_INDEX_MAX || index_size > (uint64_t)file_size - 6 ||
        fread(index, 1, index_size, file) != index_size) goto done;
    package_index_decode(index, index_size);
    if (!package_index_valid(index, index_size, count, (uint64_t)file_size, entries)) goto done;
    return file;
done:
    fclose(file);
    return NULL;
}

static int dds_center_valid(const uint8_t header[128], uint32_t bytes)
{
    return bytes == 128u + 800u * 600u * 4u && !memcmp(header, "DDS ", 4) &&
        read_u32(header + 4) == 124 && read_u32(header + 8) == 0x100F &&
        read_u32(header + 12) == 600 && read_u32(header + 16) == 800 &&
        read_u32(header + 20) == 3200 && !read_u32(header + 24) && read_u32(header + 28) == 1 &&
        read_u32(header + 76) == 32 && read_u32(header + 80) == 0x41 && read_u32(header + 84) == 0x35545844 &&
        read_u32(header + 88) == 32 && read_u32(header + 92) == 0xFF0000 &&
        read_u32(header + 96) == 0xFF00 && read_u32(header + 100) == 0xFF &&
        read_u32(header + 104) == 0xFF000000 && read_u32(header + 108) == 0x1000 &&
        !read_u32(header + 112) && !read_u32(header + 116) && !read_u32(header + 120);
}

static int title_background_valid(FILE *file, image_entry entry)
{
    /* This metadata is executable by the original ACT loader: accept only the
     * pinned 5534-byte original with its four-byte subtractive-alpha patch. */
    uint8_t data[5534];
    if (entry.size != sizeof data || fseek(file, (long)entry.offset, SEEK_SET) ||
        fread(data, 1, sizeof data, file) != sizeof data) return 0;
    uint8_t key = (uint8_t)((entry.offset >> 1) | 0x23u);
    uint64_t hash = 14695981039346656037ull;
    for (unsigned i = 0; i < sizeof data; ++i) {
        data[i] ^= key;
        hash = (hash ^ data[i]) * 1099511628211ull;
    }
    return !memcmp(data, "ACT1", 4) && !read_u32(data + 1208) && hash == 0xCFD0AF1503679866ull;
}

int void_grief_widescreen_validate(void)
{
    image_entry entries[WIDESCREEN_ENTRIES];
    FILE *file = package_open(entries);
    if (!file) return 0;
    int valid = 1;
    for (unsigned i = 0; i < WIDESCREEN_ENTRIES && valid; ++i) {
        if (i == 15) { valid = title_background_valid(file, entries[i]); continue; }
        uint8_t header[128];
        unsigned size = i < 10 ? 17 : 128;
        image_entry entry = entries[i];
        if (entry.size < size || fseek(file, (long)entry.offset, SEEK_SET) || fread(header, 1, size, file) != size) { valid = 0; break; }
        uint8_t key = (uint8_t)((entry.offset >> 1) | 0x23u);
        for (unsigned j = 0; j < size; ++j) header[j] ^= key;
        uint32_t w, h;
        valid = i >= 10 ? dds_center_valid(header, entry.size) : cv2_dimensions(header, entry.size, &w, &h);
        if (valid && i >= WIDESCREEN_IMAGES && i < 10) valid = w == 800 && h == 600;
    }
    fclose(file);
    return valid;
}

uint8_t *void_grief_widescreen_image(unsigned id, uint32_t *width, uint32_t *height)
{
    if (!width || !height) return NULL;
    *width = *height = 0;
    if (id >= WIDESCREEN_IMAGES) return NULL;
    image_entry entries[WIDESCREEN_ENTRIES];
    FILE *file = package_open(entries);
    if (!file) return NULL;
    uint8_t header[17], *pixels = NULL;
    image_entry entry = entries[id];
    if (fseek(file, (long)entry.offset, SEEK_SET) || fread(header, 1, 17, file) != 17) goto done;
    uint8_t key = (uint8_t)((entry.offset >> 1) | 0x23u);
    for (unsigned i = 0; i < 17; ++i) header[i] ^= key;
    uint32_t w, h;
    if (!cv2_dimensions(header, entry.size, &w, &h)) goto done;
    size_t size = entry.size - 17;
    pixels = malloc(size);
    if (!pixels || fread(pixels, 1, size, file) != size) { free(pixels); pixels = NULL; goto done; }
    for (size_t i = 0; i < size; i += 4) {
        uint8_t blue = pixels[i] ^ key;
        pixels[i] = pixels[i + 2] ^ key;
        pixels[i + 1] ^= key;
        pixels[i + 2] = blue;
        pixels[i + 3] = header[0] == 24 ? 255 : pixels[i + 3] ^ key;
    }
    *width = w; *height = h;
    void_log("[widescreen] loaded %s %ux%u", image_names[id], w, h);
done:
    fclose(file);
    return pixels;
}

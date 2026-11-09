/* Execute the production GS/CV2 reader against valid and malformed external banks. */
#include "../app/native/host/grief_widescreen.c"
#include "../app/native/host/d3dx9.c"
#include "../app/native/host/d3d9_format.c"
#include <assert.h>
#include <unistd.h>

static char test_root[] = "/data/local/tmp/grief-widescreen-XXXXXX";
const char *void_root_dir(void) { return test_root; }
void void_log(const char *fmt, ...) { (void)fmt; }

static void write_bank(const char *path, const uint8_t *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, size, file) == size);
    assert(!fclose(file));
}

static void put_u32(uint8_t *data, unsigned at, uint32_t value)
{ memcpy(data + at, &value, 4); }

int main(int argc, char **argv)
{
    assert(argc == 2);
    uint8_t title_bg[5534];
    FILE *metadata = fopen(argv[1], "rb");
    assert(metadata && fread(title_bg, 1, sizeof title_bg, metadata) == sizeof title_bg && fgetc(metadata) == EOF);
    assert(!fclose(metadata));
    assert(!memcmp(title_bg, "ACT1", 4) && read_u32(title_bg + 1208) == 0x3D4CCCCDu);
    memset(title_bg + 1208, 0, 4);
    package_random random;
    package_seed(&random, 5489);
    assert(package_next(&random) == 3499211612u);
    assert(package_next(&random) == 581869302u);
    assert(package_next(&random) == 3890346734u);
    uint8_t *data = calloc(1, 16u << 20), index[1024] = {0};
    assert(data);
    uint16_t count = WIDESCREEN_ENTRIES;
    uint32_t index_size = 0;
    for (unsigned i = 0; i < count; ++i) index_size += 9 + (uint32_t)strlen(image_names[i]);
    memcpy(data, &count, 2); memcpy(data + 2, &index_size, 4);
    uint32_t at = 0, offset = index_size + 6;
    uint8_t cv2[25] = {32};
    uint32_t w = 2, h = 1;
    memcpy(cv2 + 1, &w, 4); memcpy(cv2 + 5, &h, 4); memcpy(cv2 + 9, &w, 4);
    const uint8_t bgra[] = {1, 2, 3, 255, 10, 20, 30, 128};
    memcpy(cv2 + 17, bgra, sizeof bgra);
    for (unsigned i = 0; i < count; ++i) {
        uint32_t bytes = i < 7 ? 25 : i < 10 ? 17 + 800 * 600 * 4 : i < 15 ? 128 + 800 * 600 * 4 : sizeof title_bg;
        uint8_t length = (uint8_t)strlen(image_names[i]);
        memcpy(index + at, &offset, 4); memcpy(index + at + 4, &bytes, 4); index[at + 8] = length;
        memcpy(index + at + 9, image_names[i], length);
        uint8_t key = (uint8_t)((offset >> 1) | 0x23u);
        memset(data + offset, key, bytes);
        uint8_t header[128] = {32};
        unsigned header_size = i < 10 ? 17 : 128;
        if (i < 7) memcpy(header, cv2, sizeof cv2);
        else if (i < 10) {
            put_u32(header, 1, 800); put_u32(header, 5, 600); put_u32(header, 9, 800);
        } else if (i < 15) {
            memset(header, 0, sizeof header); memcpy(header, "DDS ", 4);
            put_u32(header, 4, 124); put_u32(header, 8, 0x100F);
            put_u32(header, 12, 600); put_u32(header, 16, 800); put_u32(header, 20, 3200); put_u32(header, 28, 1);
            put_u32(header, 76, 32); put_u32(header, 80, 0x41); put_u32(header, 84, 0x35545844); put_u32(header, 88, 32);
            put_u32(header, 92, 0xFF0000); put_u32(header, 96, 0xFF00);
            put_u32(header, 100, 0xFF); put_u32(header, 104, 0xFF000000); put_u32(header, 108, 0x1000);
            uint8_t unchanged[128]; memcpy(unchanged, header, sizeof header);
            dds_info decoded;
            assert(dds_center_valid(header, bytes));
            assert(parse_dds(header, sizeof header, &decoded) && decoded.format == 21 &&
                decoded.width == 800 && decoded.height == 600 && decoded.data_offset == 128 &&
                !decoded.native && level_bytes(d3d_format_find(decoded.format), 800, 600) == 800u * 600u * 4u);
            const uint8_t sample[] = {74, 66, 41, 255, 1, 2, 3, 128};
            void *owned = NULL; size_t uploaded = 0;
            const d3d_format *format = d3d_format_find(decoded.format);
            const void *converted = d3d_format_convert(format, sample, 2, 1, 8, &owned, &uploaded, 0);
            assert(converted == sample && !owned && uploaded == sizeof sample && !memcmp(converted, sample, sizeof sample));
            assert(format->swizzle[0] == GL_BLUE && format->swizzle[1] == GL_GREEN &&
                format->swizzle[2] == GL_RED && format->swizzle[3] == GL_ALPHA);
            assert(!memcmp(header, unchanged, sizeof header));
            put_u32(header, 84, 0);
            assert(!dds_center_valid(header, bytes));
            put_u32(header, 84, 0x35545844);
            put_u32(header, 80, 0x45);
            assert(!dds_center_valid(header, bytes));
            assert(parse_dds(header, sizeof header, &decoded) && decoded.format == 0x35545844);
            put_u32(header, 80, 0x41);
        }
        if (i == 15)
            for (unsigned j = 0; j < sizeof title_bg; ++j) data[offset + j] = title_bg[j] ^ key;
        else for (unsigned j = 0; j < header_size; ++j) data[offset + j] = header[j] ^ key;
        if (i < 7) for (unsigned j = 17; j < 25; ++j) data[offset + j] = cv2[j] ^ key;
        at += 9 + length; offset += bytes;
    }
    image_entry entries[WIDESCREEN_ENTRIES];
    assert(package_index_valid(index, index_size, count, offset, entries));
    assert(!package_index_valid(index, index_size - 1, count, offset, entries));
    assert(!package_index_valid(index, index_size, count, offset - 1, entries));
    index[9] ^= 1;
    assert(!package_index_valid(index, index_size, count, offset, entries));
    index[9] ^= 1;
    uint32_t original_offset = read_u32(index), bad_offset = UINT32_MAX;
    memcpy(index, &bad_offset, 4);
    assert(!package_index_valid(index, index_size, count, offset, entries));
    memcpy(index, &original_offset, 4);
    uint32_t second_at = 9 + (uint32_t)strlen(image_names[0]);
    uint32_t second_offset = read_u32(index + second_at);
    memcpy(index + second_at, &original_offset, 4);
    assert(!package_index_valid(index, index_size, count, offset, entries));
    memcpy(index + second_at, &second_offset, 4);
    assert(package_index_valid(index, index_size, count, offset, entries));
    package_index_decode(index, index_size);
    memcpy(data + 6, index, index_size);
    assert(mkdtemp(test_root));
    char path[256];
    snprintf(path, sizeof path, "%s/gs_widescreen.dat", test_root);
    write_bank(path, data, offset);
    assert(void_grief_widescreen_validate());
    data[entries[15].offset + 1000] ^= 1;
    write_bank(path, data, offset);
    assert(!void_grief_widescreen_validate());
    data[entries[15].offset + 1000] ^= 1;
    data[entries[15].offset + 1208] ^= 1;
    write_bank(path, data, offset);
    assert(!void_grief_widescreen_validate());
    data[entries[15].offset + 1208] ^= 1;
    write_bank(path, data, offset);
    assert(void_grief_widescreen_validate());
    for (unsigned i = 0; i < WIDESCREEN_IMAGES; ++i) {
        uint8_t *pixels = void_grief_widescreen_image(i, &w, &h);
        const uint8_t rgba[] = {3, 2, 1, 255, 30, 20, 10, 128};
        assert(pixels && w == 2 && h == 1 && !memcmp(pixels, rgba, sizeof rgba));
        free(pixels);
    }
    data[entries[14].offset + 16] ^= 1;
    write_bank(path, data, offset);
    assert(!void_grief_widescreen_validate());
    data[entries[14].offset + 16] ^= 1;
    write_bank(path, data, 5);
    assert(!void_grief_widescreen_image(0, &w, &h) && !w && !h);
    write_bank(path, data, 8);
    assert(!void_grief_widescreen_image(0, &w, &h));
    write_bank(path, data, offset - 1);
    assert(!void_grief_widescreen_image(0, &w, &h));
    data[original_offset] ^= 1;
    write_bank(path, data, offset);
    assert(!void_grief_widescreen_image(0, &w, &h));
    data[original_offset] ^= 1;
    uint32_t invalid = WIDESCREEN_INDEX_MAX + 1;
    memcpy(data + 2, &invalid, 4);
    write_bank(path, data, offset);
    assert(!void_grief_widescreen_image(0, &w, &h));
    w = h = 123;
    assert(!void_grief_widescreen_image(WIDESCREEN_IMAGES, &w, &h) && !w && !h);
    assert(!cv2_dimensions(cv2, 24, &w, &h));
    invalid = 9000; memcpy(cv2 + 1, &invalid, 4);
    assert(!cv2_dimensions(cv2, 25, &w, &h));
    assert(!unlink(path) && !rmdir(test_root));
    free(data);
    puts("PASS: sixteen-entry bank validation, exact title metadata, legacy DDS compatibility decodes as lossless ARGB32 with unchanged pixels; malformed GS/CV2/DDS/ACT rejection");
    return 0;
}

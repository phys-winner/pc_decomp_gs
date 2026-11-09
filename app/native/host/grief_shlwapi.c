#include "host.h"
#include <sys/stat.h>

static int path_stat(int directory)
{
    char *path = void_wstr_utf8(ARG(0));
    char *host = path ? void_path_to_host(path) : NULL;
    struct stat st;
    int result = host && !stat(host, &st) && (directory ? S_ISDIR(st.st_mode) : 1);
    free(path);
    free(host);
    return result;
}

WINAPI_EXPORT("shlwapi.dll", PathFileExistsW, 1) { return path_stat(0); }
WINAPI_EXPORT("shlwapi.dll", PathIsDirectoryW, 1) { return path_stat(1); }

WINAPI_EXPORT("shlwapi.dll", PathRemoveFileSpecW, 1)
{
    uint32_t path = ARG(0);
    uint32_t last = path;
    for (uint32_t p = path; MEM16(p); p += 2) if (MEM16(p) == '\\' || MEM16(p) == '/') last = p;
    MEM16(last) = 0;
    return 1;
}

WINAPI_EXPORT("shlwapi.dll", PathAppendW, 2)
{
    uint32_t path = ARG(0), suffix = ARG(1), length = 0;
    while (length < 260 && MEM16(path + 2 * length)) ++length;
    if (!length || length >= 259) return 0;
    if (MEM16(path + 2 * (length - 1)) != '\\' && MEM16(path + 2 * (length - 1)) != '/')
        MEM16(path + 2 * length++) = '\\';
    while (length + 1 < 260) {
        uint16_t value = MEM16(suffix);
        suffix += 2;
        MEM16(path + 2 * length++) = value;
        if (!value) return 1;
    }
    MEM16(path + 2 * 259) = 0;
    return 0;
}

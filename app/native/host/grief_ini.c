#include "host.h"
#include <strings.h>

#define K32 "kernel32.dll"

int void_grief_lan_host(void);
int void_grief_lan_port(void);
const char *void_grief_lan_address(void);

static uint32_t profile_string(uint32_t out, uint32_t capacity, const char *value, const char *fallback)
{
    if (!out || !capacity) return 0;
    const char *text = value ? value : fallback ? fallback : "";
    uint32_t written = void_utf8_to_gw(text, out, capacity);
    if (written >= capacity) {
        MEM16(out + 2 * (capacity - 1)) = 0;
        return capacity - 1;
    }
    return written;
}

WINAPI_EXPORT(K32, GetPrivateProfileStringW, 6)
{
    char *section = void_wstr_utf8(ARG(0));
    char *key = void_wstr_utf8(ARG(1));
    char *fallback = void_wstr_utf8(ARG(2));
    char value[64];
    value[0] = 0;
    if (section && key && !strcasecmp(section, "Network")) {
        if (!strcasecmp(key, "IsHost")) snprintf(value, sizeof value, "%d", void_grief_lan_host());
        else if (!strcasecmp(key, "Address")) snprintf(value, sizeof value, "%s", void_grief_lan_address());
        else if (!strcasecmp(key, "Port")) snprintf(value, sizeof value, "%d", void_grief_lan_port());
    }
    if (section && key && !strcasecmp(section, "Network"))
        void_log("[lan] profile %s/%s = %s", section, key, value);
    uint32_t result = profile_string(ARG(3), ARG(4), value[0] ? value : NULL, fallback);
    free(section);
    free(key);
    free(fallback);
    return result;
}

WINAPI_EXPORT(K32, GetPrivateProfileIntW, 4)
{
    char *section = void_wstr_utf8(ARG(0));
    char *key = void_wstr_utf8(ARG(1));
    int32_t fallback = (int32_t)ARG(2);
    int32_t result = fallback;
    if (section && key && !strcasecmp(section, "Network")) {
        if (!strcasecmp(key, "IsHost")) result = void_grief_lan_host();
        else if (!strcasecmp(key, "Port")) result = void_grief_lan_port();
    } else if (section && key && !strcasecmp(section, "Option")) {
        result = 0;
    } else if (section && key && !strcasecmp(section, "IM") && !strcasecmp(key, "DisplayTime")) {
        result = 7;
    }
    free(section);
    free(key);
    return (uint32_t)result;
}

WINAPI_EXPORT(K32, WritePrivateProfileStringW, 4) { return 1; }

WINAPI_EXPORT(K32, GetPrivateProfileStringA, 6)
{
    char *section = void_astr_utf8(ARG(0));
    char *key = void_astr_utf8(ARG(1));
    char value[64];
    value[0] = 0;
    if (section && key && !strcasecmp(section, "Network")) {
        if (!strcasecmp(key, "IsHost")) snprintf(value, sizeof value, "%d", void_grief_lan_host());
        else if (!strcasecmp(key, "Address")) snprintf(value, sizeof value, "%s", void_grief_lan_address());
        else if (!strcasecmp(key, "Port")) snprintf(value, sizeof value, "%d", void_grief_lan_port());
    }
    uint32_t fallback = ARG(2), out = ARG(3), size = ARG(4);
    if (!out || !size || !void_vm_writable(out, size)) {
        free(section);
        free(key);
        return 0;
    }
    if (value[0]) {
        uint32_t result = (uint32_t)snprintf(GPTR(out), size, "%s", value);
        free(section);
        free(key);
        return result >= size ? size - 1 : result;
    }
    uint32_t written = 0;
    if (fallback) {
        size_t length = void_gstrlen(fallback);
        written = (uint32_t)(length < size - 1 ? length : size - 1);
        memcpy(GPTR(out), GPTR(fallback), written);
    }
    MEM8(out + written) = 0;
    free(section);
    free(key);
    void_set_last_error(2u);
    return written;
}

WINAPI_EXPORT(K32, GetPrivateProfileIntA, 4)
{
    char *section = void_astr_utf8(ARG(0));
    char *key = void_astr_utf8(ARG(1));
    int32_t result = (int32_t)ARG(2);
    if (section && key && !strcasecmp(section, "Network")) {
        if (!strcasecmp(key, "IsHost")) result = void_grief_lan_host();
        else if (!strcasecmp(key, "Port")) result = void_grief_lan_port();
    } else if (section && key && !strcasecmp(section, "Option")) {
        result = 0;
    } else if (section && key && !strcasecmp(section, "IM") && !strcasecmp(key, "DisplayTime")) {
        result = 7;
    }
    if (section && key && !strcasecmp(section, "Network"))
        void_log("[lan] profile %s/%s = %d", section, key, result);
    free(section);
    free(key);
    return (uint32_t)result;
}

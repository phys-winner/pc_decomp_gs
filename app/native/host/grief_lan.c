#include "host.h"

#define GSO_ZLIB_HOOK 0x10005630u
#define GSO_SQUIRREL_HOOK 0x10005810u

static int lan_requested, lan_loaded;
static int lan_host = 1, lan_port = 6112, lan_players = 2;
static char lan_address[64] = "127.0.0.1";
static int missing_save_synced;
static int lan_participant_mask;

void void_grief_lan_missing_save(const char *path)
{
    if (!lan_loaded || missing_save_synced || !path) return;
    char expected[1200];
    snprintf(expected, sizeof expected, "%s/save/save0.dat", void_root_dir());
    char *host = void_path_to_host(path);
    int match = host && !strcmp(host, expected);
    free(host);
    if (!match) return;
    missing_save_synced = 1;
    /* GSOnline synchronizes loopNum inside decompression; a first-run peer must join that exchange too. */
    static const uint8_t default_lap[] = {
        0x78, 0x9c, 0x63, 0xc8, 0xc9, 0xcf, 0x2f, 0xf0, 0x2b, 0xcd, 0x65, 0x62,
        0x60, 0x60, 0x65, 0x00, 0x02, 0x00, 0x23, 0x43, 0x02, 0xf2
    };
    void_regs saved;
    void_regs_save(&saved);
    uint32_t caller = g_cur_func;
    uint32_t input = void_galloc(sizeof default_lap), output = void_galloc(0xFA000);
    memcpy(GPTR(input), default_lap, sizeof default_lap);
    uint32_t args[] = {input, sizeof default_lap, output};
    uint32_t size = void_call_guest(GSO_ZLIB_HOOK, 3, args, 0);
    if (size != 16) void_fatal("LAN default progression synchronization failed");
    void_gfree(output);
    void_gfree(input);
    void_regs_load(&saved);
    g_cur_func = caller;
    void_log("[lan] synchronized first-run progression without creating a save");
}

void void_grief_lan_request(int enabled)
{
    lan_requested = enabled != 0;
}

void void_grief_lan_configure(int host, const char *address, int port, int players)
{
    lan_host = host != 0;
    if (address && *address) snprintf(lan_address, sizeof lan_address, "%s", address);
    if (port > 0 && port <= 65535) lan_port = port;
    if (players >= 2 && players <= 3) lan_players = players;
}

int void_grief_lan_host(void) { return lan_host; }
int void_grief_lan_active(void) { return lan_loaded; }
int void_grief_lan_port(void) { return lan_port; }
int void_grief_lan_players(void) { return lan_players; }
const char *void_grief_lan_address(void) { return lan_address; }

static uint32_t lan_connected_object(void)
{
    if (!lan_loaded || !void_vm_readable(0x100286BCu, 8) || !MEM32(0x100286C0u)) return 0;
    uint32_t object = MEM32(0x100286BCu);
    return object && void_vm_readable(object, 91) ? object : 0;
}

int void_grief_lan_local_player(void)
{
    uint32_t object = lan_connected_object();
    if (!object) return -1;
    uint32_t index = MEM32(object + 84);
    return index < 3 && MEM8(object + 88 + index) == 1 ? (int)index : -1;
}

int void_grief_lan_participants(void)
{
    if (lan_participant_mask) return lan_participant_mask;
    uint32_t object = lan_connected_object();
    if (!object) return 0;
    uint32_t index = MEM32(object + 84);
    if (index >= 3) return 0;
    int mask = 0;
    for (int i = 0; i < 3; ++i)
        if (MEM8(object + 88 + i) == 1) mask |= 1 << i;
    if (!(mask & 1) || !(mask & (1 << index)) || !(mask & (mask - 1))) return 0;
    /* Handshake packet E gives every peer this bitmap; later disconnects must not change menu rules. */
    lan_participant_mask = mask;
    return mask;
}

void void_grief_lan_zlib_hook(void)
{
    uint32_t caller = g_cur_func;
    if (lan_loaded && caller == 0x004B97E0u) {
        void_fn hook = void_dispatch_find(GSO_ZLIB_HOOK);
        if (hook) {
            hook();
            g_cur_func = caller;
            return;
        }
    }
    void_fn original = void_dispatch_find(0x004ACB80u);
    original();
}

void void_grief_lan_squirrel_hook(void)
{
    uint32_t caller = g_cur_func;
    if (lan_loaded && caller == 0x0042E2E0u) {
        void_fn hook = void_dispatch_find(GSO_SQUIRREL_HOOK);
        if (hook) {
            hook();
            g_cur_func = caller;
            return;
        }
    }
    void_fn original = void_dispatch_find(0x0052BC90u);
    original();
}

void grief_lan_check_cookie(void)
{
    if (g_ecx != MEM32(0x10021040u)) void_fatal("GSOnline security cookie mismatch");
}

int void_grief_lan_start(void)
{
    lan_participant_mask = 0;
    if (!lan_requested) return 1;
    int void_grief_qa_flags(void);
    if (void_grief_qa_flags()) {
        void_log("[lan] disabled while QA options are enabled");
        return 0;
    }
    void void_grief_config_init(void);
    void_grief_config_init();
    lan_loaded = void_module_load("gso.dll") != 0;
    void_log("[lan] GSOnline initialization %s", lan_loaded ? "succeeded" : "failed");
    return lan_loaded;
}

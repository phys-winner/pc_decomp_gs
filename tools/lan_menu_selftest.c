/* Exercise the production GSOnline metadata reader without loading guest DLLs. */
#include "../app/native/host/host.h"
#include <assert.h>

static uint8_t guest[4096];
static int mapped = 1;
static void *guest_address(uint32_t address)
{
    assert(address >= 0x10028000u && address < 0x10029000u);
    return guest + address - 0x10028000u;
}
#undef RECOMP_MEM_ADDR
#define RECOMP_MEM_ADDR(address) guest_address((uint32_t)(address))
#include "../app/native/host/grief_lan.c"

int void_vm_readable(uint32_t address, uint32_t size)
{ return mapped && address >= 0x10028000u && size <= 4096 && address - 0x10028000u <= 4096 - size; }
int void_grief_qa_flags(void) { return 0; }
void void_grief_config_init(void) { }
uint32_t void_module_load(const char *path) { (void)path; return 1; }
void void_log(const char *format, ...) { (void)format; }

static void session(int index, int mask)
{
    lan_requested = 0;
    assert(void_grief_lan_start());
    memset(guest, 0, sizeof guest);
    lan_loaded = mapped = 1;
    MEM32(0x100286BCu) = 0x10028000u;
    MEM32(0x100286C0u) = 1;
    MEM32(0x10028000u + 84) = (uint32_t)index;
    for (int i = 0; i < 3; ++i) MEM8(0x10028000u + 88 + i) = !!(mask & (1 << i));
}

int main(void)
{
    assert(void_grief_lan_local_player() == -1 && !void_grief_lan_participants());
    session(0, 3);
    mapped = 0;
    assert(void_grief_lan_local_player() == -1 && !void_grief_lan_participants());
    mapped = 1;
    MEM32(0x100286BCu) = 0xFFFFFFFFu;
    assert(void_grief_lan_local_player() == -1 && !void_grief_lan_participants());
    session(3, 7);
    assert(void_grief_lan_local_player() == -1 && !void_grief_lan_participants());
    session(0, 1);
    assert(!void_grief_lan_participants());
    session(1, 6);
    assert(!void_grief_lan_participants());
    session(2, 3);
    assert(void_grief_lan_local_player() == -1 && !void_grief_lan_participants());
    for (int mask = 3; mask <= 7; ++mask) {
        if (!(mask & 1)) continue;
        for (int index = 0; index < 3; ++index) if (mask & (1 << index)) {
            session(index, mask);
            assert(void_grief_lan_local_player() == index && void_grief_lan_participants() == mask);
            MEM8(0x10028000u + 89) = MEM8(0x10028000u + 90) = 2;
            assert(void_grief_lan_participants() == mask);
        }
    }
    session(0, 3);
    MEM32(0x100286C0u) = 0;
    assert(void_grief_lan_local_player() == -1 && !void_grief_lan_participants());
    session(2, 5);
    assert(void_grief_lan_participants() == 5);
    puts("PASS: GSOnline handshake metadata validates pointers, local slot and frozen participant bitmap");
    return 0;
}

/* Exercise the production lifted CRT, including its zero-DWORD overlap jump. */
#define RECOMP_LOCAL_REGS
#define RECOMP_GUEST_PROVENANCE
#include "../generated/game-x86/recomp_0092.c"
#include <assert.h>

uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
uint32_t g_flag_k, g_flag_a, g_flag_b, g_flag_cf, g_backedges;
xmm_t g_xmm[8];
void (*recomp_yield_hook)(void);
#if defined(__arm__)
static uint8_t guest_memory[0x700000];
uint8_t *g_guest_mem = guest_memory;
#else
uint8_t g_guest_mem[0x700000];
#endif

recomp_func_t void_resolve(uint32_t address)
{ fprintf(stderr, "Unexpected guest target %08X\n", address); abort(); }

static void tables(void)
{
    /* Exact jump-table words from the verified original Japanese PE. */
    const uint32_t forward_tail[] = {0x5AA020, 0x5AA028, 0x5AA034, 0x5AA048};
    const uint32_t forward_align[] = {0x5A9F34, 0x5A9F60, 0x5A9F84};
    const uint32_t forward_words[] = {0x5AA007, 0x5A9FF4, 0x5A9FEC, 0x5A9FE4, 0x5A9FDC, 0x5A9FD4, 0x5A9FCC, 0x5A9FC4};
    const uint32_t reverse_align[] = {0x5AA0C0, 0x5AA0E4, 0x5AA10C};
    const uint32_t reverse_words[] = {0x5AA160, 0x5AA168, 0x5AA170, 0x5AA178, 0x5AA180, 0x5AA188, 0x5AA190, 0x5AA1A3};
    const uint32_t reverse_tail[] = {0x5AA1BC, 0x5AA1C4, 0x5AA1D4, 0x5AA1E8};
    memcpy(GPTR(0x5AA010), forward_tail, sizeof forward_tail);
    memcpy(GPTR(0x5A9F28), forward_align, sizeof forward_align);
    memcpy(GPTR(0x5A9FA4), forward_words, sizeof forward_words);
    memcpy(GPTR(0x5AA0B4), reverse_align, sizeof reverse_align);
    memcpy(GPTR(0x5AA140), reverse_words, sizeof reverse_words);
    memcpy(GPTR(0x5AA1AC), reverse_tail, sizeof reverse_tail);
}

static void check(uint32_t source, uint32_t destination, uint32_t count, int helper)
{
    uint8_t expected[1536];
    for (unsigned i = 0; i < sizeof expected; ++i) expected[i] = (uint8_t)(i * 37u + 11u);
    memcpy(GPTR(0x2000), expected, sizeof expected);
    memmove(expected + destination - 0x2000, expected + source - 0x2000, count);
    g_esi = 0x11223344; g_edi = 0x22334455; g_ebp = 0x33445566;
    g_esp = 0x1000;
    MEM32(g_esp) = RECOMP_RETADDR;
    MEM32(g_esp + 4) = destination;
    MEM32(g_esp + 8) = source;
    MEM32(g_esp + 12) = count;
    if (helper) {
        MEM32(0xFFC) = g_ebp; MEM32(0xFF8) = g_edi; MEM32(0xFF4) = g_esi;
        g_ebp = 0xFFC; g_esp = 0xFF4;
        g_esi = source; g_edi = destination; g_ecx = g_edx = count;
        sub_005AA060();
    } else sub_005A9EA0();
    assert(g_esp == 0x1004 && g_eax == destination);
    assert(g_esi == 0x11223344 && g_edi == 0x22334455 && g_ebp == 0x33445566);
    assert(!memcmp(GPTR(0x2000), expected, sizeof expected));
}

int main(void)
{
    tables();
    for (int simd = 0; simd <= 1; ++simd) {
        MEM32(0x6AD03C) = simd;
        for (unsigned align = 0; align < 16; ++align) for (int offset = -32; offset <= 32; ++offset)
            for (unsigned length = 0; length <= 1024; ++length) {
                if (length > 96 && length != 127 && length != 128 && length != 129 &&
                    length != 255 && length != 256 && length != 1024) continue;
                uint32_t source = 0x2040 + align, destination = source + offset;
                check(source, destination, length, 0);
                if (offset > 0 && (unsigned)offset < length) check(source, destination, length, 1);
            }
    }
    puts("PASS: lifted memcpy scalar/SSE, all 16 alignments, forward/reverse overlap, empty copies and zero-DWORD tail targets");
    return 0;
}

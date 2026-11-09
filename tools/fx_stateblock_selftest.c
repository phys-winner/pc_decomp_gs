/* Execute the production effect End path with counted state-block COM calls. */
static unsigned char test_memory[8192];
#define VOID_GUEST_BASE ((uintptr_t)test_memory)
#include "../app/native/host/d3dx43_effect.c"
#include <assert.h>

#if defined(__arm__)
uint8_t *g_guest_mem = test_memory;
#endif
uint32_t g_esp;
static fx_effect effect;
static unsigned allocations, begins, ends, applies, saved_releases, recorded_releases;
static int return_recorded, fail_end;
const fx_technique fx_techniques[] = {{.pass_count = 1}};

void *void_com_host(uint32_t guest)
{
    assert(guest == 0x200);
    return &effect;
}

uint32_t void_galloc(uint32_t size)
{
    assert(size == 4 && allocations++ == 0);
    MEM32(0x1800) = 0;
    return 0x1800;
}

void void_gfree(uint32_t address)
{
    assert(address == 0x1800 && allocations-- == 1);
}

uint32_t void_call_guest(uint32_t address, int count, const uint32_t *args, int pops)
{
    assert(pops == 1);
    if (address == 0x140) {
        assert(count == 3 && args[0] == 0x300 && args[1] == 0 && args[2] == 0x1800);
        ++begins;
        return D3DERR_INVALIDCALL;
    }
    if (address == 0x100) {
        assert(count == 2 && args[0] == 0x300 && args[1] == 0x1800);
        ++ends;
        if (!fail_end && return_recorded) MEM32(args[1]) = 0x600;
        return fail_end ? D3DERR_INVALIDCALL : D3D_OK;
    }
    assert(count == 1);
    if (address == 0x110) { assert(args[0] == 0x500); ++applies; }
    else if (address == 0x120) { assert(args[0] == 0x500); ++saved_releases; }
    else if (address == 0x130) { assert(args[0] == 0x600); ++recorded_releases; }
    else assert(0);
    return 0;
}

static void check(int recorded, int failure)
{
    memset(&effect, 0, sizeof effect);
    allocations = ends = applies = saved_releases = recorded_releases = 0;
    return_recorded = recorded; fail_end = failure;
    effect.device = 0x300;
    effect.state_block = 0x500;
    effect.in_begin = 1;
    assert(fx_End_impl() == D3D_OK);
    assert(!effect.in_begin && !effect.state_block && !allocations);
    assert(ends == 1 && applies == 1 && saved_releases == 1);
    assert(recorded_releases == (unsigned)(recorded && !failure));
    assert(fx_End_impl() == D3DERR_INVALIDCALL);
    assert(ends == 1 && saved_releases == 1 && !allocations);
}

int main(void)
{
    g_esp = 0x1000;
    MEM32(g_esp + 4) = 0x200;
    MEM32(0x300) = 0x400;
    MEM32(0x400 + 59 * 4) = 0x140;
    MEM32(0x400 + 61 * 4) = 0x100;
    MEM32(0x500) = 0x700;
    MEM32(0x700 + 5 * 4) = 0x110;
    MEM32(0x700 + 2 * 4) = 0x120;
    MEM32(0x600) = 0x800;
    MEM32(0x800 + 2 * 4) = 0x130;
    for (unsigned i = 0; i < 2048; ++i) check(1, 0);
    check(0, 0);
    check(1, 1);
    effect.in_begin = effect.in_pass = 1;
    assert(fx_End_impl() == D3DERR_INVALIDCALL && effect.in_begin);
    assert(!allocations && ends == 1 && recorded_releases == 0);
    memset(&effect, 0, sizeof effect);
    effect.device = 0x300;
    MEM32(g_esp + 8) = MEM32(g_esp + 12) = 0;
    for (unsigned i = 0; i < 2048; ++i) {
        assert(fx_Begin_impl() == D3DERR_INVALIDCALL);
        assert(!allocations && !effect.in_begin && !effect.state_block && begins == i + 1);
    }
    puts("PASS: 2048 production effect End cycles release saved/recorded COM blocks once; 2048 failed Begin calls release scratch storage; unmatched/empty paths stay balanced");
    return 0;
}

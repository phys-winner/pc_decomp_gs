/* Exercise the real guest page map and dlmalloc's coalesced/partial munmap paths. */
#include "../app/native/host/memory.c"
#include <assert.h>

#if defined(__arm__)
uint8_t *g_guest_mem;
#endif
ptrdiff_t g_mem_base;
uint32_t g_cur_func;
int void_verbose;
void void_log(const char *format, ...) { (void)format; }
_Noreturn void void_fatal(const char *format, ...) { (void)format; abort(); }
void void_set_last_error(uint32_t value) { (void)value; }

#ifdef TEST_LEGACY_UNMAP
/* The exact previous adapter, to demonstrate the fixture detects its leak. */
static int system_unmap(void *pointer, size_t size)
{
    (void)size;
    return void_vm_free(GVA(pointer), 0, MEM_RELEASE_) ? 0 : -1;
}
#else
#define system_unmap guest_sys_free
#endif

static unsigned committed_pages(void)
{
    unsigned count = 0;
    for (unsigned page = 0; page < PAGES; ++page) count += page_state[page] == ST_COMMIT;
    return count;
}

static void merged_maps(void)
{
    const unsigned span = 256u << 10;
    void *first = guest_sys_alloc(span), *second = guest_sys_alloc(span);
    assert(first != MFAIL && second != MFAIL);
    assert((uint8_t *)second == (uint8_t *)first + span);
    memset(first, 0x31, span);
    memset(second, 0x72, span);
    assert(system_unmap(first, 2 * span) == 0);
    if (void_vm_readable(GVA(second), 1)) {
        puts("FAIL: combined dlmalloc munmap returned success but left the second reservation committed");
        fflush(stdout);
    }
    assert(!void_vm_readable(GVA(first), 1));
    assert(!void_vm_readable(GVA(second), 1));
    assert(committed_pages() == 0);
}

static void split_maps(unsigned pages_per_host)
{
    host_pages = pages_per_host;
    const unsigned span = 1u << 20, quarter = span / 4;
    void *memory = guest_sys_alloc(span);
    assert(memory != MFAIL);
    uint32_t base = GVA(memory);
    memset(memory, 0xA5, span);
    assert(system_unmap(GPTR(base + quarter), quarter) == 0);
    assert(void_vm_readable(base, quarter));
    assert(!void_vm_readable(base + quarter, 1));
    assert(void_vm_readable(base + 2 * quarter, 2 * quarter));
    assert(MEM8(base) == 0xA5 && MEM8(base + 2 * quarter) == 0xA5);
    assert(page_alloc[(base + 2 * quarter) >> 12] == (base + 2 * quarter) >> 12);
    assert(!granule_used[(base + quarter) >> 16]);
    assert(system_unmap(GPTR(base), quarter) == 0);
    assert(system_unmap(GPTR(base + 2 * quarter), 2 * quarter) == 0);
    assert(committed_pages() == 0);

    memory = guest_sys_alloc(span);
    assert(GVA(memory) == base);
    assert(MEM8(base) == 0 && MEM8(base + span - 1) == 0);
    memset(memory, 0xC3, span);
    assert(system_unmap(GPTR(base + quarter), span - quarter) == 0);
    assert(void_vm_readable(base, quarter) && MEM8(base + quarter - 1) == 0xC3);
    assert(!void_vm_readable(base + quarter, 1));
    assert(system_unmap(memory, quarter) == 0);
    assert(committed_pages() == 0);

    /* A 4-KiB hole must preserve its committed neighbors on a 16-KiB host page. */
    memory = guest_sys_alloc(GUEST_GRANULARITY);
    base = GVA(memory);
    memset(memory, 0x6D, GUEST_GRANULARITY);
    assert(system_unmap(GPTR(base + GUEST_PAGE), GUEST_PAGE) == 0);
    assert(MEM8(base) == 0x6D && MEM8(base + 2 * GUEST_PAGE) == 0x6D);
    assert(!void_vm_readable(base + GUEST_PAGE, 1));
    assert(granule_used[base >> 16]);
    assert(system_unmap(memory, GUEST_PAGE) == 0);
    assert(system_unmap(GPTR(base + 2 * GUEST_PAGE), GUEST_GRANULARITY - 2 * GUEST_PAGE) == 0);
    assert(!granule_used[base >> 16] && committed_pages() == 0);
}

static void invalid_ranges(void)
{
    void *memory = guest_sys_alloc(GUEST_GRANULARITY);
    uint32_t base = GVA(memory);
    assert(memory != MFAIL);
    memset(memory, 0xE1, GUEST_GRANULARITY);
    assert(system_unmap(memory, 0) == -1);
    assert(system_unmap((uint8_t *)memory + 1, GUEST_PAGE) == -1);
    assert(system_unmap(memory, GUEST_PAGE - 1) == -1);
    assert(system_unmap((void *)((uintptr_t)GPTR(0) - GUEST_PAGE), GUEST_PAGE) == -1);
    assert(system_unmap((void *)((uintptr_t)GPTR(0) + guest_span - GUEST_PAGE), 2 * GUEST_PAGE) == -1);
    assert(system_unmap(memory, SIZE_MAX & ~(size_t)(GUEST_PAGE - 1)) == -1);
    assert(void_vm_readable(base, GUEST_GRANULARITY) && MEM8(base) == 0xE1);
    /* Public Win32 MEM_RELEASE still rejects interior addresses and nonzero size. */
    assert(!void_vm_free(base + GUEST_PAGE, 0, MEM_RELEASE_));
    assert(!void_vm_free(base, GUEST_PAGE, MEM_RELEASE_));
    assert(void_vm_readable(base, GUEST_GRANULARITY));
    assert(system_unmap(memory, GUEST_GRANULARITY) == 0);
    assert(committed_pages() == 0);
}

static void heap_cycles(void)
{
    for (unsigned cycle = 0; cycle < 64; ++cycle) {
        uint32_t heap = void_heap_create(0, 0), blocks[64];
        assert(heap);
        for (unsigned i = 0; i < 64; ++i) {
            unsigned size = (i % 8 + 1) * GUEST_GRANULARITY - 32;
            blocks[i] = void_heap_alloc(heap, size, 1);
            assert(blocks[i] && MEM8(blocks[i]) == 0 && MEM8(blocks[i] + size - 1) == 0);
            MEM8(blocks[i]) = (uint8_t)i;
            MEM8(blocks[i] + size - 1) = (uint8_t)(i + 1);
        }
        for (unsigned parity = 0; parity < 2; ++parity)
            for (unsigned i = parity; i < 64; i += 2) {
                unsigned size = (i % 8 + 1) * GUEST_GRANULARITY - 32;
                assert(MEM8(blocks[i]) == i && MEM8(blocks[i] + size - 1) == i + 1);
                assert(void_heap_free(heap, blocks[i]));
            }
        assert(void_heap_destroy(heap));
        assert(committed_pages() == 0);
    }
}

int main(void)
{
    assert(void_guest_reserve());
    merged_maps();
    split_maps(1);
    split_maps(4);
    invalid_ranges();
    heap_cycles();
    puts("PASS: production dlmalloc unmaps combined reservations and partial ranges, preserves 4/16-KiB neighbors, rejects invalid ranges and reclaims 64 real heap cycles");
    return 0;
}

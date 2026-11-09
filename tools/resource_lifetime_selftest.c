/* Exercise production COM/audio facets and guest-thread teardown with counted guest storage. */
#define _GNU_SOURCE
static unsigned char test_memory[8 * 1024 * 1024];
#define VOID_GUEST_BASE ((uintptr_t)test_memory)
#include "../app/native/host/com.c"
#define IID_IUnknown IID_DSoundIUnknown
#include "../app/native/host/dsound.c"
#undef IID_IUnknown
#include "../app/native/host/threads.c"
static int record_timer_thread(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
#define pthread_create record_timer_thread
#include "../app/native/host/system.c"
#undef pthread_create
#include <assert.h>

#if defined(__arm__)
uint8_t *g_guest_mem = test_memory;
#endif
uint32_t g_eax, g_ecx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_fs_base, g_cur_func;
void (*recomp_yield_hook)(void);
int void_verbose;
static uint32_t next_allocation = 0x10000, live_allocations;
static uint64_t live_bytes;
static struct { uint32_t address, size; } allocations[512];
static pthread_mutex_t allocation_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t timer_workers[32];
static unsigned timer_worker_count, timer_callbacks;

static int record_timer_thread(pthread_t *thread, const pthread_attr_t *attr,
    void *(*start)(void *), void *argument)
{
    (void)attr;
    assert(timer_worker_count < 32);
    unsigned index = timer_worker_count++;
    /* Join only in the test, so completion includes pthread-key destructors. */
    int result = pthread_create(&timer_workers[index], NULL, start, argument);
    *thread = timer_workers[index];
    return result;
}

void void_log(const char *fmt, ...) { (void)fmt; }
_Noreturn void void_fatal(const char *fmt, ...) { (void)fmt; abort(); }
void void_lift_trap(uint32_t va, const char *text) { (void)va; (void)text; abort(); }
uint32_t void_galloc(uint32_t size)
{
    pthread_mutex_lock(&allocation_lock);
    for (unsigned i = 0; i < 512; ++i) if (!allocations[i].address) {
        uint32_t address = next_allocation;
        next_allocation += (size + 15) & ~15u;
        assert(next_allocation < sizeof test_memory);
        allocations[i].address = address; allocations[i].size = size;
        ++live_allocations; live_bytes += size;
        memset(GPTR(address), 0, size);
        pthread_mutex_unlock(&allocation_lock);
        return address;
    }
    abort();
}
void void_gfree(uint32_t address)
{
    if (!address) return;
    pthread_mutex_lock(&allocation_lock);
    for (unsigned i = 0; i < 512; ++i) if (allocations[i].address == address) {
        --live_allocations; live_bytes -= allocations[i].size;
        allocations[i].address = 0;
        pthread_mutex_unlock(&allocation_lock);
        return;
    }
    abort();
}
uint32_t void_vm_alloc(uint32_t address, uint32_t size, uint32_t type, uint32_t protection)
{ (void)address; (void)type; (void)protection; return void_galloc(size); }
int void_vm_free(uint32_t address, uint32_t size, uint32_t type)
{ (void)size; (void)type; void_gfree(address); return 1; }
int void_vm_readable(uint32_t address, uint32_t size)
{ return address && address < sizeof test_memory && size <= sizeof test_memory - address; }
int void_vm_writable(uint32_t address, uint32_t size) { return void_vm_readable(address, size); }
uint32_t void_thunk(void_fn fn, const char *name)
{ static uint32_t next = 0x100; (void)fn; free((void *)name); return next += 16; }
uint32_t void_module_exe_base(void) { return 0x400000; }
uint32_t void_process_heap(void) { return 0x100; }
void void_eh_reset_thread(void) {}
void void_regs_save(void_regs *r) { r->esp = g_esp; r->fs = g_fs_base; }
void void_regs_load(const void_regs *r) { g_esp = r->esp; g_fs_base = r->fs; }
static void guest_worker(void) { assert(MEM32(g_esp) == RECOMP_RETADDR); g_eax = 37; g_esp += 8; }
static void timer_worker(void)
{
    uint32_t user = ARG(2);
    assert(user < timer_worker_count && pthread_equal(pthread_self(), timer_workers[user]));
    ++timer_callbacks;
    assert(!timeKillEvent_impl()); /* Callback self-kill must not join itself. */
    g_eax = 0; g_esp += 24;
}
recomp_func_t void_resolve(uint32_t va)
{
    if (va == 0x87654321) return timer_worker;
    assert(va == 0x12345678); return guest_worker;
}

static void this_argument(uint32_t guest)
{ g_esp = 0x1000; MEM32(g_esp + 4) = guest; }

static dsbuf_t *new_buffer(int primary)
{
    dsbuf_t *b = calloc(1, sizeof *b);
    b->primary = primary;
    b->flags = DSBCAPS_CTRL3D;
    if (!primary) { b->size = 1024; b->data = void_galloc(b->size); }
    void_com_wrap(&b->com, &buffer_class, 0);
    b->com.destroy = buffer_destroy;
    b->next = buffers; buffers = b;
    return b;
}

static void audio_cycle(int primary, int owner_first)
{
    dsbuf_t *b = new_buffer(primary);
    uint32_t base = b->com.guest;
    memcpy(GPTR(0x2000), primary ? IID_IDirectSound3DListener : IID_IDirectSound3DBuffer, 16);
    assert(buffer_query(b, 0x2000, 0x2010) == 0);
    uint32_t facet = MEM32(0x2010);
    this_argument(facet);
    assert((primary ? lst_AddRef_impl() : d3b_AddRef_impl()) == 3);
    assert((primary ? lst_Release_impl() : d3b_Release_impl()) == 2);
    uint32_t notify = 0;
    if (!primary) {
        memcpy(GPTR(0x2000), IID_IDirectSoundNotify, 16);
        assert(buffer_query(b, 0x2000, 0x2010) == 0);
        notify = MEM32(0x2010);
    }
    if (owner_first) { this_argument(base); assert(dsb_Release_impl() != 0); }
    this_argument(facet);
    if (primary) lst_Release_impl(); else d3b_Release_impl();
    if (notify) { this_argument(notify); ntf_Release_impl(); }
    if (!owner_first) { this_argument(base); assert(dsb_Release_impl() == 0); }
    assert(!buffers);
    for (unsigned i = 1; i < MAX_OBJECTS; ++i) assert(!objects[i]);
    assert(MEM32(base) == 0 && MEM32(facet) == 0);
    if (notify) assert(MEM32(notify) == 0);
}

static void thread_cycle(int close_first)
{
    void_thread *t = thread_new(0);
    t->start = 0x12345678;
    t->guest_owned = 1;
    t->object = void_object_new(VOID_OBJ_THREAD, 0);
    t->object->data = t;
    t->object->destroy = thread_object_destroy;
    t->object->refs = 2;
    uint32_t handle = void_handle_new(t->object);
    assert(t->static_tls_allocations[0] && t->static_tls_allocations[1]);
    assert(!(MEM32(t->static_tls) & 63) && !(MEM32(t->static_tls + 4) & 127));
    if (close_first) assert(void_handle_close(handle));
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, thread_main, t));
    assert(!pthread_join(worker, NULL));
    assert(!all_threads && !t_self);
    if (!close_first) {
        void_object *o = void_handle_get(handle, VOID_OBJ_THREAD);
        assert(o && o->signaled && o->count == 37);
        assert(!t->stack_lo && !t->tib && !t->static_tls);
        assert(void_handle_close(handle));
    }
}

static void *host_callback(void *unused)
{
    (void)unused;
    void_machine_enter();
    void_machine_leave();
    return NULL;
}

static uint32_t timer_start(uint32_t delay)
{
    MEM32(g_esp + 4) = delay; MEM32(g_esp + 8) = 0; MEM32(g_esp + 12) = 0x87654321;
    MEM32(g_esp + 16) = timer_worker_count; MEM32(g_esp + 20) = 0x101;
    uint32_t id = timeSetEvent_impl();
    assert(id); return id;
}

static void *timer_reuse(void *unused)
{
    (void)unused;
    uint32_t watermark = 0;
    for (unsigned i = 0; i < 8; ++i) {
        void_machine_enter();
        if (!i) watermark = next_allocation;
        uint32_t old = timer_start(1);
        /* Hold the machine until the old callback is definitely queued for it. */
        for (unsigned wait = 0; !machine_contended() && wait < 2000; ++wait) usleep(1000);
        assert(machine_contended());
        MEM32(g_esp + 4) = old;
        assert(!timeKillEvent_impl());
        assert(timer_start(1) == old); /* Immediate reuse of the same public timer ID. */
        void_machine_leave();
        assert(!pthread_join(timer_workers[2 * i], NULL));
        assert(!pthread_join(timer_workers[2 * i + 1], NULL));
        assert(timer_callbacks == i + 1);
        next_allocation = watermark;
    }
    return NULL;
}

int main(void)
{
    /* Vtables and the process PEB are deliberately shared process resources. */
    audio_cycle(0, 0); audio_cycle(1, 0);
    make_peb();
    static_tls_count = 2;
    static_tls_modules[0] = (static_tls_module){.total_size = 37, .alignment = 64};
    static_tls_modules[1] = (static_tls_module){.total_size = 277, .alignment = 128};
    uint32_t baseline_count = live_allocations, watermark = next_allocation;
    uint64_t baseline_bytes = live_bytes;
    for (unsigned i = 0; i < 2048; ++i) {
        audio_cycle(i & 1, (i >> 1) & 1);
        thread_cycle(i & 1);
        pthread_t callback;
        assert(!pthread_create(&callback, NULL, host_callback, NULL));
        assert(!pthread_join(callback, NULL));
        assert(!all_threads);
        assert(live_allocations == baseline_count && live_bytes == baseline_bytes);
        next_allocation = watermark;
    }
    pthread_t timers;
    assert(!pthread_create(&timers, NULL, timer_reuse, NULL));
    assert(!pthread_join(timers, NULL));
    assert(!all_threads && live_allocations == baseline_count && live_bytes == baseline_bytes);
    puts("PASS: 2048 audio/thread/callback cycles reclaim COM/PCM/stack/TIB/TLS; rapid timer kill/reuse cancels queued old callbacks, supports self-kill and frees worker storage");
    return 0;
}

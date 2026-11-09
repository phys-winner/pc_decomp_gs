#include <stdint.h>

/* Generated code addresses guest memory as g_guest_mem + VA. ARM64 uses an
 * absolute symbol; ARM32 stores the kernel-selected window in a pointer. */
#if defined(__arm__)
__attribute__((visibility("default"))) uint8_t *g_guest_mem;
#else
__asm__(".globl g_guest_mem\n\t.hidden g_guest_mem\n\t.set g_guest_mem, 0x2000000000");
#endif

#include "host.h"
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define WS2 "ws2_32.dll"
#define SOCKET_ERROR_ 0xFFFFFFFFu

static __thread int wsa_error;
#define NETLOG(...) do { } while (0)

static int wsa_from_errno(int error)
{
    switch (error) {
    case EAGAIN: return 10035;
    case ECONNRESET: return 10054;
    case EMSGSIZE: return 10040;
    case EINPROGRESS: return 10036;
    case EALREADY: return 10037;
    case ENOTSOCK: return 10038;
    case EDESTADDRREQ: return 10039;
    case EFAULT: return 10014;
    case EINVAL: return 10022;
    case ENOBUFS: case ENOMEM: return 10055;
    case ENOTCONN: return 10057;
    case ECONNREFUSED: return 10061;
    case ETIMEDOUT: return 10060;
    default: return 10050;
    }
}

static int wsa_fail(int error)
{
    wsa_error = wsa_from_errno(error);
    return -1;
}

WINAPI_EXPORT(WS2, WSAStartup, 2)
{
    (void)ARG(0);
    if (!ARG(1) || !void_vm_writable(ARG(1), 400)) return 10014;
    memset(GPTR(ARG(1)), 0, 400);
    wsa_error = 0;
    return 0;
}

WINAPI_EXPORT(WS2, WSACleanup, 0) { return 0; }
WINAPI_EXPORT(WS2, WSAGetLastError, 0) { return (uint32_t)wsa_error; }
WINAPI_EXPORT(WS2, htons, 1) { return (ARG(0) >> 8) | ((ARG(0) & 0xFFu) << 8); }
WINAPI_EXPORT(WS2, ntohs, 1) { return (ARG(0) >> 8) | ((ARG(0) & 0xFFu) << 8); }

WINAPI_EXPORT(WS2, inet_addr, 1)
{
    char *text = void_astr_utf8(ARG(0));
    uint32_t address = text ? inet_addr(text) : INADDR_NONE;
    free(text);
    return address;
}

WINAPI_EXPORT(WS2, socket, 3)
{
    int fd = socket((int)ARG(0), (int)ARG(1), (int)ARG(2));
    NETLOG("[net] socket %d family %u type %u protocol %u", fd, ARG(0), ARG(1), ARG(2));
    if (fd < 0) return (uint32_t)wsa_fail(errno);
    wsa_error = 0;
    return (uint32_t)fd;
}

WINAPI_EXPORT(WS2, closesocket, 1)
{
    int result = close((int)ARG(0));
    int error = errno;
    NETLOG("[net] close %d result %d error %d caller %08X", ARG(0), result, error, g_cur_func);
    if (result) return (uint32_t)wsa_fail(error);
    wsa_error = 0;
    return 0;
}

WINAPI_EXPORT(WS2, bind, 3)
{
    struct sockaddr_storage address;
    socklen_t size = (socklen_t)ARG(2);
    if (!ARG(1) || size > sizeof address) return (uint32_t)wsa_fail(EFAULT);
    memcpy(&address, GPTR(ARG(1)), size);
    int result = bind((int)ARG(0), (struct sockaddr *)&address, size), error = errno;
    NETLOG("[net] bind %d port %u result %d error %d", ARG(0),
        size >= sizeof(struct sockaddr_in) ? ntohs(((struct sockaddr_in *)&address)->sin_port) : 0, result, error);
    if (result) return (uint32_t)wsa_fail(error);
    wsa_error = 0;
    return 0;
}

WINAPI_EXPORT(WS2, sendto, 6)
{
    uint32_t socket = ARG(0), buffer = ARG(1), length = ARG(2), flags = ARG(3), to = ARG(4);
    socklen_t size = (socklen_t)ARG(5);
    struct sockaddr_storage address;
    const struct sockaddr *target = NULL;
    if (to && size && size <= sizeof address) {
        memcpy(&address, GPTR(to), size);
        target = (const struct sockaddr *)&address;
    } else if (to) {
        size = 0;
    }
    void_blocking_begin();
    ssize_t result = sendto((int)socket, GPTR(buffer), length, (int)flags, target, target ? size : 0);
    int error = errno;
    void_blocking_end();
    NETLOG("[net] send %d bytes %d to port %u result %d error %d", socket, length,
        target && size >= sizeof(struct sockaddr_in) ? ntohs(((struct sockaddr_in *)target)->sin_port) : 0, (int)result, error);
    if (result < 0) return (uint32_t)wsa_fail(error);
    wsa_error = 0;
    return (uint32_t)result;
}

WINAPI_EXPORT(WS2, recvfrom, 6)
{
    uint32_t socket = ARG(0), buffer = ARG(1), length = ARG(2), flags = ARG(3);
    uint32_t from = ARG(4), from_length = ARG(5);
    struct sockaddr_storage address;
    socklen_t size = from_length ? MEM32(from_length) : 0;
    struct sockaddr *source = from ? (struct sockaddr *)&address : NULL;
    if (size > sizeof address) size = sizeof address;
    void_blocking_begin();
    ssize_t result = recvfrom((int)socket, GPTR(buffer), length, (int)flags, source, source ? &size : NULL);
    int error = errno;
    void_blocking_end();
    NETLOG("[net] recv %d result %d error %d", socket, (int)result, error);
    if (result < 0) return (uint32_t)wsa_fail(error);
    if (source) {
        memcpy(GPTR(from), &address, size);
        if (from_length) MEM32(from_length) = size;
    }
    wsa_error = 0;
    return (uint32_t)result;
}

WINAPI_EXPORT(WS2, ioctlsocket, 3)
{
    uint32_t command = ARG(1);
    int value = ARG(2) ? (int)MEM32(ARG(2)) : 0;
    int result;
    if (command == 0x8004667Eu) result = ioctl((int)ARG(0), FIONBIO, &value);
    else if (command == 0x4004667Fu) {
        result = ioctl((int)ARG(0), FIONREAD, &value);
        if (!result && ARG(2)) MEM32(ARG(2)) = (uint32_t)value;
    } else {
        errno = EINVAL;
        result = -1;
    }
    if (result < 0) return (uint32_t)wsa_fail(errno);
    wsa_error = 0;
    return 0;
}

#define WS2_ALIAS(symbol, ordinal) \
    static const void_shim symbol##_##ordinal##_shim VOID_SHIM_SECTION = { WS2, #symbol, ordinal, symbol##_entry };
WS2_ALIAS(closesocket, 3)
WS2_ALIAS(socket, 23)
WS2_ALIAS(bind, 2)
WS2_ALIAS(WSACleanup, 116)
WS2_ALIAS(sendto, 20)
WS2_ALIAS(htons, 9)
WS2_ALIAS(WSAGetLastError, 111)
WS2_ALIAS(ioctlsocket, 10)
WS2_ALIAS(WSAStartup, 115)
WS2_ALIAS(recvfrom, 17)
WS2_ALIAS(inet_addr, 11)
#undef WS2_ALIAS

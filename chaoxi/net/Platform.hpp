#pragma once

#include <cstddef>
#include <cstdint>
#include <cerrno>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef ERROR
#undef ERROR
#endif

namespace chaoxi::net
{
using SocketHandle = SOCKET;
using SignedSize = std::ptrdiff_t;
using SocketLength = int;
using sa_family_t = ADDRESS_FAMILY;
inline constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

inline void ensureNetworkInitialized()
{
    static const bool initialized = [] {
        WSADATA data{};
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)initialized;
}

inline int lastSocketError() noexcept { return ::WSAGetLastError(); }
inline void setLastSocketError(int error) noexcept { ::WSASetLastError(error); }
inline int socketErrorToErrno(int error) noexcept
{
    switch (error)
    {
        case WSAEWOULDBLOCK: return EWOULDBLOCK;
        case WSAEINTR: return EINTR;
        case WSAEINPROGRESS: return EINPROGRESS;
        case WSAECONNABORTED: return ECONNABORTED;
        case WSAECONNRESET: return ECONNRESET;
        case WSAECONNREFUSED: return ECONNREFUSED;
        case WSAETIMEDOUT: return ETIMEDOUT;
        case WSAENOTCONN: return ENOTCONN;
        case WSAEADDRINUSE: return EADDRINUSE;
        case WSAEADDRNOTAVAIL: return EADDRNOTAVAIL;
        case WSAENETUNREACH: return ENETUNREACH;
        case WSAEMFILE: return EMFILE;
        case WSAEINVAL: return EINVAL;
        case WSAENOTSOCK: return ENOTSOCK;
        case WSAEOPNOTSUPP: return EOPNOTSUPP;
        default: return error;
    }
}
}  // namespace chaoxi::net
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace chaoxi::net
{
using SocketHandle = int;
using SignedSize = ssize_t;
using SocketLength = socklen_t;
inline constexpr SocketHandle kInvalidSocket = -1;
inline void ensureNetworkInitialized() noexcept {}
inline int lastSocketError() noexcept { return errno; }
inline void setLastSocketError(int error) noexcept { errno = error; }
inline int socketErrorToErrno(int error) noexcept { return error; }
}  // namespace chaoxi::net
#endif

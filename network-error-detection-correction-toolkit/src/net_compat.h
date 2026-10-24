// net_compat.h - small layer so the same socket code builds on Windows (Winsock)
// and on Linux/macOS (BSD sockets). Only IPv4 UDP is used in this project.
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define CLOSE_SOCKET closesocket
#define BAD_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define CLOSE_SOCKET ::close
#define BAD_SOCKET (-1)
#endif

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

namespace net {

inline bool start() {
#ifdef _WIN32
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
#else
    return true;
#endif
}

inline void stop() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// receive timeout so that blocking recvfrom() returns regularly
inline void setRecvTimeout(sock_t s, int ms) {
#ifdef _WIN32
    DWORD t = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof(t));
#else
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

inline void setBuffers(sock_t s, int bytes) {
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&bytes, sizeof(bytes));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&bytes, sizeof(bytes));
}

// IPv4 address + port
struct Addr {
    sockaddr_in sa;
    Addr() { std::memset(&sa, 0, sizeof(sa)); sa.sin_family = AF_INET; }
    uint32_t ip() const { return ntohl(sa.sin_addr.s_addr); }
    int port() const { return ntohs(sa.sin_port); }
    bool operator==(const Addr &o) const {
        return sa.sin_addr.s_addr == o.sa.sin_addr.s_addr && sa.sin_port == o.sa.sin_port;
    }
    bool operator<(const Addr &o) const {
        if (sa.sin_addr.s_addr != o.sa.sin_addr.s_addr)
            return sa.sin_addr.s_addr < o.sa.sin_addr.s_addr;
        return sa.sin_port < o.sa.sin_port;
    }
    std::string str() const {
        uint32_t a = ip();
        return std::to_string(a >> 24) + "." + std::to_string((a >> 16) & 255) + "." +
               std::to_string((a >> 8) & 255) + "." + std::to_string(a & 255) + ":" +
               std::to_string(port());
    }
};

// resolve "host" (name or dotted IPv4) and port into an Addr
inline bool resolve(const std::string &host, int port, Addr &out) {
    out = Addr();
    out.sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.c_str(), &out.sa.sin_addr) == 1) return true;
    addrinfo hints, *res = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    out.sa.sin_addr = ((sockaddr_in *)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

// create a UDP socket bound to ip:port (port 0 = any free port)
inline sock_t udpSocket(const std::string &ip, int port) {
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == BAD_SOCKET) return BAD_SOCKET;
    Addr a;
    a.sa.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, ip.c_str(), &a.sa.sin_addr);
    if (bind(s, (sockaddr *)&a.sa, sizeof(a.sa)) != 0) {
        CLOSE_SOCKET(s);
        return BAD_SOCKET;
    }
    setBuffers(s, 1 << 21);
    return s;
}

inline int localPort(sock_t s) {
    Addr a;
    socklen_t n = sizeof(a.sa);
    getsockname(s, (sockaddr *)&a.sa, &n);
    return a.port();
}

inline int sendTo(sock_t s, const void *buf, size_t len, const Addr &to) {
    return (int)sendto(s, (const char *)buf, (int)len, 0, (const sockaddr *)&to.sa, sizeof(to.sa));
}

// returns bytes received, 0 on timeout or an ignorable error, -1 on a fatal error
inline int recvFrom(sock_t s, void *buf, size_t cap, Addr &from) {
    socklen_t n = sizeof(from.sa);
    int r = (int)recvfrom(s, (char *)buf, (int)cap, 0, (sockaddr *)&from.sa, &n);
    if (r >= 0) return r;
#ifdef _WIN32
    int e = WSAGetLastError();
    // 10060 timeout, 10054 ICMP port unreachable from an earlier send, 10040 message too big
    if (e == WSAETIMEDOUT || e == WSAECONNRESET || e == WSAEMSGSIZE || e == WSAEWOULDBLOCK) return 0;
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ECONNREFUSED) return 0;
#endif
    return -1;
}

// milliseconds from a steady clock (only differences are meaningful)
inline double nowMs() {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return duration<double, std::milli>(steady_clock::now() - t0).count();
}

// microseconds, wraps every ~71 minutes; used for the timestamp option
inline uint32_t nowUs32() {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return (uint32_t)duration_cast<microseconds>(steady_clock::now() - t0).count();
}

}  // namespace net


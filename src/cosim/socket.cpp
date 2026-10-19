#include "socket.h"

#include <cerrno>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace logic::cosim {

namespace {

#ifdef _WIN32
const intptr_t bad = (intptr_t)INVALID_SOCKET;

void startup()
{
    static bool done = [] {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    (void)done;
}

void close_socket(intptr_t s)
{
    closesocket((SOCKET)s);
}

// 1 readable, 0 timeout, -1 error
int wait_readable(intptr_t s, int timeout_ms)
{
    WSAPOLLFD p{(SOCKET)s, POLLRDNORM, 0};
    int r = WSAPoll(&p, 1, timeout_ms);
    return r < 0 ? -1 : r;
}
#else
const intptr_t bad = -1;

void startup()
{
}

void close_socket(intptr_t s)
{
    close((int)s);
}

int wait_readable(intptr_t s, int timeout_ms)
{
    pollfd p{(int)s, POLLIN, 0};
    int r;
    do {
        r = poll(&p, 1, timeout_ms);
    } while (r < 0 && errno == EINTR);
    return r < 0 ? -1 : r;
}
#endif

}

connection::~connection()
{
    close_socket(s);
}

bool connection::send(const void* data, size_t n)
{
    auto p = (const char*)data;
    while (n > 0) {
#ifdef _WIN32
        int k = ::send((SOCKET)s, p, (int)n, 0);
#else
        auto k = ::send((int)s, p, n, MSG_NOSIGNAL);
#endif
        if (k <= 0) return false;
        p += k;
        n -= (size_t)k;
    }
    return true;
}

bool connection::receive(void* data, size_t n, int timeout_ms)
{
    auto p = (char*)data;
    while (n > 0) {
        if (wait_readable(s, timeout_ms) <= 0) return false;
#ifdef _WIN32
        int k = ::recv((SOCKET)s, p, (int)n, 0);
#else
        auto k = ::recv((int)s, p, n, 0);
#endif
        if (k <= 0) return false;
        p += k;
        n -= (size_t)k;
    }
    return true;
}

bool listener::open(std::string& error)
{
    startup();
    s = (intptr_t)::socket(AF_INET, SOCK_STREAM, 0);
    if (s == bad) {
        error = "can't create a socket";
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t len = sizeof(addr);
#ifdef _WIN32
    auto sock = (SOCKET)s;
#else
    auto sock = (int)s;
#endif
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(sock, 1) != 0 || getsockname(sock, (sockaddr*)&addr, &len) != 0) {
        error = "can't listen on 127.0.0.1";
        return false;
    }
    m_port = ntohs(addr.sin_port);
    return true;
}

listener::~listener()
{
    if (s != bad) close_socket(s);
}

std::unique_ptr<connection> listener::accept(int timeout_ms)
{
    if (s == bad || wait_readable(s, timeout_ms) <= 0) return nullptr;
#ifdef _WIN32
    auto c = (intptr_t)::accept((SOCKET)s, nullptr, nullptr);
#else
    auto c = (intptr_t)::accept((int)s, nullptr, nullptr);
#endif
    if (c == bad) return nullptr;
    int one = 1;
#ifdef _WIN32
    setsockopt((SOCKET)c, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
#else
    setsockopt((int)c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
    return std::unique_ptr<connection>(new connection(c));
}

}

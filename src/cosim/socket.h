#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace logic::cosim {

// a tcp connection on 127.0.0.1
class connection {
public:
    ~connection();
    connection(const connection&) = delete;
    connection& operator=(const connection&) = delete;

    // false if the other side is gone or doesn't answer within the timeout
    bool send(const void* data, size_t n);
    bool receive(void* data, size_t n, int timeout_ms);

private:
    friend class listener;
    explicit connection(intptr_t s)
        : s(s)
    {
    }
    intptr_t s;
};

// waits for one connection on 127.0.0.1 at a free port
class listener {
public:
    // false with an error if it can't listen
    bool open(std::string& error);
    ~listener();

    int port() const { return m_port; }
    // null if nobody connects within the timeout
    std::unique_ptr<connection> accept(int timeout_ms);

private:
    intptr_t s = -1;
    int m_port = 0;
};

}

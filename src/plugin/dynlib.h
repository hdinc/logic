#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace logic::plugin {

// a loaded shared library (.so / .dll)
class dynlib {
public:
    // null with an error if it can't be loaded
    static std::unique_ptr<dynlib> open(const std::filesystem::path& p, std::string& error);
    ~dynlib();

    dynlib(const dynlib&) = delete;
    dynlib& operator=(const dynlib&) = delete;

    // null if the library doesn't export it
    void* symbol(const char* name) const;

private:
    explicit dynlib(void* h)
        : handle(h)
    {
    }

    void* handle;
};

// file extension of shared libraries on this platform, e.g. ".so"
const char* library_extension();

// folder of the running program, empty if unknown
std::filesystem::path executable_dir();

}

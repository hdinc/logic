#include "dynlib.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace logic::plugin {

#ifdef _WIN32

std::unique_ptr<dynlib> dynlib::open(const std::filesystem::path& p, std::string& error)
{
    // the folder of the plugin is searched for the dlls it depends on
    auto h = LoadLibraryExW(p.wstring().c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!h) {
        error = "can't load " + p.string() + " (error " + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
    return std::unique_ptr<dynlib>(new dynlib((void*)h));
}

dynlib::~dynlib()
{
    FreeLibrary((HMODULE)handle);
}

void* dynlib::symbol(const char* name) const
{
    return (void*)GetProcAddress((HMODULE)handle, name);
}

const char* library_extension()
{
    return ".dll";
}

std::filesystem::path executable_dir()
{
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, sizeof(buf) / sizeof(buf[0]));
    if (n == 0 || n == sizeof(buf) / sizeof(buf[0])) return {};
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

#else

std::unique_ptr<dynlib> dynlib::open(const std::filesystem::path& p, std::string& error)
{
    // local: plugins don't see each other's symbols
    auto h = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        auto e = dlerror();
        error = e ? e : "can't load " + p.string();
        return nullptr;
    }
    return std::unique_ptr<dynlib>(new dynlib(h));
}

dynlib::~dynlib()
{
    dlclose(handle);
}

void* dynlib::symbol(const char* name) const
{
    return dlsym(handle, name);
}

const char* library_extension()
{
#ifdef __APPLE__
    return ".dylib";
#else
    return ".so";
#endif
}

std::filesystem::path executable_dir()
{
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : p.parent_path();
}

#endif

}

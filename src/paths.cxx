#include "paths.hxx"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace pluginhost
{
namespace
{
// Per-user data folder: %LOCALAPPDATA% on Windows, $XDG_DATA_HOME or ~/.local/share on Linux.
std::string userDataBase()
{
#ifdef _WIN32
    const char* base = std::getenv("LOCALAPPDATA");
    return std::string(base ? base : ".");
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        if (*xdg)
            return xdg;
    const char* home = std::getenv("HOME");
    return joinPath(home ? home : ".", ".local/share");
#endif
}
}

std::string joinPath(const std::string& dir, const std::string& name)
{
    if (dir.empty())
        return name;
    if (dir.back() == kPathSep)
        return dir + name;
    return dir + kPathSep + name;
}

std::string baseName(const std::string& path)
{
    const std::size_t pos = path.find_last_of(kPathSep);
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string moduleDirectory()
{
#ifdef _WIN32
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&moduleDirectory), &module);
    wchar_t wide[MAX_PATH] = {};
    GetModuleFileNameW(module, wide, MAX_PATH);
    std::wstring w(wide);
    const size_t slash = w.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return std::string();
    w = w.substr(0, slash + 1);
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(n > 0 ? static_cast<size_t>(n) : 0, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &utf8[0], n, nullptr, nullptr);
    return utf8;
#else
    // dladdr gives the path of the shared object that contains moduleDirectory.
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&moduleDirectory), &info) == 0 || info.dli_fname == nullptr)
        return std::string();
    const std::string path(info.dli_fname);
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return std::string();
    return path.substr(0, slash + 1);
#endif
}

std::string pluginsRoot()
{
    if (const char* override = std::getenv("PLUGINHOST_PLUGINS_DIR"))
        if (*override)
            return override;
    return joinPath(joinPath(userDataBase(), "PluginHost"), "plugins");
}

std::string pluginDataDir(const std::string& pluginId)
{
    return joinPath(joinPath(joinPath(userDataBase(), "PluginHost"), "data"), pluginId);
}

std::string fileUrl(const std::string& path)
{
#ifdef _WIN32
    std::string url = "file:///";
    for (char c : path)
        url += (c == '\\') ? '/' : c;
    return url;
#else
    return "file://" + path; // path already starts with '/'
#endif
}
}

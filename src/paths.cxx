#include "paths.hxx"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pluginhost
{
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
    return std::string(); // stage 10
#endif
}

std::string pluginsRoot()
{
    if (const char* override = std::getenv("PLUGINHOST_PLUGINS_DIR"))
        if (*override)
            return override;
    const char* base = std::getenv("LOCALAPPDATA");
    return std::string(base ? base : ".") + "\\PluginHost\\plugins";
}

std::string pluginDataDir(const std::string& pluginId)
{
    const char* base = std::getenv("LOCALAPPDATA");
    return std::string(base ? base : ".") + "\\PluginHost\\data\\" + pluginId;
}

std::string fileUrl(const std::string& path)
{
    std::string url = "file:///";
    for (char c : path)
        url += (c == '\\') ? '/' : c;
    return url;
}
}

#pragma once

#include <string>

namespace pluginhost
{
// Separator used in paths the host builds: '\' on Windows, '/' elsewhere.
#ifdef _WIN32
constexpr char kPathSep = '\\';
#else
constexpr char kPathSep = '/';
#endif

// Joins a folder and a name with exactly one separator. An empty folder returns the name.
std::string joinPath(const std::string& dir, const std::string& name);

// Last component of a path (the folder or file name).
std::string baseName(const std::string& path);

// Directory that contains the pluginhost library (the unpacked extension root), with a trailing separator.
// Empty when it cannot be found.
std::string moduleDirectory();

// Root of installed plugin packages: %LOCALAPPDATA%\PluginHost\plugins on Windows,
// $XDG_DATA_HOME/PluginHost/plugins (or ~/.local/share) on Linux. PLUGINHOST_PLUGINS_DIR overrides it (tests).
std::string pluginsRoot();

// Per-plugin browser data folder: <user data>/PluginHost/data/<pluginId>
std::string pluginDataDir(const std::string& pluginId);

// Converts a local path to a file URL.
std::string fileUrl(const std::string& path);
}

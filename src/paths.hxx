#pragma once

#include <string>

namespace pluginhost
{
// Directory that contains pluginhost.dll (the unpacked extension root), with a trailing separator.
// On non-Windows builds this returns an empty string until stage 10 is implemented.
std::string moduleDirectory();

// Root of installed plugin packages: %LOCALAPPDATA%\PluginHost\plugins
// (or PLUGINHOST_PLUGINS_DIR when set, which the tests use).
std::string pluginsRoot();

// Per-plugin browser data folder: <pluginsRoot>\..\data\<pluginId>
std::string pluginDataDir(const std::string& pluginId);

// Converts a Windows path to a file:/// URL.
std::string fileUrl(const std::string& path);
}

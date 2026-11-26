#pragma once

#include <string>
#include <vector>

namespace pluginhost
{
// Subset of an ONLYOFFICE-style plugin config.json that the prototype reads.
struct PluginConfig
{
    std::string guid;        // e.g. "asc.{GUID}" as written in the package
    std::string name;
    std::string version;
    std::string installDir;  // folder that contains config.json
    std::vector<std::string> variationUrls; // relative to installDir
    bool isVisual = true;
    bool isSystem = false;
    // Host extension (not in ONLYOFFICE configs): "hostPermissions": ["document.read", "document.edit", "menu"].
    // Missing or empty means the plugin may do nothing in the document. Prototype policy, see REVIEW_AND_PLAN.md.
    std::vector<std::string> permissions;
};

// Reads installDir\config.json. Returns false and fills error on failure.
bool loadPluginConfig(const std::string& installDir, PluginConfig& out, std::string& error);

// Lists package folders under root that contain a readable config.json, sorted by folder name.
std::vector<PluginConfig> scanPlugins(const std::string& root);
}

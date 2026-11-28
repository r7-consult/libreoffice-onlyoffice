#include "plugin_config.hxx"

#include "json_mini.hxx"
#include "paths.hxx"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace pluginhost
{
namespace
{
bool readFile(const std::string& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::stringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}
}

bool loadPluginConfig(const std::string& installDir, PluginConfig& out, std::string& error)
{
    std::string text;
    const std::string path = joinPath(installDir, "config.json");
    if (!readFile(path, text))
    {
        error = "cannot read " + path;
        return false;
    }
    JsonValue root;
    if (!parseJson(text, root) || !root.isObject())
    {
        error = "config.json is not a valid JSON object: " + path;
        return false;
    }

    out = PluginConfig();
    out.installDir = installDir;
    out.guid = root.getString("guid");
    out.name = root.getString("name");
    out.version = root.getString("version");
    if (out.guid.empty())
    {
        error = "config.json has no guid: " + path;
        return false;
    }

    if (const JsonValue* isVisual = root.get("isVisual"))
        if (isVisual->kind == JsonValue::Kind::Bool)
            out.isVisual = isVisual->boolean;
    if (const JsonValue* isSystem = root.get("isSystem"))
        if (isSystem->kind == JsonValue::Kind::Bool)
            out.isSystem = isSystem->boolean;

    if (const JsonValue* perms = root.get("hostPermissions"))
    {
        out.permissionsDeclared = true;
        for (const JsonValue& p : perms->array)
            if (p.isString())
                out.permissions.push_back(p.str);
    }

    // variations[].url is the entry page; the prototype uses the first one.
    if (const JsonValue* variations = root.get("variations"))
    {
        for (const JsonValue& v : variations->array)
        {
            const std::string url = v.getString("url");
            if (!url.empty())
                out.variationUrls.push_back(url);
        }
    }
    if (out.variationUrls.empty())
    {
        error = "config.json has no variations[].url: " + path;
        return false;
    }
    error.clear();
    return true;
}

std::vector<PluginConfig> scanPlugins(const std::string& root)
{
    std::vector<PluginConfig> result;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(root, ec))
        return result;
    std::vector<std::string> folders;
    for (const fs::directory_entry& entry : fs::directory_iterator(root, ec))
    {
        if (entry.is_directory(ec))
            folders.push_back(entry.path().filename().string());
    }
    std::sort(folders.begin(), folders.end());
    for (const std::string& f : folders)
    {
        PluginConfig cfg;
        std::string err;
        if (loadPluginConfig(joinPath(root, f), cfg, err))
            result.push_back(cfg);
    }
    return result;
}
}

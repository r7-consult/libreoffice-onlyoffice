#include "protocol_adapter.hxx"

#include "command_engine.hxx"
#include "context_menu.hxx"
#include "document_bridge.hxx"
#include "host_service.hxx"
#include "json_mini.hxx"
#include "log.hxx"
#include "paths.hxx"
#include "plugin_config.hxx"

#include <com/sun/star/uno/Exception.hpp>
#include <rtl/ustring.hxx>

#include <cstdlib>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pluginhost
{
ProtocolAdapter& ProtocolAdapter::instance()
{
    static ProtocolAdapter s;
    return s;
}

std::string ProtocolAdapter::runtimeScriptPath()
{
    const std::string dir = moduleDirectory();
    return dir.empty() ? std::string() : dir + "content" + kPathSep + "runtime.js";
}

namespace
{
// config.json is read once per plugin per process (it was read from disk on every message).
// A changed package needs a LibreOffice restart; fine for the prototype.
const PluginConfig* configOf(const std::string& pluginId)
{
    static std::map<std::string, std::pair<bool, PluginConfig>> cache;
    auto it = cache.find(pluginId);
    if (it == cache.end())
    {
        PluginConfig cfg;
        std::string err;
        const bool ok = loadPluginConfig(joinPath(pluginsRoot(), pluginId), cfg, err);
        if (!ok)
            logEvent("protocol", "config_error", pluginId + "|" + err);
        it = cache.emplace(pluginId, std::make_pair(ok, cfg)).first;
    }
    return it->second.first ? &it->second.second : nullptr;
}

bool hasPermission(const std::string& pluginId, const std::string& permission)
{
    const PluginConfig* cfg = configOf(pluginId);
    if (!cfg)
        return false;
    // ADR-002, option A (user decision): no hostPermissions key means every permission. A declared list narrows it.
    if (!cfg->permissionsDeclared)
        return true;
    for (const std::string& p : cfg->permissions)
        if (p == permission)
            return true;
    return false;
}
}

void ProtocolAdapter::onPageMessage(const std::string& pluginId, const std::string& json)
{
    try
    {
        dispatchMessage(pluginId, json);
    }
    catch (const std::exception& e)
    {
        logEvent("protocol", "error", pluginId + "|" + e.what());
    }
    catch (const css::uno::Exception& e)
    {
        logEvent("protocol", "error",
                 pluginId + "|UNO: " + rtl::OUStringToOString(e.Message, RTL_TEXTENCODING_UTF8).getStr());
    }
    catch (...)
    {
        logEvent("protocol", "error", pluginId + "|unknown exception");
    }
}

void ProtocolAdapter::dispatchMessage(const std::string& pluginId, const std::string& json)
{
    JsonValue msg;
    if (!parseJson(json, msg) || !msg.isObject())
    {
        logEvent("protocol", "error", "malformed message from " + pluginId);
        return;
    }
    const std::string type = msg.getString("type");
    const JsonValue* idValue = msg.get("id");
    const long long id = (idValue && idValue->kind == JsonValue::Kind::Number) ? static_cast<long long>(idValue->number) : 0;

    const char* needed = type == "executeMethod" ? "document.read"
                         : type == "callCommand" ? "document.edit"
                         : type == "contextMenu" ? "menu"
                                                 : nullptr;
    if (needed && !hasPermission(pluginId, needed))
    {
        logEvent("protocol", "denied", pluginId + "|" + type + "|needs " + needed);
        if (type == "executeMethod" || type == "callCommand")
            sendResult(pluginId, type == "executeMethod" ? "method_result" : "command_result", id, "null",
                       std::string("PermissionDenied: plugin lacks ") + needed);
        return;
    }

    if (type == "ready")
        handleReady(pluginId);
    else if (type == "executeMethod")
        handleExecuteMethod(pluginId, id, msg.getString("name"), [&]() {
            const JsonValue* args = msg.get("args");
            if (!args)
                return std::string("[]");
            // Re-serialise the args array: only scalars and arrays are used by the prototype.
            std::string out = "[";
            for (size_t i = 0; i < args->array.size(); ++i)
            {
                const JsonValue& a = args->array[i];
                if (i)
                    out += ",";
                if (a.isString())
                    out += "\"" + jsonEscape(a.str) + "\"";
                else if (a.kind == JsonValue::Kind::Number)
                    out += formatJsonNumber(a.number);
                else if (a.kind == JsonValue::Kind::Bool)
                    out += a.boolean ? "true" : "false";
                else
                    out += "null";
            }
            return out + "]";
        }());
    else if (type == "callCommand")
    {
        // isCalc defaults to true (ONLYOFFICE); only an explicit false skips the recalculation.
        const JsonValue* calc = msg.get("isCalc");
        const bool recalc = !(calc && calc->kind == JsonValue::Kind::Bool && !calc->boolean);
        handleCallCommand(pluginId, id, msg.getString("source"), msg.getString("scope"), recalc);
    }
    else if (type == "contextMenu")
    {
        std::vector<std::pair<std::string, std::string>> items;
        if (const JsonValue* list = msg.get("items"))
            for (const JsonValue& it : list->array)
                items.emplace_back(it.getString("id"), it.getString("text"));
        logEvent("protocol", "contextMenu", pluginId + "|items=" + std::to_string(items.size()));
        setContextMenuItems(pluginId, std::move(items));
    }
    else if (type == "resize")
        logEvent("protocol", "resize", pluginId);
    else if (type == "close")
    {
        // ONLYOFFICE executeCommand("close"): hide the plugin; the session stays (no reload on reopen).
        logEvent("protocol", "close", pluginId);
        if (WebHost* web = HostService::instance().find(pluginId))
            web->setVisible(false);
    }
    else
        logEvent("protocol", "unknown", type);
}

void ProtocolAdapter::handleReady(const std::string& pluginId)
{
    // Handshake order (plan, stage 4): initialize -> plugin_init -> init.
    std::string guid = pluginId;
    std::string name;
    std::string version;
    if (const PluginConfig* cfg = configOf(pluginId))
    {
        guid = cfg->guid;
        name = cfg->name;
        version = cfg->version;
    }
    // testMode lets the test plugin run its scripted checks (set by PLUGINHOST_TEST_MODE=1).
    // Test builds only (PH_TEST_HOOKS): a release build must not change behaviour from an environment variable.
    bool isTest = false;
#ifdef PH_TEST_HOOKS
    const char* testMode = std::getenv("PLUGINHOST_TEST_MODE");
    isTest = testMode && std::string(testMode) == "1";
#endif
    sendRaw(pluginId, "{\"type\":\"initialize\",\"guid\":\"" + jsonEscape(guid) + "\",\"name\":\"" +
                          jsonEscape(name) + "\",\"version\":\"" + jsonEscape(version) +
                          "\",\"testMode\":" + (isTest ? "true" : "false") + "}");
    sendRaw(pluginId, "{\"type\":\"plugin_init\",\"data\":{}}");
    sendRaw(pluginId, "{\"type\":\"init\",\"data\":\"\"}"); // ONLYOFFICE init(text): empty text
    logEvent("protocol", "handshake", pluginId);
}

void ProtocolAdapter::handleExecuteMethod(const std::string& pluginId, long long id, const std::string& name,
                                          const std::string& argsJson)
{
    try
    {
        const std::string result = callDocumentMethod(name, argsJson);
        sendResult(pluginId, "method_result", id, result, std::string());
    }
    catch (const std::exception& e)
    {
        sendResult(pluginId, "method_result", id, "null", e.what());
    }
    catch (const css::uno::Exception& e)
    {
        // A UNO exception is not a std::exception. Without this catch it would unwind through C code and end the process.
        sendResult(pluginId, "method_result", id, "null",
                   std::string("UNO error: ") + rtl::OUStringToOString(e.Message, RTL_TEXTENCODING_UTF8).getStr());
    }
    catch (...)
    {
        sendResult(pluginId, "method_result", id, "null", "internal error");
    }
}

void ProtocolAdapter::handleCallCommand(const std::string& pluginId, long long id, const std::string& source,
                                        const std::string& scopeJson, bool recalc)
{
    try
    {
        const CommandOutcome outcome = CommandEngine::instance().run(pluginId, source, scopeJson, recalc);
        if (outcome.ok)
            sendResult(pluginId, "command_result", id, outcome.resultJson, std::string());
        else
            sendResult(pluginId, "command_result", id, "null", outcome.error);
    }
    catch (const std::exception& e)
    {
        sendResult(pluginId, "command_result", id, "null", e.what());
    }
    catch (const css::uno::Exception& e)
    {
        sendResult(pluginId, "command_result", id, "null",
                   std::string("UNO error: ") + rtl::OUStringToOString(e.Message, RTL_TEXTENCODING_UTF8).getStr());
    }
    catch (...)
    {
        sendResult(pluginId, "command_result", id, "null", "internal error");
    }
}

void ProtocolAdapter::sendResult(const std::string& pluginId, const char* type, long long id,
                                 const std::string& resultJson, const std::string& error)
{
    std::string out = std::string("{\"type\":\"") + type + "\",\"id\":" + std::to_string(id);
    if (!error.empty())
        out += ",\"error\":\"" + jsonEscape(error) + "\"";
    else
        out += ",\"result\":" + resultJson;
    out += "}";
    sendRaw(pluginId, out);
}

void ProtocolAdapter::sendEvent(const std::string& pluginId, const std::string& name, const std::string& dataJson)
{
    sendRaw(pluginId, "{\"type\":\"event\",\"name\":\"" + jsonEscape(name) + "\",\"data\":" + dataJson + "}");
}

void ProtocolAdapter::sendRaw(const std::string& pluginId, const std::string& json)
{
    if (WebHost* web = HostService::instance().find(pluginId))
        web->postMessage(json);
    else
        logEvent("protocol", "no_host", pluginId);
}
}

namespace pluginhost
{
// Public form of hasPermission, used by the bridge policy in command_engine.cxx.
bool pluginHasPermission(const std::string& pluginId, const std::string& permission)
{
    return hasPermission(pluginId, permission);
}
}

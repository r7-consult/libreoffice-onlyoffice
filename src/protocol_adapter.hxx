#pragma once

// Host side of the postMessage protocol used by extension/content/runtime.js.
// Page -> host: ready, executeMethod, callCommand, resize.
// Host -> page: initialize, plugin_init, init, method_result, command_result.

#include <string>

namespace pluginhost
{
class ProtocolAdapter
{
public:
    static ProtocolAdapter& instance();

    // Path of runtime.js inside the installed extension (empty if unknown).
    static std::string runtimeScriptPath();

    // Handles one JSON message from the page of pluginId. Runs on the main thread.
    void onPageMessage(const std::string& pluginId, const std::string& json);

    // Sends {type:"event", name, data} to the page; dataJson must be valid JSON.
    void sendEvent(const std::string& pluginId, const std::string& name, const std::string& dataJson);

private:
    ProtocolAdapter() = default;

    // The real dispatch. onPageMessage wraps it so that no exception (std or UNO) reaches the caller, which is C
    // code (a WebKit signal handler or a COM callback) and cannot unwind.
    void dispatchMessage(const std::string& pluginId, const std::string& json);
    void handleReady(const std::string& pluginId);
    void handleExecuteMethod(const std::string& pluginId, long long id, const std::string& name, const std::string& argsJson);
    // recalc: the page's isCalc flag. False skips the recalculation after the command.
    void handleCallCommand(const std::string& pluginId, long long id, const std::string& source, const std::string& scopeJson,
                           bool recalc);

    // Sends a complete JSON object to the page. resultJson must already be valid JSON.
    void sendResult(const std::string& pluginId, const char* type, long long id, const std::string& resultJson, const std::string& error);
    void sendRaw(const std::string& pluginId, const std::string& json);
};
}

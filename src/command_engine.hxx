#pragma once

// Runs plugin callCommand() sources inside QuickJS (stage 5).
// One JSRuntime + JSContext per plugin. The global "Api" object is defined by
// extension\js\api_shim.js; it calls the native dispatcher __ph_call(name, argsJson)
// implemented in api_bridge.cxx.

#include <string>

namespace pluginhost
{
struct CommandOutcome
{
    bool ok = false;
    std::string resultJson; // valid JSON when ok
    std::string error;      // message when !ok; "TimeoutError: ..." when the watchdog fired
};

class CommandEngine
{
public:
    static CommandEngine& instance();

    // source: JavaScript expression that evaluates to a function, e.g. "(function(){ return 1; })()".
    // Runs with an Undo context and locked controllers; one Undo step reverts all of it.
    // scopeJson: JSON text of the page's Asc.scope, exposed to the command as Asc.scope ("{}" if none).
    // recalc: the page's isCalc flag; false skips the recalculation after a successful command.
    CommandOutcome run(const std::string& pluginId, const std::string& source, const std::string& scopeJson,
                       bool recalc = true);

    // Watchdog limit in milliseconds (default 5000). Not changed by plugins.
    void setTimeoutMs(long long ms) { m_timeoutMs = ms; }
    long long timeoutMs() const { return m_timeoutMs; }

    // Drops the engine of one plugin (used on uninstall and in tests).
    void dispose(const std::string& pluginId);

private:
    CommandEngine() = default;
    long long m_timeoutMs = 5000;
};
}

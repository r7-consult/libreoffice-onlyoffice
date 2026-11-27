#include "command_engine.hxx"

#include "log.hxx"
#include "paths.hxx"

#include <quickjs.h>

#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

// Native dispatcher, defined in api_bridge.cxx.
namespace pluginhost
{
std::string nativeCall(const std::string& name, const std::string& argsJson);
void beginUndoContext();
void endUndoContext(bool rollback);
void lockActiveDocument(bool lock);
void recalculateActiveDocument();
}

namespace pluginhost
{
namespace
{
struct Engine
{
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;
    // max() disables the watchdog while the shim loads; run() sets the real deadline and resets it after.
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
    bool watchdogFired = false;
};

std::map<std::string, Engine>& engines()
{
    static std::map<std::string, Engine> m;
    return m;
}

int interruptCallback(JSRuntime* /*rt*/, void* opaque)
{
    auto* e = static_cast<Engine*>(opaque);
    if (std::chrono::steady_clock::now() > e->deadline)
    {
        e->watchdogFired = true;
        return 1; // non-zero aborts the running script
    }
    return 0;
}

// __ph_call(name, argsJson) -> resultJson (string). Errors become JS exceptions.
JSValue jsPhCall(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv)
{
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "__ph_call expects (name, argsJson)");
    const char* name = JS_ToCString(ctx, argv[0]);
    const char* args = JS_ToCString(ctx, argv[1]);
    if (!name || !args)
    {
        if (name) JS_FreeCString(ctx, name);
        if (args) JS_FreeCString(ctx, args);
        return JS_ThrowTypeError(ctx, "__ph_call: bad arguments");
    }
    std::string result;
    std::string error;
    try
    {
        result = nativeCall(name, args);
    }
    catch (const std::exception& e)
    {
        error = e.what();
    }
    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, args);
    if (!error.empty())
        return JS_ThrowTypeError(ctx, "%s", error.c_str());
    return JS_NewStringLen(ctx, result.data(), result.size());
}

std::string readShim()
{
    const std::string dir = moduleDirectory();
    if (dir.empty())
        return std::string();
    std::ifstream in(dir + "js\\api_shim.js", std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

Engine& engineFor(const std::string& pluginId)
{
    auto it = engines().find(pluginId);
    if (it != engines().end())
        return it->second;

    // std::map nodes do not move, so the pointer given to the interrupt handler stays valid.
    auto& stored = engines()[pluginId];
    stored.rt = JS_NewRuntime();
    // The engine runs on the LibreOffice main thread: a runaway plugin must get a JS exception,
    // never exhaust the process stack or heap.
    JS_SetMaxStackSize(stored.rt, 256 * 1024);
    JS_SetMemoryLimit(stored.rt, 256 * 1024 * 1024);
    stored.ctx = JS_NewContext(stored.rt);
    JS_SetInterruptHandler(stored.rt, interruptCallback, &stored);

    JSValue global = JS_GetGlobalObject(stored.ctx);
    JSValue fn = JS_NewCFunction(stored.ctx, jsPhCall, "__ph_call", 2);
    JS_SetPropertyStr(stored.ctx, global, "__ph_call", fn);
    JS_FreeValue(stored.ctx, global);

    const std::string shim = readShim();
    if (!shim.empty())
    {
        JSValue r = JS_Eval(stored.ctx, shim.data(), shim.size(), "api_shim.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(r))
            logEvent("engine", "error", "api_shim.js failed for " + pluginId);
        JS_FreeValue(stored.ctx, r);
    }
    else
        logEvent("engine", "error", "api_shim.js not found; Api is unavailable");
    return stored;
}

// RAII: controllers stay locked and the undo context stays open for the whole command.
class CommandScope
{
public:
    CommandScope()
    {
        lockActiveDocument(true);
    }
    ~CommandScope()
    {
        lockActiveDocument(false);
    }
};
}

CommandEngine& CommandEngine::instance()
{
    static CommandEngine s;
    return s;
}

CommandOutcome CommandEngine::run(const std::string& pluginId, const std::string& source, const std::string& scopeJson)
{
    CommandOutcome out;
    Engine& e = engineFor(pluginId);
    e.deadline = std::chrono::steady_clock::time_point::max(); // stale deadline from the last run must not fire here

    // Asc.scope: parsed as JSON by the engine itself (no string splicing into code).
    {
        const std::string text = scopeJson.empty() ? std::string("{}") : scopeJson;
        JSValue scope = JS_ParseJSON(e.ctx, text.data(), text.size(), "scope.json");
        if (JS_IsException(scope))
        {
            JS_FreeValue(e.ctx, JS_GetException(e.ctx));
            scope = JS_NewObject(e.ctx);
        }
        JSValue global = JS_GetGlobalObject(e.ctx);
        JSValue asc = JS_NewObject(e.ctx);
        JS_SetPropertyStr(e.ctx, asc, "scope", scope); // takes ownership of scope
        JS_SetPropertyStr(e.ctx, global, "Asc", asc);  // takes ownership of asc
        JS_FreeValue(e.ctx, global);
    }

    e.watchdogFired = false;
    e.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_timeoutMs);

    // The page sends a function expression; JSON.stringify keeps the result transferable.
    const std::string wrapped = "JSON.stringify((" + source + "))";

    {
        CommandScope scope;
        beginUndoContext();
        JSValue v = JS_Eval(e.ctx, wrapped.data(), wrapped.size(), "command.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(v))
        {
            JSValue exc = JS_GetException(e.ctx);
            const char* msg = JS_ToCString(e.ctx, exc);
            out.error = e.watchdogFired ? std::string("TimeoutError: command exceeded ") + std::to_string(m_timeoutMs) + " ms"
                                        : std::string(msg ? msg : "script error");
            if (msg)
                JS_FreeCString(e.ctx, msg);
            JS_FreeValue(e.ctx, exc);
            JS_FreeValue(e.ctx, v);
            // The whole command is one Undo step: roll it back if it changed anything.
            endUndoContext(true);
            logEvent("command", e.watchdogFired ? "timeout" : "error", pluginId + "|" + out.error);
            return out;
        }
        // JSON.stringify(undefined) is undefined (not a string): a command that returns nothing
        // must still produce valid JSON, or the reply message to the page is malformed.
        if (JS_IsString(v))
        {
            const char* text = JS_ToCString(e.ctx, v);
            out.resultJson = text ? text : "null";
            if (text)
                JS_FreeCString(e.ctx, text);
        }
        else
            out.resultJson = "null";
        JS_FreeValue(e.ctx, v);
        endUndoContext(false);
        recalculateActiveDocument();
    }
    out.ok = true;
    logEvent("command", "ok", pluginId);
    return out;
}

void CommandEngine::dispose(const std::string& pluginId)
{
    auto it = engines().find(pluginId);
    if (it == engines().end())
        return;
    JS_FreeContext(it->second.ctx);
    JS_FreeRuntime(it->second.rt);
    engines().erase(it);
}
}

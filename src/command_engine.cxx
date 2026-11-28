#include "command_engine.hxx"

#include "bridge_native.hxx"
#include "host_service.hxx"
#include "log.hxx"
#include "paths.hxx"

#include <com/sun/star/uno/Exception.hpp>
#include <quickjs.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

// Helpers defined in api_bridge.cxx and protocol_adapter.cxx.
namespace pluginhost
{
std::string nativeCall(const std::string& name, const std::string& argsJson);
void beginUndoContext();
void endUndoContext(bool rollback);
void lockActiveDocument(bool lock);
void recalculateActiveDocument();
css::uno::Reference<css::uno::XInterface> activeDocumentRoot();
bool pluginHasPermission(const std::string& pluginId, const std::string& permission);
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
    // Factory from extension/js/bridge_boot.js: makeBridge(policy) returns the bridge for one command.
    JSValue makeBridge = JS_UNDEFINED;
    // The bridge object of the command that is running (our own reference). BridgeDisposer ends it after the command.
    JSValue currentBridge = JS_UNDEFINED;
};

std::map<std::string, Engine>& engines()
{
    static std::map<std::string, Engine> m;
    return m;
}

int interruptCallback(JSContext* /*ctx*/, void* opaque) // JSInterruptHandler in quickjs-ng 0.18
{
    auto* e = static_cast<Engine*>(opaque);
    if (std::chrono::steady_clock::now() > e->deadline)
    {
        e->watchdogFired = true;
        return 1; // non-zero aborts the running script
    }
    return 0;
}

// The bridge session of the command that is running. Set by BridgeScope in run(), null otherwise.
BridgeSession* g_activeBridge = nullptr;

class BridgeScope
{
public:
    explicit BridgeScope(BridgeSession* session)
    {
        g_activeBridge = session;
    }
    ~BridgeScope()
    {
        g_activeBridge = nullptr;
    }
    BridgeScope(const BridgeScope&) = delete;
    BridgeScope& operator=(const BridgeScope&) = delete;
};

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
    catch (const css::uno::Exception& e)
    {
        // UNO exceptions do not derive from std::exception. Letting one escape would unwind through QuickJS's C frames.
        error = std::string("UNO error: ") + rtl::OUStringToOString(e.Message, RTL_TEXTENCODING_UTF8).getStr();
    }
    catch (...)
    {
        error = "internal error";
    }
    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, args);
    if (!error.empty())
        return JS_ThrowTypeError(ctx, "%s", error.c_str());
    return JS_NewStringLen(ctx, result.data(), result.size());
}

// __ph_bridge(op, argsJson) -> resultJson. Only the bridge object calls it (it is passed to bridge_boot.js and is
// not a global), so plugin code cannot reach it directly. Works only while a command runs.
JSValue jsPhBridge(JSContext* ctx, JSValueConst /*thisVal*/, int argc, JSValueConst* argv)
{
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "__ph_bridge expects (op, argsJson)");
    const char* op = JS_ToCString(ctx, argv[0]);
    const char* args = JS_ToCString(ctx, argv[1]);
    if (!op || !args)
    {
        if (op) JS_FreeCString(ctx, op);
        if (args) JS_FreeCString(ctx, args);
        return JS_ThrowTypeError(ctx, "__ph_bridge: bad arguments");
    }
    std::string result;
    std::string error;
    if (!g_activeBridge)
    {
        error = "bridge: no command is running";
    }
    else
    {
        try
        {
            result = g_activeBridge->dispatch(op, args);
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        catch (const css::uno::Exception& e)
        {
            // "text" + const char* would add two pointers, so the left operand must be a std::string.
            error = std::string("bridge: UNO error: ") +
                    rtl::OUStringToOString(e.Message, RTL_TEXTENCODING_UTF8).getStr();
        }
        catch (...)
        {
            error = "bridge: internal error";
        }
    }
    JS_FreeCString(ctx, op);
    JS_FreeCString(ctx, args);
    if (!error.empty())
        return JS_ThrowTypeError(ctx, "%s", error.c_str());
    return JS_NewStringLen(ctx, result.data(), result.size());
}

// The allowlist as the host reads it: the parsed map for the host check, and the file text for the JS policy.
struct AllowlistFile
{
    std::map<std::string, std::string> allow;
    std::string json = "{}";
};

// Directory with api_shim.js, bridge.js, bridge_boot.js and bridge_allowlist.json, with a trailing separator.
// Test builds only (PH_TEST_HOOKS): PLUGINHOST_API_DIR overrides the installed copy, so tests run the repository
// files without reinstalling. The override changes the allowlist the host reads, so release builds never honour it.
std::string jsDirectory()
{
#ifdef PH_TEST_HOOKS
    if (const char* override = std::getenv("PLUGINHOST_API_DIR"))
    {
        if (*override)
        {
            std::string dir(override);
            if (dir.back() != kPathSep)
                dir += kPathSep;
            return dir;
        }
    }
#endif
    const std::string dir = moduleDirectory();
    return dir.empty() ? std::string() : dir + "js" + kPathSep;
}

const AllowlistFile& allowlistFile()
{
    static const AllowlistFile cached = [] {
        AllowlistFile f;
        const std::string path = jsDirectory() + "bridge_allowlist.json";
        std::ifstream in(path, std::ios::binary);
        std::stringstream buf;
        buf << in.rdbuf();
        const std::string text = buf.str();
        if (!text.empty() && loadBridgeAllowlist(path, f.allow))
        {
            f.json = text;
        }
        else
        {
            f.allow.clear();
            logEvent("bridge", "error", "allowlist missing or invalid: every bridge call is refused");
        }
        return f;
    }();
    return cached;
}

std::string readJs(const char* name)
{
    const std::string dir = jsDirectory();
    if (dir.empty())
        return std::string();
    std::ifstream in(dir + name, std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

std::string readShim()
{
    return readJs("api_shim.js");
}

// Loads bridge.js and bridge_boot.js into the engine. The factory from bridge_boot.js is kept in the engine,
// so plugin code never sees createBridge or the native primitive.
void installBridgeScripts(Engine& e, const std::string& pluginId)
{
    const std::string bridgeSrc = readJs("bridge.js");
    const std::string bootSrc = readJs("bridge_boot.js");
    if (bridgeSrc.empty() || bootSrc.empty())
    {
        logEvent("engine", "error", "bridge.js or bridge_boot.js not found; Asc.bridge is unavailable for " + pluginId);
        return;
    }

    JSValue r = JS_Eval(e.ctx, bridgeSrc.data(), bridgeSrc.size(), "bridge.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r))
    {
        JS_FreeValue(e.ctx, JS_GetException(e.ctx));
        logEvent("engine", "error", "bridge.js failed for " + pluginId);
        return;
    }
    JS_FreeValue(e.ctx, r);

    JSValue maker = JS_Eval(e.ctx, bootSrc.data(), bootSrc.size(), "bridge_boot.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(maker))
    {
        JS_FreeValue(e.ctx, JS_GetException(e.ctx));
        logEvent("engine", "error", "bridge_boot.js failed for " + pluginId);
        return;
    }
    JSValue nativeFn = JS_NewCFunction(e.ctx, jsPhBridge, "__ph_bridge", 2);
    JSValue makerArgs[1] = {nativeFn};
    JSValue factory = JS_Call(e.ctx, maker, JS_UNDEFINED, 1, makerArgs);
    JS_FreeValue(e.ctx, nativeFn);
    JS_FreeValue(e.ctx, maker);
    if (JS_IsException(factory))
    {
        JS_FreeValue(e.ctx, JS_GetException(e.ctx));
        logEvent("engine", "error", "bridge factory failed for " + pluginId);
        return;
    }
    e.makeBridge = factory;
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

    installBridgeScripts(stored, pluginId);
    return stored;
}

// Asc.bridge for one command: the host policy (permissions from hostPermissions and the allowlist file) and a session
// whose root is the active document. Returns null when the bridge cannot be set up; then Asc.bridge stays undefined.
// The caller keeps the session alive until the command ends; the session releases every handle when destroyed.
std::unique_ptr<BridgeSession> installBridge(Engine& e, const std::string& pluginId)
{
    const auto& host = HostService::instance().context();
    if (!host.is() || JS_IsUndefined(e.makeBridge))
        return nullptr;

    const AllowlistFile& list = allowlistFile();
    BridgePolicy policy;
#ifdef PH_TEST_HOOKS
    // The TestRunner runs commands as "__test__", which has no plugin config. Test builds grant it everything.
    const bool testPlugin = pluginId == "__test__";
#else
    const bool testPlugin = false;
#endif
    policy.canRead = testPlugin || pluginHasPermission(pluginId, "document.read");
    policy.canEdit = testPlugin || pluginHasPermission(pluginId, "document.edit");
    policy.allow = list.allow;

    // The JS policy carries the same flags and the allowlist text. The host check in C++ is the one that is enforced.
    const std::string policyText = std::string("{\"canRead\":") + (policy.canRead ? "true" : "false") +
                                   ",\"canEdit\":" + (policy.canEdit ? "true" : "false") + ",\"allow\":" + list.json + "}";
    JSValue policyObj = JS_ParseJSON(e.ctx, policyText.data(), policyText.size(), "bridge_policy.json");
    if (JS_IsException(policyObj))
    {
        JS_FreeValue(e.ctx, JS_GetException(e.ctx));
        logEvent("bridge", "error", pluginId + "|policy");
        return nullptr;
    }

    std::unique_ptr<BridgeSession> session;
    try
    {
        session = std::make_unique<BridgeSession>(host, activeDocumentRoot(), policy);
    }
    catch (...)
    {
        JS_FreeValue(e.ctx, policyObj);
        logEvent("bridge", "error", pluginId + "|session");
        return nullptr;
    }

    JSValue factoryArgs[1] = {policyObj};
    JSValue bridge = JS_Call(e.ctx, e.makeBridge, JS_UNDEFINED, 1, factoryArgs);
    JS_FreeValue(e.ctx, policyObj);
    if (JS_IsException(bridge))
    {
        JS_FreeValue(e.ctx, JS_GetException(e.ctx));
        logEvent("bridge", "error", pluginId + "|factory");
        return nullptr;
    }

    JSValue global = JS_GetGlobalObject(e.ctx);
    JSValue asc = JS_GetPropertyStr(e.ctx, global, "Asc");
    JS_SetPropertyStr(e.ctx, asc, "bridge", JS_DupValue(e.ctx, bridge)); // the property owns one reference
    JS_FreeValue(e.ctx, asc);
    JS_FreeValue(e.ctx, global);
    e.currentBridge = bridge; // our own reference, released by BridgeDisposer
    return session;
}

// Ends the command's bridge: dispose() releases its handles and makes any reference a plugin kept unusable, so an old
// Asc.bridge cannot reach the session of a later command. Declared after BridgeScope in run(), so it runs first
// (dispose calls native release, which needs the active session).
class BridgeDisposer
{
public:
    explicit BridgeDisposer(Engine& e)
        : m_e(e)
    {
    }
    ~BridgeDisposer()
    {
        if (JS_IsUndefined(m_e.currentBridge))
            return;
        m_e.deadline = std::chrono::steady_clock::time_point::max(); // the command's watchdog must not cut this short
        JSValue fn = JS_GetPropertyStr(m_e.ctx, m_e.currentBridge, "dispose");
        JSValue r = JS_Call(m_e.ctx, fn, m_e.currentBridge, 0, nullptr);
        if (JS_IsException(r))
            JS_FreeValue(m_e.ctx, JS_GetException(m_e.ctx));
        JS_FreeValue(m_e.ctx, r);
        JS_FreeValue(m_e.ctx, fn);
        JS_FreeValue(m_e.ctx, m_e.currentBridge);
        m_e.currentBridge = JS_UNDEFINED;
    }
    BridgeDisposer(const BridgeDisposer&) = delete;
    BridgeDisposer& operator=(const BridgeDisposer&) = delete;

private:
    Engine& m_e;
};

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

CommandOutcome CommandEngine::run(const std::string& pluginId, const std::string& source, const std::string& scopeJson,
                                  bool recalc)
{
    CommandOutcome out;
    Engine& e = engineFor(pluginId);
    // QuickJS measures stack depth from the stack position seen in JS_NewRuntime (first command). This call can come
    // from a different depth, and the limit is only 256 KB, so refresh the reference point.
    JS_UpdateStackTop(e.rt);
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

    // Asc.bridge for this command. The session is declared before the scope, so the scope clears the pointer first.
    std::unique_ptr<BridgeSession> bridgeSession = installBridge(e, pluginId);
    BridgeScope bridgeScope(bridgeSession.get());
    BridgeDisposer bridgeDisposer(e); // destroyed first: it needs the scope's active session

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
        if (recalc)
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
    JS_FreeValue(it->second.ctx, it->second.makeBridge);
    JS_FreeContext(it->second.ctx);
    JS_FreeRuntime(it->second.rt);
    engines().erase(it);
}
}

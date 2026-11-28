#pragma once

// Native side of the generic UNO bridge (PLAN_FULL_API.md section 2, F3, C++ part).
// It implements the four operations that extension/js/bridge.js calls through the closure:
//   get(handle, property)  set(handle, property, value)  call(handle, iface, method, args)  release(handle)
// Permissions and the allowlist are set by the host when the session is created. Plugin JavaScript cannot change
// them, and the JS bridge can only narrow them further.
//
// Scope of this version: parameters and results of scalar, string, enum, interface and sequence types (nested
// sequences included). Structs, Any and out-parameters are refused with an error.
//
// Draft: not compiled. Every UNO name was checked against udkapi/ (see the comments in bridge_native.cxx).
// Must run on the LibreOffice main thread (UNO calls are not thread-safe here).

#include "json_mini.hxx"

#include <com/sun/star/reflection/XIdlReflection.hpp>
#include <com/sun/star/uno/Any.hxx>
#include <com/sun/star/uno/Type.hxx>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/uno/XInterface.hpp>
#include <rtl/ustring.hxx>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pluginhost
{
struct BridgePolicy
{
    bool canRead = false;   // document.read
    bool canEdit = false;   // document.edit
    // "Interface.method" -> "read" | "edit". Interface names are full UNO names, for example
    // "com.sun.star.sheet.XSpreadsheet". Filled from extension/js/bridge_allowlist.json.
    std::map<std::string, std::string> allow;
    std::size_t maxArgBytes = 4 * 1024 * 1024;
};

// Loads {"Interface": {"method": "read"|"edit"}} from a JSON file. Returns false on any error. The caller then keeps an
// empty allowlist, so every call is refused (fail closed).
bool loadBridgeAllowlist(const std::string& path, std::map<std::string, std::string>& out);

// One session per plugin command. The root object gets handle 1. Handles returned by calls get increasing numbers.
class BridgeSession
{
public:
    BridgeSession(const css::uno::Reference<css::uno::XComponentContext>& ctx,
                  const css::uno::Reference<css::uno::XInterface>& root, BridgePolicy policy);
    ~BridgeSession();

    BridgeSession(const BridgeSession&) = delete;
    BridgeSession& operator=(const BridgeSession&) = delete;

    // op is "get", "set", "call" or "release". argsJson is a JSON array. Returns JSON text.
    // Throws std::runtime_error with a message that the engine turns into a JS error.
    std::string dispatch(const std::string& op, const std::string& argsJson);

    // Releases every handle and refuses further dispatch. Safe to call twice.
    void releaseAll();

    std::size_t liveCount() const { return m_handles.size(); }

private:
    std::string opGet(const std::vector<JsonValue>& args);
    std::string opSet(const std::vector<JsonValue>& args);
    std::string opCall(const std::vector<JsonValue>& args);
    std::string opRelease(const std::vector<JsonValue>& args);

    css::uno::Reference<css::uno::XInterface> resolve(const JsonValue& handle) const;
    css::uno::Any toAny(const JsonValue& v, const css::uno::Type& type);
    std::string toJson(const css::uno::Any& value);
    std::string registerHandle(const css::uno::Reference<css::uno::XInterface>& obj);

    css::uno::Reference<css::uno::XComponentContext> m_ctx;
    css::uno::Reference<css::reflection::XIdlReflection> m_reflection;
    std::map<std::int64_t, css::uno::Reference<css::uno::XInterface>> m_handles;
    std::int64_t m_next = 2;
    BridgePolicy m_policy;
    bool m_disposed = false;
};
}

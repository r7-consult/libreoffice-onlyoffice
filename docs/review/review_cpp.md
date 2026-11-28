# Static review of pluginhost C++ (never compiled). Reviewer: C++ pass 1

Build mode assumption (checked in CMakeLists.txt): SDK build, LIBO_INTERNAL_ONLY is NOT defined. In that mode
(include/rtl/ustring.hxx ~3521-3560, include/com/sun/star/uno/Sequence.h 121-129):
- OUString::fromUtf8 takes `const OString&` only (std::string / string_view overload is LIBO_INTERNAL_ONLY).
- No global `OUString` (`using ::rtl::OUString;` at ustring.hxx 4057 is LIBO_INTERNAL_ONLY).
- Sequence has no initializer_list constructor; no `Sequence<T>{x}`.
- UNO enum enumerators (TypeClass_STRING ...) are in css::uno, not imported by `using css::uno::TypeClass;`.

## Defects FIXED

### bridge_native.cxx / bridge_convert.cxx / bridge_native.hxx
1. bridge_native.cxx opCall (was l.251): `m_reflection->getType(Any(obj))` returns the class of the Any's declared type,
   which is XInterface (stoc/source/corereflection/crefl.cxx l.89-92: forType(rObj.getValueTypeRef())). Its method list
   has only queryInterface/acquire/release, so every allowlisted call ended with "method not found".
   Fix: `m_reflection->forName(iface)` (class of the allowlisted interface), check null and TypeClass_INTERFACE, then
   look up the method by name and declaring class. `XIdlMethod::invoke` itself queries the object for the declaring
   interface (criface.cxx 475, mapToUno -> extract -> uno_type_assignData with queryInterface). Also reject a null obj.
2. bridge_convert.cxx: `case TypeClass_BOOLEAN:` etc. unqualified. Compile error (enumerators are in css::uno).
   Fix: `using namespace css::uno;` in the pluginhost namespace of that file.
3. bridge_convert.cxx default branch of toAny: `"literal" + const char*` (pointer + pointer). Compile error.
   Fix: wrap in std::string(...). Same pattern in command_engine.cxx jsPhBridge (see 9).
4. fromUtf8(std::string) in bridge_native.cxx (opGet, opSet, opCall x2) and bridge_convert.cxx (string, any). Compile
   error in SDK mode. Fix: new inline `utf8ToOUString(const std::string&)` in bridge_native.hxx
   (`OUString::fromUtf8(OString(data, size))`).
5. bridge_convert.cxx INTERFACE argument: `Any(&ref, type)` stores the XInterface pointer under the target interface
   type. With multiple inheritance the XInterface sub-object need not be the face for that type, and
   IdlInterfaceMethodImpl::invoke does not re-query when the types are equal (criface.cxx 517). Fix: use
   `ref->queryInterface(type)` and throw if the object does not implement it (null still passes as typed null).
6. bridge_convert.cxx: sequence<any> (the element type of XCellRangeData.setDataArray, which is in the allowlist)
   and `any` properties hit the default branch "parameter type not supported", so setDataArray could never work.
   Fix: TypeClass_ANY from JSON kind (null -> void, bool, number -> double, string). Checked against
   corereflection coerce_assign (base.hxx 380) and IdlInterfaceMethodImpl::invoke ANY branch (criface.cxx 525).
7. bridge_convert.cxx formatDouble: NaN/Inf printed as nan/inf (invalid JSON; the JS side then fails with "invalid
   JSON"). Now "null". A decimal comma from the C locale is replaced by '.'.
8. bridge_native.cxx opSet: `getPropertySetInfo()` may return null -> null dereference. Now throws. resolve(): a handle
   number outside the int64 range was cast to int64 (UB); range check added (0 <= n < 2^53).
   Also null checks for `forName` results (sequence class in toAny and toJson).

### command_engine.cxx
9. jsPhBridge: `"text" + rtl::OUStringToOString(...).getStr()` is pointer + pointer. Compile error. Fixed with
   std::string(...) first.
10. jsPhCall: only std::exception was caught. nativeCall (api_bridge.cxx) uses UNO_QUERY_THROW and UNO calls, which
    throw css::uno::Exception (not a std::exception). It would unwind through QuickJS C frames (terminate or UB).
    Added catch for css::uno::Exception and catch(...), both turned into a JS TypeError. JSValue ownership of
    name/args strings is unchanged (freed before the throw).
11. run(): JS_UpdateStackTop(e.rt) added. QuickJS takes the stack reference point at JS_NewRuntime (first command),
    and the stack limit is only 256 KB; a later command arriving from a deeper call chain would get a bogus
    "stack overflow". quickjs.h l.524: "should be called when changing thread / to update the stack top".

### test_runner.hxx / test_runner.cxx
12. Bare `OUString` (header and source): no such global name in SDK mode. Added `#include <rtl/ustring.hxx>` and
    `using ::rtl::OUString;` inside namespace pluginhost in test_runner.hxx.
13. `Sequence<OUString>{ OUString(kServiceName) }` -> compile error in SDK mode. Now Sequence(1) + getArray()[0].
14. `OUString::fromUtf8(std::string)` x2 -> utf8ToOUString (includes bridge_native.hxx).
15. Added `#include <com/sun/star/beans/XIntrospectionAccess.hpp>` (the Reference<> return value is constructed and
    destroyed in getIntrospection, so the type must be complete).

### CMakeLists.txt
16. Linux: qjs is a static library linked into a shared library; no -fPIC. Added
    `set(CMAKE_POSITION_INDEPENDENT_CODE ON)` before add_subdirectory (applies to the qjs target).
17. Linux: compile definitions lacked LINUX (and GCC). osl/endian.h, sal/config.h, sal/alloca.h need LINUX
    (osl/endian.h falls into `#error "Target platform not specified !"`). Now `UNX LINUX GCC CPPU_ENV=gcc3`,
    as in sdk/settings/settings.mk.
18. Added LIBRARY_OUTPUT_DIRECTORY (the .so on Linux; RUNTIME_OUTPUT_DIRECTORY only covers the Windows .dll).

## Defects found, NOT fixed (not my files, or not definite)

A. SAME SDK-mode compile errors in files I do not own:
   - bare `OUString` (no `using ::rtl::OUString`): menu_dispatch.hxx/.cxx, panel_factory.hxx/.cxx, plugin_panel.hxx/.cxx.
   - `Sequence<OUString>{ ... }`: menu_dispatch.cxx l.82, panel_factory.cxx l.85.
   - `rtl::OUString::fromUtf8(std::string)`: api_bridge.cxx l.74, 83, 98, 223, 246, 267; context_menu.cxx l.78, 80
     (the second also feeds an OUStringConcat-free std::string expression); document_bridge.cxx l.76.
   Fix for owners: `using ::rtl::OUString;` in the namespace, build Sequence with Sequence(n)+getArray(), and call
   `utf8ToOUString(s)` from bridge_native.hxx (or copy it).
   (api_bridge.cxx activeDocumentRoot, the function I own, is fine.)
B. extension/pluginhost.components says uri="pluginhost.dll"; the Linux build produces pluginhost.so. Extension owner.
C. Windows non-ASCII paths: moduleDirectory() returns UTF-8, while std::ifstream/std::filesystem on MSVC take the ANSI
   code page for narrow strings, and getenv("LOCALAPPDATA") is ANSI. With a non-ASCII profile path the JS files
   (command_engine readJs) cannot be opened. Not fixed: needs a project-wide decision (UTF-8 manifest or wide paths).
D. fileUrl() does not percent-encode (spaces, '#', '%'). It has no callers in src/ today.
E. JS bridge objects survive a command in plugin globals, the native session does not. A stashed old bridge calling
   in a later command talks to the new session, with the C++ policy of the new command (C++ stays the enforcer, so no
   privilege issue; stale handle numbers just fail or hit the new session's handles).
F. TestRunner id "__test__" has no config, so Asc.bridge is always refused for it (documented in TEST_RUNNER.md).
G. XIdlClass::getMethods of corereflection lists only IDL methods; interface *attributes* (new-style `[attribute]`)
   are fields there (criface.cxx 745), so an allowlist entry such as Interface.getFoo for an attribute would be "method
   not found". The current allowlist entries are all methods (checked XModel.getCurrentController, XNamed.getName ...
   in udkapi), so no change.

## Verified correct (no change)
- Any(bool), Any(T const&) (FavourUnsigned for sal_uInt16/uInt32/uInt64), Any(const void*, const Type&),
  Any::getValue/getValueType/getValueTypeClass/hasValue, operator>>= (OUString, Reference), Type(TypeClass, OUString).
- XIdlReflection::forName/getType, XIdlClass::getMethods/getComponentType/getArray/createObject([out] any -> Any&),
  XIdlMember::getDeclaringClass, XIdlMethod::getParameterInfos/invoke(const Any&, Sequence<Any>& inout),
  XIdlArray::get/set(inout Any&)/getLen/realloc(inout Any&), ParamInfo{aName,aMode,aType}, ParamMode_IN,
  theCoreReflection::get(ctx) (singleton, udkapi/.../theCoreReflection.idl).
- JsonValue/parseJson/jsonEscape usage matches json_mini.hxx (get, getString, isObject/isArray/isString, kind enum).
- Order in opCall: denylist, allowlist, permission (same as bridge.js). Handle 1 = root, null receiver -> root,
  member init order matches declaration order in BridgeSession.
- QuickJS-ng 0.18 (quickjs.h): JSInterruptHandler(JSContext*, void*), JS_SetInterruptHandler, JS_NewCFunction(ctx, fn,
  name, length), JS_ParseJSON(ctx, buf, len, filename), JS_Call, JS_GetPropertyStr/JS_SetPropertyStr (the latter takes
  ownership of the value), JS_ThrowTypeError(ctx, fmt, ...), JS_IsUndefined/JS_IsException/JS_IsString, JS_UNDEFINED
  in C++ (JS_MKVAL inline for the non-NaN-boxing layout). Every JSValue in command_engine.cxx walked through: no leak
  or double free (maker, nativeFn, factory, policyObj, bridge, asc, global, scope, exc, v, strings).
- run(): BridgeSession is declared before BridgeScope, so the scope clears g_activeBridge first, then the session is
  destroyed. Exceptions: UNO calls in api_bridge helpers (begin/endUndoContext, lock, recalc, activeDocumentRoot)
  catch(...); BridgeSession construction is wrapped in catch(...).
- test_runner: XInvocation signatures match udkapi XInvocation.idl; WeakImplHelper<XInvocation, XServiceInfo>;
  exception constructors (Message, Context[, ArgumentPosition]); cppu::supportsService(XServiceInfo*, OUString const&).
- component.cxx matches odk/examples/cpp/custompanel/ctp_services.cxx; ImplementationEntry layout checked in
  cppuhelper/implementationentry.hxx.
- CMake: every src/*.cxx is listed; webhost_win.cxx only on WIN32, webhost_linux_webkitgtk.cxx only on Linux
  (webhost_stub.cxx unused); link libs for Windows (isal, icppu, icppuhelper, isalhelper) and Linux (uno_sal, uno_cppu,
  uno_cppuhelpergcc3, uno_salhelpergcc3) are the SDK names; qjs target is `qjs` with PUBLIC include dir.
- paths.cxx: both branches compile in principle (dladdr with _GNU_SOURCE default in g++, GetModuleHandleExW).
  plugin_config.cxx: std::filesystem usage with error_code overloads is correct.
- pluginHasPermission (protocol_adapter.cxx) sees the anonymous-namespace hasPermission (same TU); activeDocumentRoot
  sees activeModel (anonymous namespace of the first pluginhost block, same TU).

## Cannot be verified without a compiler
- Whether `Type(TypeClass_SEQUENCE, "[]any")` / `forName("[][]any")` resolve for nested any sequences (relies on
  typelib name parsing; same pattern as sbunoobj.cxx).
- Exact SAL_CALL/ABI match of the static ImplementationEntry function pointers (identical on x64 and Linux).
- Whether stricter warnings flag the implicit fallthrough in toAny's inner `switch` in the ANY case (all inner paths
  return or throw).
- Behaviour of createSingleComponentFactory when Create returns an empty reference (TEST_RUNNER.md already lists it).
- MSVC treatment of `std::map<std::string, JsonValue>` inside JsonValue (incomplete type; works in practice with MSVC
  and libstdc++; json_mini.hxx is not mine).

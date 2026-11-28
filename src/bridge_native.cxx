// Native side of the generic UNO bridge. See bridge_native.hxx. Conversions are in bridge_convert.cxx.
//
// UNO names used here, checked against the LibreOffice tree (C:\Oracle\lo):
//   com/sun/star/reflection/XIdlReflection.idl      getType(any) -> XIdlClass
//   com/sun/star/reflection/XIdlClass.idl           getName, getTypeClass, getMethods
//   com/sun/star/reflection/XIdlMember.idl          getName, getDeclaringClass
//   com/sun/star/reflection/XIdlMethod.idl          getParameterInfos, invoke(any obj, sequence<any>& args)
//   com/sun/star/reflection/ParamInfo.idl           aName, aMode, aType
//   com/sun/star/reflection/theCoreReflection.idl   singleton, theCoreReflection::get(ctx)
//   com/sun/star/beans/XPropertySet.idl             getPropertyValue, setPropertyValue, getPropertySetInfo
//   com/sun/star/beans/XPropertySetInfo.idl         getPropertyByName
//   com/sun/star/beans/Property.idl                 Name, Type
// The Basic bridge (basic/source/classes/sbunoobj.cxx) uses the same calls.

#include "bridge_native.hxx"

#include <com/sun/star/beans/Property.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/XPropertySetInfo.hpp>
#include <com/sun/star/reflection/ParamInfo.hpp>
#include <com/sun/star/reflection/ParamMode.hpp>
#include <com/sun/star/reflection/XIdlClass.hpp>
#include <com/sun/star/reflection/XIdlMember.hpp>
#include <com/sun/star/reflection/XIdlMethod.hpp>
#include <com/sun/star/reflection/theCoreReflection.hpp>
#include <com/sun/star/uno/Exception.hpp>
#include <com/sun/star/uno/TypeClass.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace pluginhost
{
using css::uno::Any;
using css::uno::Reference;
using css::uno::Sequence;
using css::uno::Type;
using css::uno::XInterface;
using css::reflection::XIdlClass;
using css::reflection::XIdlMethod;

namespace
{
// Same fixed denylist as extension/js/bridge.js. A policy cannot remove entries.
const char* const kDeniedInterfaces[] = {
    "XStorable", "XComponentLoader", "XDispatchHelper", "XDispatchProvider", "XSimpleFileAccess",
    "XConfigurationProvider", "XSingleServiceFactory", "XMultiServiceFactory", "XScriptProvider",
    "XMacroExpander", "XScriptLibraryContainer"};
const char* const kDeniedInterfaceParts[] = {
    "Script", "Macro", "Basic", "Python", "Dispatch", "FileAccess", "Storable", "ServiceManager"};
const char* const kDeniedMethods[] = {
    "storeToURL", "storeAsURL", "loadComponentFromURL", "dispatch", "executeDispatch", "createInstance",
    "createInstanceWithContext", "getServiceManager", "openURL", "system", "exec"};

bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

std::string lastComponent(const std::string& name)
{
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(dot + 1);
}

bool isDenied(const std::string& iface, const std::string& method)
{
    const std::string simple = lastComponent(iface);
    for (const char* d : kDeniedInterfaces)
        if (simple == d)
            return true;
    for (const char* part : kDeniedInterfaceParts)
        if (contains(simple, part))
            return true;
    for (const char* m : kDeniedMethods)
        if (method == m)
            return true;
    return false;
}

// Property names: a letter, then letters and digits, at most 64 characters (as in bridge.js).
bool isPropertyName(const std::string& name)
{
    if (name.empty() || name.size() > 64)
        return false;
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        const bool digit = c >= '0' && c <= '9';
        if (i == 0 ? !letter : !(letter || digit))
            return false;
    }
    return true;
}
}

bool loadBridgeAllowlist(const std::string& path, std::map<std::string, std::string>& out)
{
    out.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::stringstream ss;
    ss << in.rdbuf();
    JsonValue root;
    if (!parseJson(ss.str(), root) || !root.isObject())
        return false;
    for (const auto& iface : root.object)
    {
        if (!iface.second.isObject())
            return false;
        for (const auto& method : iface.second.object)
        {
            if (!method.second.isString() || (method.second.str != "read" && method.second.str != "edit"))
                return false;
            out[iface.first + "." + method.first] = method.second.str;
        }
    }
    return true;
}

BridgeSession::BridgeSession(const Reference<css::uno::XComponentContext>& ctx,
                             const Reference<XInterface>& root, BridgePolicy policy)
    : m_ctx(ctx)
    , m_reflection(css::reflection::theCoreReflection::get(ctx))
    , m_policy(std::move(policy))
{
    if (root.is())
        m_handles[1] = root;
}

BridgeSession::~BridgeSession()
{
    releaseAll();
}

void BridgeSession::releaseAll()
{
    m_handles.clear();
    m_disposed = true;
}

std::string BridgeSession::dispatch(const std::string& op, const std::string& argsJson)
{
    if (m_disposed)
        throw std::runtime_error("bridge: bridge disposed");
    if (argsJson.size() > m_policy.maxArgBytes)
        throw std::runtime_error("bridge: arguments exceed the size limit");
    JsonValue parsed;
    if (!parseJson(argsJson, parsed) || !parsed.isArray())
        throw std::runtime_error("bridge: arguments must be a JSON array");
    if (op == "get")
        return opGet(parsed.array);
    if (op == "set")
    {
        opSet(parsed.array);
        return "null";
    }
    if (op == "call")
        return opCall(parsed.array);
    if (op == "release")
    {
        opRelease(parsed.array);
        return "null";
    }
    throw std::runtime_error("bridge: unknown operation " + op);
}

Reference<XInterface> BridgeSession::resolve(const JsonValue& handle) const
{
    if (handle.kind != JsonValue::Kind::Number || std::floor(handle.number) != handle.number)
        throw std::runtime_error("bridge: invalid or released handle");
    const auto it = m_handles.find(static_cast<std::int64_t>(handle.number));
    if (it == m_handles.end())
        throw std::runtime_error("bridge: invalid or released handle");
    return it->second;
}

std::string BridgeSession::registerHandle(const Reference<XInterface>& obj)
{
    const std::int64_t id = m_next++;
    m_handles[id] = obj;
    return "{\"$h\":" + std::to_string(id) + "}";
}

std::string BridgeSession::opGet(const std::vector<JsonValue>& args)
{
    if (args.size() != 2)
        throw std::runtime_error("bridge: get takes (handle, property)");
    if (!m_policy.canRead)
        throw std::runtime_error("bridge: document.read permission required");
    if (args[1].kind != JsonValue::Kind::String || !isPropertyName(args[1].str))
        throw std::runtime_error("bridge: property name not allowed");
    const Reference<css::beans::XPropertySet> set(resolve(args[0]), css::uno::UNO_QUERY);
    if (!set.is())
        throw std::runtime_error("bridge: object is not a property set");
    return toJson(set->getPropertyValue(rtl::OUString::fromUtf8(args[1].str)));
}

void BridgeSession::opSet(const std::vector<JsonValue>& args)
{
    if (args.size() != 3)
        throw std::runtime_error("bridge: set takes (handle, property, value)");
    if (!m_policy.canEdit)
        throw std::runtime_error("bridge: document.edit permission required");
    if (args[1].kind != JsonValue::Kind::String || !isPropertyName(args[1].str))
        throw std::runtime_error("bridge: property name not allowed");
    const Reference<css::beans::XPropertySet> set(resolve(args[0]), css::uno::UNO_QUERY);
    if (!set.is())
        throw std::runtime_error("bridge: object is not a property set");
    const rtl::OUString name = rtl::OUString::fromUtf8(args[1].str);
    const css::beans::Property info = set->getPropertySetInfo()->getPropertyByName(name);
    set->setPropertyValue(name, toAny(args[2], info.Type));
}

std::string BridgeSession::opCall(const std::vector<JsonValue>& args)
{
    if (args.size() != 4)
        throw std::runtime_error("bridge: call takes (handle, iface, method, args)");
    if (args[1].kind != JsonValue::Kind::String || args[2].kind != JsonValue::Kind::String)
        throw std::runtime_error("bridge: interface and method must be strings");
    if (!args[3].isArray())
        throw std::runtime_error("bridge: call arguments must be an array");
    const std::string iface = args[1].str;
    const std::string method = args[2].str;
    if (isDenied(iface, method))
        throw std::runtime_error("bridge: denied: " + iface + "." + method);

    const auto rule = m_policy.allow.find(iface + "." + method);
    if (rule == m_policy.allow.end())
        throw std::runtime_error("bridge: not in allowlist: " + iface + "." + method);
    if (rule->second == "edit" && !m_policy.canEdit)
        throw std::runtime_error("bridge: document.edit permission required");
    if (rule->second == "read" && !m_policy.canRead)
        throw std::runtime_error("bridge: document.read permission required");

    const Reference<XInterface> obj = resolve(args[0]);
    const Reference<XIdlClass> cls = m_reflection->getType(Any(obj));
    const Sequence<Reference<XIdlMethod>> methods = cls->getMethods();
    Reference<XIdlMethod> target;
    const rtl::OUString wantedName = rtl::OUString::fromUtf8(method);
    const rtl::OUString wantedIface = rtl::OUString::fromUtf8(iface);
    for (const Reference<XIdlMethod>& m : methods)
    {
        if (m->getName() == wantedName && m->getDeclaringClass()->getName() == wantedIface)
        {
            target = m;
            break;
        }
    }
    if (!target.is())
        throw std::runtime_error("bridge: method not found: " + iface + "." + method);

    const Sequence<css::reflection::ParamInfo> infos = target->getParameterInfos();
    const JsonValue& argValues = args[3];
    if (infos.getLength() != static_cast<sal_Int32>(argValues.array.size()))
        throw std::runtime_error("bridge: wrong number of arguments for " + method);

    Sequence<Any> params(infos.getLength());
    for (sal_Int32 i = 0; i < infos.getLength(); ++i)
    {
        if (infos[i].aMode != css::reflection::ParamMode_IN)
            throw std::runtime_error("bridge: out and in-out parameters are not supported yet");
        const Type paramType(infos[i].aType->getTypeClass(), infos[i].aType->getName());
        params[i] = toAny(argValues.array[static_cast<std::size_t>(i)], paramType);
    }

    try
    {
        return toJson(target->invoke(Any(obj), params));
    }
    catch (const css::uno::Exception& e)
    {
        throw std::runtime_error("bridge: " + method + " failed: " +
                                 rtl::OUStringToOString(e.Message, RTL_TEXTENCODING_UTF8).getStr());
    }
}

void BridgeSession::opRelease(const std::vector<JsonValue>& args)
{
    if (args.size() != 1)
        throw std::runtime_error("bridge: release takes (handle)");
    resolve(args[0]);
    m_handles.erase(static_cast<std::int64_t>(args[0].number));
}
}

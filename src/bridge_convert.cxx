// Conversions between JSON and UNO values for the native bridge (see bridge_native.hxx).
//
// UNO names: css::uno::Any constructors (Any(T), Any(void const*, Type)), Any::getValue, Any::getValueTypeClass,
// Type(TypeClass, OUString) and TypeClass_* from com/sun/star/uno. Checked against the LibreOffice cppu headers.

#include "bridge_native.hxx"

#include <com/sun/star/uno/TypeClass.hpp>

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace pluginhost
{
using css::uno::Any;
using css::uno::Reference;
using css::uno::Type;
using css::uno::TypeClass;
using css::uno::XInterface;

namespace
{
// Integer value from a JSON number, checked to be whole and inside [lo, hi].
long long integerFrom(const JsonValue& v, double lo, double hi)
{
    if (v.kind != JsonValue::Kind::Number || std::floor(v.number) != v.number || v.number < lo || v.number > hi)
        throw std::runtime_error("bridge: argument is not an integer in range");
    return static_cast<long long>(v.number);
}

std::string formatDouble(double d)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.17g", d);
    return buf;
}
}

Any BridgeSession::toAny(const JsonValue& v, const Type& type)
{
    switch (type.getTypeClass())
    {
        case TypeClass_BOOLEAN:
            if (v.kind != JsonValue::Kind::Bool)
                throw std::runtime_error("bridge: argument must be a boolean");
            return Any(v.boolean);
        case TypeClass_BYTE:
            return Any(static_cast<sal_Int8>(integerFrom(v, -128, 127)));
        case TypeClass_SHORT:
            return Any(static_cast<sal_Int16>(integerFrom(v, -32768, 32767)));
        case TypeClass_UNSIGNED_SHORT:
            return Any(static_cast<sal_uInt16>(integerFrom(v, 0, 65535)));
        case TypeClass_LONG:
            return Any(static_cast<sal_Int32>(integerFrom(v, -2147483648.0, 2147483647.0)));
        case TypeClass_UNSIGNED_LONG:
            return Any(static_cast<sal_uInt32>(integerFrom(v, 0, 4294967295.0)));
        case TypeClass_HYPER:
            return Any(static_cast<sal_Int64>(integerFrom(v, -9007199254740992.0, 9007199254740992.0)));
        case TypeClass_UNSIGNED_HYPER:
            return Any(static_cast<sal_uInt64>(integerFrom(v, 0, 9007199254740992.0)));
        case TypeClass_FLOAT:
            if (v.kind != JsonValue::Kind::Number)
                throw std::runtime_error("bridge: argument must be a number");
            return Any(static_cast<float>(v.number));
        case TypeClass_DOUBLE:
            if (v.kind != JsonValue::Kind::Number)
                throw std::runtime_error("bridge: argument must be a number");
            return Any(v.number);
        case TypeClass_STRING:
            if (!v.isString())
                throw std::runtime_error("bridge: argument must be a string");
            return Any(rtl::OUString::fromUtf8(v.str));
        case TypeClass_ENUM:
        {
            const sal_Int32 value = static_cast<sal_Int32>(integerFrom(v, -2147483648.0, 2147483647.0));
            return Any(&value, type);
        }
        case TypeClass_INTERFACE:
        {
            Reference<XInterface> ref;
            if (v.kind == JsonValue::Kind::Null)
                return Any(&ref, type);
            if (!v.isObject() || !v.get("$h"))
                throw std::runtime_error("bridge: interface argument must be a handle");
            ref = resolve(*v.get("$h"));
            return Any(&ref, type);
        }
        default:
            throw std::runtime_error("bridge: parameter type not supported yet: " +
                                     rtl::OUStringToOString(type.getTypeName(), RTL_TEXTENCODING_UTF8).getStr());
    }
}

std::string BridgeSession::toJson(const Any& value)
{
    switch (value.getValueTypeClass())
    {
        case TypeClass_VOID:
            return "null";
        case TypeClass_BOOLEAN:
            return *static_cast<const sal_Bool*>(value.getValue()) ? "true" : "false";
        case TypeClass_BYTE:
            return std::to_string(*static_cast<const sal_Int8*>(value.getValue()));
        case TypeClass_SHORT:
            return std::to_string(*static_cast<const sal_Int16*>(value.getValue()));
        case TypeClass_UNSIGNED_SHORT:
            return std::to_string(*static_cast<const sal_uInt16*>(value.getValue()));
        case TypeClass_LONG:
        case TypeClass_ENUM:
            return std::to_string(*static_cast<const sal_Int32*>(value.getValue()));
        case TypeClass_UNSIGNED_LONG:
            return std::to_string(*static_cast<const sal_uInt32*>(value.getValue()));
        case TypeClass_HYPER:
            return std::to_string(*static_cast<const sal_Int64*>(value.getValue()));
        case TypeClass_UNSIGNED_HYPER:
            return std::to_string(*static_cast<const sal_uInt64*>(value.getValue()));
        case TypeClass_FLOAT:
            return formatDouble(*static_cast<const float*>(value.getValue()));
        case TypeClass_DOUBLE:
            return formatDouble(*static_cast<const double*>(value.getValue()));
        case TypeClass_STRING:
        {
            rtl::OUString s;
            value >>= s;
            return "\"" + jsonEscape(rtl::OUStringToOString(s, RTL_TEXTENCODING_UTF8).getStr()) + "\"";
        }
        case TypeClass_INTERFACE:
        {
            Reference<XInterface> ref;
            value >>= ref;
            if (!ref.is())
                return "null";
            return registerHandle(ref);
        }
        default:
            throw std::runtime_error("bridge: result type not supported yet");
    }
}
}

// Conversions between JSON and UNO values for the native bridge (see bridge_native.hxx).
//
// UNO names: css::uno::Any constructors (Any(T), Any(void const*, Type)), Any::getValue, Any::getValueTypeClass,
// Type(TypeClass, OUString) and TypeClass_* from com/sun/star/uno. Checked against the LibreOffice cppu headers.

#include "bridge_native.hxx"

#include <com/sun/star/reflection/XIdlArray.hpp>
#include <com/sun/star/reflection/XIdlClass.hpp>
#include <com/sun/star/reflection/XIdlReflection.hpp>
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
using css::reflection::XIdlArray;
using css::reflection::XIdlClass;
// The TypeClass_* enumerators live in css::uno. A using-declaration of the type TypeClass does not bring them in.
using namespace css::uno;

namespace
{
// Integer value from a JSON number, checked to be whole and inside [lo, hi].
long long integerFrom(const JsonValue& v, double lo, double hi)
{
    if (v.kind != JsonValue::Kind::Number || std::floor(v.number) != v.number || v.number < lo || v.number > hi)
        throw std::runtime_error("bridge: argument is not an integer in range");
    return static_cast<long long>(v.number);
}

// One shared, locale-independent formatter (json_mini.cxx): NaN and infinity become null.
std::string formatDouble(double d)
{
    return formatJsonNumber(d);
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
            return Any(utf8ToOUString(v.str));
        case TypeClass_ANY:
            // Used for sequence<any> elements (setDataArray) and "any" properties: the JSON kind picks the UNO type.
            switch (v.kind)
            {
                case JsonValue::Kind::Null:
                    return Any();
                case JsonValue::Kind::Bool:
                    return Any(v.boolean);
                case JsonValue::Kind::Number:
                    return Any(v.number);
                case JsonValue::Kind::String:
                    return Any(utf8ToOUString(v.str));
                default:
                    throw std::runtime_error("bridge: an any value must be null, a boolean, a number or a string");
            }
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
            if (!ref.is())
                return Any(&ref, type);
            // Any(&ref, type) would label the stored XInterface pointer with the target interface type, which is only
            // right when the object's XInterface sub-object is also its face for that type. queryInterface returns an
            // Any that holds the right pointer.
            const Any queried = ref->queryInterface(type);
            if (!queried.hasValue())
                throw std::runtime_error("bridge: the object does not implement the interface the method expects");
            return queried;
        }
        case TypeClass_SEQUENCE:
        {
            if (v.kind != JsonValue::Kind::Array)
                throw std::runtime_error("bridge: argument must be an array");
            // Same calls as the Basic bridge (basic/source/classes/sbunoobj.cxx): forName, createObject, realloc, set.
            const Reference<XIdlClass> seqClass = m_reflection->forName(type.getTypeName());
            if (!seqClass.is())
                throw std::runtime_error("bridge: unknown sequence type");
            const Reference<XIdlClass> elemClass = seqClass->getComponentType();
            const Type elemType(elemClass->getTypeClass(), elemClass->getName());
            const Reference<XIdlArray> arr = seqClass->getArray();
            Any seq;
            seqClass->createObject(seq);
            arr->realloc(seq, static_cast<sal_Int32>(v.array.size()));
            for (std::size_t i = 0; i < v.array.size(); ++i)
                arr->set(seq, static_cast<sal_Int32>(i), toAny(v.array[i], elemType));
            return seq;
        }
        default:
            // std::string(...) first: a string literal plus a const char* would add two pointers.
            throw std::runtime_error(std::string("bridge: parameter type not supported yet: ") +
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
        case TypeClass_SEQUENCE:
        {
            const Reference<XIdlClass> seqClass = m_reflection->forName(value.getValueType().getTypeName());
            if (!seqClass.is())
                throw std::runtime_error("bridge: unknown sequence type in a result");
            const Reference<XIdlArray> arr = seqClass->getArray();
            const sal_Int32 n = arr->getLen(value);
            std::string out = "[";
            for (sal_Int32 i = 0; i < n; ++i)
            {
                if (i > 0)
                    out += ",";
                out += toJson(arr->get(value, i));
            }
            return out + "]";
        }
        default:
            throw std::runtime_error("bridge: result type not supported yet");
    }
}
}

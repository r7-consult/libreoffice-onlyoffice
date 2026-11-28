#pragma once

// Small JSON reader/writer used by the host. Supports the subset needed for
// plugin config.json and page messages: objects, arrays, strings, numbers,
// booleans and null. No dependencies. Not a general-purpose JSON library.

#include <map>
#include <string>
#include <vector>

namespace pluginhost
{
struct JsonValue
{
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    bool isObject() const { return kind == Kind::Object; }
    bool isArray() const { return kind == Kind::Array; }
    bool isString() const { return kind == Kind::String; }

    // Returns nullptr when the key is missing or this is not an object.
    const JsonValue* get(const std::string& key) const;
    std::string getString(const std::string& key, const std::string& fallback = std::string()) const;
};

// Parses text (UTF-8). Returns false and leaves out unchanged on syntax error.
bool parseJson(const std::string& text, JsonValue& out);

// Escapes a string for inclusion between double quotes in JSON output.
std::string jsonEscape(const std::string& s);

// A number as JSON text: locale-independent, up to 17 significant digits, "null" for NaN and infinity.
std::string formatJsonNumber(double value);
}

#include "json_mini.hxx"

#include <cstdlib>

namespace pluginhost
{
const JsonValue* JsonValue::get(const std::string& key) const
{
    if (kind != Kind::Object)
        return nullptr;
    auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

std::string JsonValue::getString(const std::string& key, const std::string& fallback) const
{
    const JsonValue* v = get(key);
    return (v && v->kind == Kind::String) ? v->str : fallback;
}

namespace
{
class Parser
{
public:
    explicit Parser(const std::string& text)
        : m_s(text)
    {
    }

    bool parseDocument(JsonValue& out)
    {
        skipWs();
        if (!parseValue(out, 0))
            return false;
        skipWs();
        return m_pos == m_s.size();
    }

private:
    static constexpr int kMaxDepth = 64;

    void skipWs()
    {
        while (m_pos < m_s.size() && (m_s[m_pos] == ' ' || m_s[m_pos] == '\t' || m_s[m_pos] == '\n' || m_s[m_pos] == '\r'))
            ++m_pos;
    }

    bool literal(const char* lit)
    {
        const std::string l(lit);
        if (m_s.compare(m_pos, l.size(), l) != 0)
            return false;
        m_pos += l.size();
        return true;
    }

    bool parseValue(JsonValue& out, int depth)
    {
        if (depth > kMaxDepth || m_pos >= m_s.size())
            return false;
        const char c = m_s[m_pos];
        if (c == '{')
            return parseObject(out, depth);
        if (c == '[')
            return parseArray(out, depth);
        if (c == '"')
        {
            out.kind = JsonValue::Kind::String;
            return parseString(out.str);
        }
        if (c == 't' || c == 'f')
        {
            out.kind = JsonValue::Kind::Bool;
            out.boolean = (c == 't');
            return literal(c == 't' ? "true" : "false");
        }
        if (c == 'n')
        {
            out.kind = JsonValue::Kind::Null;
            return literal("null");
        }
        return parseNumber(out);
    }

    bool parseObject(JsonValue& out, int depth)
    {
        out.kind = JsonValue::Kind::Object;
        ++m_pos; // {
        skipWs();
        if (m_pos < m_s.size() && m_s[m_pos] == '}')
        {
            ++m_pos;
            return true;
        }
        while (true)
        {
            skipWs();
            std::string key;
            if (m_pos >= m_s.size() || m_s[m_pos] != '"' || !parseString(key))
                return false;
            skipWs();
            if (m_pos >= m_s.size() || m_s[m_pos] != ':')
                return false;
            ++m_pos;
            skipWs();
            JsonValue v;
            if (!parseValue(v, depth + 1))
                return false;
            out.object[key] = std::move(v);
            skipWs();
            if (m_pos >= m_s.size())
                return false;
            if (m_s[m_pos] == ',')
            {
                ++m_pos;
                continue;
            }
            if (m_s[m_pos] == '}')
            {
                ++m_pos;
                return true;
            }
            return false;
        }
    }

    bool parseArray(JsonValue& out, int depth)
    {
        out.kind = JsonValue::Kind::Array;
        ++m_pos; // [
        skipWs();
        if (m_pos < m_s.size() && m_s[m_pos] == ']')
        {
            ++m_pos;
            return true;
        }
        while (true)
        {
            skipWs();
            JsonValue v;
            if (!parseValue(v, depth + 1))
                return false;
            out.array.push_back(std::move(v));
            skipWs();
            if (m_pos >= m_s.size())
                return false;
            if (m_s[m_pos] == ',')
            {
                ++m_pos;
                continue;
            }
            if (m_s[m_pos] == ']')
            {
                ++m_pos;
                return true;
            }
            return false;
        }
    }

    static void appendUtf8(std::string& s, unsigned cp)
    {
        if (cp < 0x80)
            s += static_cast<char>(cp);
        else if (cp < 0x800)
        {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parseHex4(unsigned& out)
    {
        if (m_pos + 4 > m_s.size())
            return false;
        out = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = m_s[m_pos++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parseString(std::string& out)
    {
        ++m_pos; // opening quote
        while (m_pos < m_s.size())
        {
            const char c = m_s[m_pos++];
            if (c == '"')
                return true;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (m_pos >= m_s.size())
                return false;
            const char e = m_s[m_pos++];
            switch (e)
            {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
            {
                unsigned cp = 0;
                if (!parseHex4(cp))
                    return false;
                // Surrogate pair: \uD800-\uDBFF followed by \uDC00-\uDFFF.
                if (cp >= 0xD800 && cp <= 0xDBFF && m_pos + 6 <= m_s.size() && m_s[m_pos] == '\\' && m_s[m_pos + 1] == 'u')
                {
                    m_pos += 2;
                    unsigned lo = 0;
                    if (!parseHex4(lo))
                        return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool parseNumber(JsonValue& out)
    {
        const size_t start = m_pos;
        if (m_pos < m_s.size() && m_s[m_pos] == '-')
            ++m_pos;
        while (m_pos < m_s.size() && ((m_s[m_pos] >= '0' && m_s[m_pos] <= '9') || m_s[m_pos] == '.' ||
                                      m_s[m_pos] == 'e' || m_s[m_pos] == 'E' || m_s[m_pos] == '+' || m_s[m_pos] == '-'))
            ++m_pos;
        if (m_pos == start)
            return false;
        out.kind = JsonValue::Kind::Number;
        out.number = std::strtod(m_s.substr(start, m_pos - start).c_str(), nullptr);
        return true;
    }

    const std::string& m_s;
    size_t m_pos = 0;
};
}

bool parseJson(const std::string& text, JsonValue& out)
{
    out = JsonValue();
    Parser p(text);
    JsonValue tmp;
    if (!p.parseDocument(tmp))
        return false;
    out = std::move(tmp);
    return true;
}

std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                static const char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[(c >> 4) & 0xF];
                out += hex[c & 0xF];
            }
            else
                out += c;
        }
    }
    return out;
}
}

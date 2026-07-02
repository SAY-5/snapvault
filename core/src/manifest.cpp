#include "snapvault/manifest.h"

#include <cctype>
#include <sstream>
#include <stdexcept>

namespace snapvault {

namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

// Small recursive-descent scanner over our own schema.
struct Parser {
    const std::string& s;
    size_t i = 0;
    explicit Parser(const std::string& text) : s(text) {}

    [[noreturn]] void fail(const std::string& msg) {
        throw std::runtime_error("manifest parse error: " + msg);
    }

    void skip_ws() {
        while (i < s.size() &&
               (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) {
            ++i;
        }
    }

    char peek() {
        skip_ws();
        if (i >= s.size()) fail("unexpected end of input");
        return s[i];
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++i;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i >= s.size()) fail("bad escape");
                char e = s[i++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        fail("unterminated string");
    }

    uint64_t parse_uint() {
        skip_ws();
        uint64_t v = 0;
        bool any = false;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            v = v * 10 + static_cast<uint64_t>(s[i] - '0');
            ++i;
            any = true;
        }
        if (!any) fail("expected number");
        return v;
    }

    std::vector<std::string> parse_string_array() {
        std::vector<std::string> out;
        expect('[');
        skip_ws();
        if (peek() == ']') {
            ++i;
            return out;
        }
        while (true) {
            out.push_back(parse_string());
            char c = peek();
            if (c == ',') {
                ++i;
                continue;
            }
            if (c == ']') {
                ++i;
                break;
            }
            fail("expected ',' or ']' in array");
        }
        return out;
    }
};

}  // namespace

std::string Manifest::to_json() const {
    std::ostringstream os;
    os << "{\n";
    os << "  \"name\": \"" << json_escape(name) << "\",\n";
    os << "  \"chunk_size\": " << chunk_size << ",\n";
    os << "  \"files\": [";
    for (size_t f = 0; f < files.size(); ++f) {
        const FileEntry& fe = files[f];
        os << (f == 0 ? "\n" : ",\n");
        os << "    {\n";
        os << "      \"path\": \"" << json_escape(fe.path) << "\",\n";
        os << "      \"size\": " << fe.size << ",\n";
        os << "      \"chunks\": [";
        for (size_t c = 0; c < fe.chunks.size(); ++c) {
            os << (c == 0 ? "" : ", ");
            os << "\"" << fe.chunks[c] << "\"";
        }
        os << "]\n";
        os << "    }";
    }
    if (!files.empty()) os << "\n  ";
    os << "]\n";
    os << "}\n";
    return os.str();
}

Manifest Manifest::from_json(const std::string& text) {
    Parser p(text);
    Manifest m;
    p.expect('{');
    while (true) {
        std::string key = p.parse_string();
        p.expect(':');
        if (key == "name") {
            m.name = p.parse_string();
        } else if (key == "chunk_size") {
            m.chunk_size = static_cast<uint32_t>(p.parse_uint());
        } else if (key == "files") {
            p.expect('[');
            p.skip_ws();
            if (p.peek() != ']') {
                while (true) {
                    FileEntry fe;
                    p.expect('{');
                    while (true) {
                        std::string fkey = p.parse_string();
                        p.expect(':');
                        if (fkey == "path") {
                            fe.path = p.parse_string();
                        } else if (fkey == "size") {
                            fe.size = p.parse_uint();
                        } else if (fkey == "chunks") {
                            fe.chunks = p.parse_string_array();
                        } else {
                            p.fail("unknown file key: " + fkey);
                        }
                        char c = p.peek();
                        if (c == ',') {
                            ++p.i;
                            continue;
                        }
                        if (c == '}') {
                            ++p.i;
                            break;
                        }
                        p.fail("expected ',' or '}' in file object");
                    }
                    m.files.push_back(std::move(fe));
                    char c = p.peek();
                    if (c == ',') {
                        ++p.i;
                        continue;
                    }
                    if (c == ']') {
                        ++p.i;
                        break;
                    }
                    p.fail("expected ',' or ']' in files array");
                }
            } else {
                ++p.i;  // consume ']'
            }
        } else {
            p.fail("unknown key: " + key);
        }
        char c = p.peek();
        if (c == ',') {
            ++p.i;
            continue;
        }
        if (c == '}') {
            ++p.i;
            break;
        }
        p.fail("expected ',' or '}' in object");
    }
    return m;
}

}  // namespace snapvault

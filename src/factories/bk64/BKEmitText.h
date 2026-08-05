#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <yaml-cpp/yaml.h>

namespace BK64 {

// Emit a text string for modding yaml.
inline void EmitText(YAML::Emitter& out, const std::string& str) {
    const char* s = str.c_str();
    bool plain = true;
    for (const char* p = s; *p != '\0'; p++) {
        if (static_cast<uint8_t>(*p) < 0x20 || static_cast<uint8_t>(*p) > 0x7E || *p == '\\') {
            plain = false;
            break;
        }
    }
    if (plain) {
        out << YAML::DoubleQuoted << s;
        return;
    }
    std::string escaped;
    for (const char* p = s; *p != '\0'; p++) {
        const auto c = static_cast<uint8_t>(*p);
        if (c == '\\') {
            escaped += "\\\\";
        } else if (c < 0x20 || c > 0x7E) {
            char buf[5];
            std::snprintf(buf, sizeof(buf), "\\x%02X", c);
            escaped += buf;
        } else {
            escaped += static_cast<char>(c);
        }
    }
    out << YAML::SingleQuoted << escaped;
}

// Reverse EmitText's escapes on a string read back from modding yaml.
inline std::string DecodeText(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            if (str[i + 1] == '\\') {
                out += '\\';
                i++;
                continue;
            }
            if ((str[i + 1] == 'x' || str[i + 1] == 'X') && i + 3 < str.size() &&
                std::isxdigit(static_cast<unsigned char>(str[i + 2])) &&
                std::isxdigit(static_cast<unsigned char>(str[i + 3]))) {
                out += static_cast<char>(std::stoi(str.substr(i + 2, 2), nullptr, 16));
                i += 3;
                continue;
            }
        }
        out += str[i];
    }
    return out;
}

} // namespace BK64

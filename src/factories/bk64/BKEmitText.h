#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <yaml-cpp/yaml.h>

// Shared yaml/text emission formatters
#define YAML_HEX(num) YAML::Hex << (num) << YAML::Dec
#define FORMAT_HEX(x, w) \
    std::hex << std::uppercase << std::setfill('0') << std::setw(w) << x << std::nouppercase << std::dec

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

// Modding yaml readers. They throw, naming where the bad value is.
[[noreturn]] inline void YamlFail(const std::string& where, const std::string& problem) {
    throw std::runtime_error(where + ": " + problem);
}

inline uint32_t YamlNumber(const YAML::Node& node, const std::string& where, uint32_t max) {
    const std::string text = node.as<std::string>();
    size_t used = 0;
    unsigned long value = 0;
    try {
        value = std::stoul(text, &used, 0);
    } catch (const std::exception&) { used = 0; }
    if (used == 0 || used != text.size()) {
        YamlFail(where, "\"" + text + "\" isn't a number");
    }
    if (value > max) {
        YamlFail(where, text + " is over " + std::to_string(max));
    }
    return static_cast<uint32_t>(value);
}

inline int16_t YamlShort(const YAML::Node& node, const std::string& where) {
    const std::string text = node.as<std::string>();
    size_t used = 0;
    long value = 0;
    try {
        value = std::stol(text, &used, 0);
    } catch (const std::exception&) { used = 0; }
    if (used == 0 || used != text.size()) {
        YamlFail(where, "\"" + text + "\" isn't a number");
    }
    if (value < INT16_MIN || value > INT16_MAX) {
        YamlFail(where, text + " is out of range");
    }
    return static_cast<int16_t>(value);
}

// The same for a key the entry has to have
inline uint32_t YamlNumber(const YAML::Node& entry, const char* key, const std::string& where, uint32_t max) {
    if (!entry[key]) {
        YamlFail(where, std::string("needs ") + key);
    }
    return YamlNumber(entry[key], where + " " + key, max);
}

inline int16_t YamlShort(const YAML::Node& entry, const char* key, const std::string& where) {
    if (!entry[key]) {
        YamlFail(where, std::string("needs ") + key);
    }
    return YamlShort(entry[key], where + " " + key);
}

inline float YamlFloat(const YAML::Node& entry, const char* key, const std::string& where) {
    if (!entry[key]) {
        YamlFail(where, std::string("needs ") + key);
    }
    try {
        return entry[key].as<float>();
    } catch (const YAML::Exception&) {
        YamlFail(where + " " + key, "\"" + entry[key].as<std::string>() + "\" isn't a number");
    }
}

// A modding file's one entry, keyed by the asset's name.
inline YAML::Node YamlEntry(const std::vector<uint8_t>& buffer, const std::string& where) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string(buffer.begin(), buffer.end()));
    } catch (const YAML::Exception& e) { YamlFail(where, e.what()); }
    if (!root.IsMap() || root.size() != 1) {
        YamlFail(where, "needs one top-level key, the asset's name");
    }
    return root.begin()->second;
}

// Every key the map holds has to be one of keys.
inline void YamlKeys(const YAML::Node& node, std::initializer_list<const char*> keys, const std::string& where) {
    if (!node.IsMap()) {
        YamlFail(where, "needs to be a map");
    }
    for (const auto& item : node) {
        const std::string key = item.first.as<std::string>();
        bool known = false;
        for (const char* name : keys) {
            known |= key == name;
        }
        if (!known) {
            YamlFail(where, "doesn't take " + key);
        }
    }
}

template <typename T> void ReadList(const YAML::Node& list, T* values, int count, const std::string& at) {
    if (!list.IsSequence() || (int)list.size() != count) {
        YamlFail(at, "needs " + std::to_string(count) + " numbers");
    }
    for (int i = 0; i < count; i++) {
        if constexpr (std::is_same_v<T, int16_t>) {
            values[i] = YamlShort(list[i], at);
        } else {
            try {
                values[i] = list[i].as<T>();
            } catch (const YAML::Exception&) { YamlFail(at, "\"" + list[i].as<std::string>() + "\" isn't a number"); }
        }
    }
}

template <typename T>
void ReadList(const YAML::Node& entry, const char* key, T* values, int count, const std::string& where) {
    const YAML::Node list = entry[key];
    if (!list) {
        YamlFail(where, std::string("needs a ") + key);
    }
    ReadList(list, values, count, where + " " + key);
}

template <typename T> void EmitList(YAML::Emitter& out, const char* key, const T* values, int count) {
    out << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq;
    for (int i = 0; i < count; i++) {
        out << values[i];
    }
    out << YAML::EndSeq;
}

inline void EmitList(YAML::Emitter& out, const char* key, std::initializer_list<int> values) {
    EmitList(out, key, values.begin(), (int)values.size());
}

} // namespace BK64

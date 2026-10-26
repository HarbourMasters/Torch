#include "ACUtils.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "utils/Decompressor.h"

#include <algorithm>
#include <cstdio>

namespace AC {

std::string Hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", v);
    return buf;
}

std::string Upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

std::string TypeOf(YAML::Node node) {
    return Upper(GetSafeNode<std::string>(node, "type", ""));
}

std::string NameOf(YAML::Node& node) {
    return GetSafeNode<std::string>(node, "symbol", GetSafeNode<std::string>(node, "vpath", "?"));
}

std::vector<uint8_t> ReadBytes(uint32_t addr, uint32_t size, const std::string& what) {
    if (size == 0) {
        return {};
    }
    if (addr == 0) {
        throw std::runtime_error(what + " is NULL but " + std::to_string(size) + " bytes are needed");
    }
    auto [_, chunk] = Decompressor::AutoDecode(addr, size, Companion::Instance->GetRomData());
    if (chunk.size < size) {
        throw std::runtime_error(what + " at " + Hex(addr) + " is truncated");
    }
    return std::vector<uint8_t>(chunk.data, chunk.data + size);
}

std::string AssetPath(uint32_t addr, const std::string& type, const std::string& what) {
    if (addr == 0) {
        return "";
    }
    auto found = Companion::Instance->GetNodeByAddr(addr);
    if (!found.has_value()) {
        throw std::runtime_error(what + ": " + Hex(addr) + " is not a declared asset");
    }
    auto node = std::get<1>(found.value());
    if (!type.empty() && TypeOf(node) != type) {
        throw std::runtime_error(what + ": " + Hex(addr) + " is a " + TypeOf(node) + ", expected " + type);
    }
    return std::get<0>(found.value());
}

std::optional<uint32_t> CountHint(YAML::Node& node, const std::string& key) {
    if (!node["counts"] || !node["counts"][key]) {
        return std::nullopt;
    }
    return node["counts"][key].as<uint32_t>();
}

void CheckCount(YAML::Node& node, const std::string& key, uint32_t computed, const std::string& name) {
    const auto declared = CountHint(node, key);
    if (!declared.has_value()) {
        return;
    }
    if (computed > declared.value()) {
        throw std::runtime_error(name + ": " + key + " needs " + std::to_string(computed) +
                                 " elements but the decomp declares " + std::to_string(declared.value()));
    }
    if (computed < declared.value()) {
        SPDLOG_WARN("{}: {} uses {} of the {} declared elements", name, key, computed, declared.value());
    }
}

void WriteBytes(LUS::BinaryWriter& writer, const std::vector<uint8_t>& bytes) {
    writer.Write((uint32_t)bytes.size());
    for (const auto b : bytes) {
        writer.Write(b);
    }
}

} // namespace AC

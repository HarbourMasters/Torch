#pragma once

#include "Companion.h"
#include <lib/binarytools/BinaryWriter.h>
#include <spdlog/spdlog.h>

#include <sstream>
#include <variant>

namespace SM64::ScriptBinary {

enum class ArgType : uint8_t {
    Number = 0,
    Symbol = 1,
    Path = 2,
};

inline void WriteNumber(LUS::BinaryWriter& writer, int64_t value) {
    writer.Write(static_cast<uint8_t>(ArgType::Number));
    writer.Write(value);
}

inline void WritePointer(LUS::BinaryWriter& writer, uint32_t ptr) {
    if (ptr == 0) {
        WriteNumber(writer, 0);
        return;
    }
    if (auto node = Companion::Instance->GetNodeByAddr(ptr)) {
        writer.Write(static_cast<uint8_t>(ArgType::Path));
        writer.Write(std::get<0>(node.value()));
        return;
    }
    if (auto symbol = Companion::Instance->GetSymbolByAddr(ptr)) {
        writer.Write(static_cast<uint8_t>(ArgType::Symbol));
        writer.Write(symbol.value());
        return;
    }
    SPDLOG_WARN("Script points at 0x{:X}, which is neither an extracted asset nor a known symbol", ptr);
    WriteNumber(writer, 0);
}

// Arguments are std::variant<u8, s8, u16, s16, u32, s32, f32, ptr>, as Torch's level and behavior factories parse
// them; the last alternative is a pointer.
template <typename Command>
void WriteScript(LUS::BinaryWriter& writer, const std::string& name, const std::vector<Command>& commands) {
    writer.Write(static_cast<uint32_t>(1)); // arrays
    writer.Write(std::string());            // entry: the only array
    writer.Write(name);
    writer.Write(static_cast<uint32_t>(commands.size()));
    for (const auto& [opcode, arguments] : commands) {
        std::ostringstream opName;
        opName << opcode;
        writer.Write(opName.str());
        writer.Write(static_cast<uint32_t>(0)); // no source line
        writer.Write(static_cast<uint8_t>(arguments.size()));
        for (const auto& arg : arguments) {
            std::visit(
                [&](auto value) {
                    using T = decltype(value);
                    if constexpr (std::is_same_v<T, uint64_t>) {
                        WritePointer(writer, static_cast<uint32_t>(value));
                    } else {
                        WriteNumber(writer, static_cast<int64_t>(value));
                    }
                },
                arg);
        }
    }
}

} // namespace SM64::ScriptBinary

#pragma once

#include <string>
#include <vector>
#include <limits>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <type_traits>

namespace Torch {
template< typename T >
std::string to_hex(T number, const bool append0x = true) {
    static_assert(std::is_integral_v<T>, "to_hex function only accepts integral types.");
    std::stringstream stream;
    if (append0x) {
        stream << "0x";
    }

    if constexpr (std::is_signed_v<T>) {
        stream << std::abs(number);
    } else {
        stream << number;
    }

    auto formatted_string = stream.str();
    std::transform(
            formatted_string.begin() + (append0x ? 2 : 0),
            formatted_string.end(),
            formatted_string.begin() + (append0x ? 2 : 0),
            ::toupper
    );

    return formatted_string;
}

uint32_t translate(uint32_t offset);
std::vector<std::filesystem::directory_entry> getRecursiveEntries(const std::filesystem::path baseDir);

};
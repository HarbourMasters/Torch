#pragma once

#include <vector>

namespace PyUtils {
template<typename T>
std::vector<T> slice(std::vector<T> const &v, uint32_t m, uint32_t n = -1) {
    auto first = v.cbegin() + m;
    auto last = n == -1 ? v.cend() : v.cbegin() + n;

    std::vector<T> vec(first, last);
    return vec;
}

std::vector<uint32_t> range(uint32_t start, uint32_t end) {
    std::vector<uint32_t> result;
    for (uint32_t i = start; i < end; ++i) {
        result.push_back(i);
    }
    return result;
}

}
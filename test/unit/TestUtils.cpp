#include "TestUtils.h"

#include <fstream>
#include <random>
#include <stdexcept>

namespace TestUtils {

void PutBE32(std::vector<uint8_t>& buf, size_t offset, uint32_t value) {
    if (buf.size() < offset + 4) {
        buf.resize(offset + 4);
    }
    buf[offset] = value >> 24;
    buf[offset + 1] = value >> 16;
    buf[offset + 2] = value >> 8;
    buf[offset + 3] = value;
}

void PutBE16(std::vector<uint8_t>& buf, size_t offset, uint16_t value) {
    if (buf.size() < offset + 2) {
        buf.resize(offset + 2);
    }
    buf[offset] = value >> 8;
    buf[offset + 1] = value;
}

uint32_t GetBE32(const std::vector<uint8_t>& buf, size_t offset) {
    return (uint32_t(buf[offset]) << 24) | (uint32_t(buf[offset + 1]) << 16) | (uint32_t(buf[offset + 2]) << 8) |
           buf[offset + 3];
}

uint16_t GetBE16(const std::vector<uint8_t>& buf, size_t offset) {
    return (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
}

void AppendBE32(std::vector<uint8_t>& buf, uint32_t value) {
    PutBE32(buf, buf.size(), value);
}

void Align(std::vector<uint8_t>& buf, size_t alignment) {
    buf.resize((buf.size() + alignment - 1) / alignment * alignment, 0);
}

std::vector<uint8_t> Yaz0Literal(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out = { 'Y', 'a', 'z', '0' };
    AppendBE32(out, static_cast<uint32_t>(data.size()));
    out.resize(16, 0);
    for (size_t i = 0; i < data.size(); i += 8) {
        out.push_back(0xFF);
        for (size_t j = i; j < i + 8 && j < data.size(); j++) {
            out.push_back(data[j]);
        }
    }
    return out;
}

std::vector<uint8_t> Pattern(size_t size, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<uint8_t> out(size);
    for (auto& b : out) {
        b = static_cast<uint8_t>(rng());
    }
    return out;
}

std::filesystem::path MakeTempDir(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / ("torch_tests_" + name + "_" + std::to_string(std::random_device{}()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Missing file " + path.string());
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

} // namespace TestUtils

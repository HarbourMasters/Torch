#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace TestUtils {

void PutBE32(std::vector<uint8_t>& buf, size_t offset, uint32_t value);
void PutBE16(std::vector<uint8_t>& buf, size_t offset, uint16_t value);
uint32_t GetBE32(const std::vector<uint8_t>& buf, size_t offset);
uint16_t GetBE16(const std::vector<uint8_t>& buf, size_t offset);
void AppendBE32(std::vector<uint8_t>& buf, uint32_t value);
void Align(std::vector<uint8_t>& buf, size_t alignment);

std::vector<uint8_t> Yaz0Literal(const std::vector<uint8_t>& data);

std::vector<uint8_t> Pattern(size_t size, uint32_t seed);

std::filesystem::path MakeTempDir(const std::string& name);
void WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& data);
void WriteText(const std::filesystem::path& path, const std::string& text);
std::vector<uint8_t> ReadFile(const std::filesystem::path& path);

} // namespace TestUtils

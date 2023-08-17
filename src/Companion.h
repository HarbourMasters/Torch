#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

class Companion {
public:
    static Companion* Instance;
    Companion(const std::filesystem::path& rom) : gRomPath(rom) {}
    void Start();
    void ProcessAssets();
private:
    std::filesystem::path gRomPath;
    std::vector<uint8_t> gRomData;
};
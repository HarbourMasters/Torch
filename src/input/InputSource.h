#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace N64 {
class Cartridge;
}

class InputSource {
public:
    virtual ~InputSource() = default;

    virtual void Open() = 0;
    virtual void Build(YAML::Node& rom) {}

    virtual std::string GetHash() = 0;
    virtual std::string GetTitle() = 0;
    virtual uint32_t GetVersionCRC() = 0;
    virtual std::vector<uint8_t>& GetImage() = 0;
    virtual void LogInfo() = 0;

    virtual const std::unordered_map<std::string, uint32_t>& GetFileOffsets() const { return mFileOffsets; }
    virtual const std::unordered_map<std::string, uint32_t>& GetFileAddresses() const { return mFileAddresses; }
    virtual const std::unordered_map<std::string, uint32_t>& GetFileSizes() const { return mFileSizes; }
    virtual const std::unordered_map<uint32_t, uint32_t>& GetVirtualSegments() const { return mVirtualSegments; }

    virtual N64::Cartridge* GetCartridge() { return nullptr; }
    virtual bool IsN64() const { return false; }

    static std::shared_ptr<InputSource> Create(const std::optional<std::filesystem::path>& path,
                                               std::vector<uint8_t>& data);

protected:
    std::unordered_map<std::string, uint32_t> mFileOffsets;
    std::unordered_map<std::string, uint32_t> mFileAddresses;
    std::unordered_map<std::string, uint32_t> mFileSizes;
    std::unordered_map<uint32_t, uint32_t> mVirtualSegments;
};

#pragma once

#include "BlockReader.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GC {

struct FstEntry {
    uint64_t offset;
    uint32_t size;
};

class IDiscFileSystem {
public:
    virtual ~IDiscFileSystem() = default;
    virtual bool Exists(const std::string& path) = 0;
    virtual std::vector<uint8_t> ReadFile(const std::string& path) = 0;
    virtual std::string GetGameId() = 0;
    virtual uint8_t GetRevision() = 0;
};

class GcmDisc : public IDiscFileSystem {
public:
    static constexpr uint32_t MAGIC = 0xC2339F3D;
    static constexpr uint32_t BOOT_SIZE = 0x440;

    explicit GcmDisc(std::shared_ptr<IBlockReader> reader);

    bool Exists(const std::string& path) override;
    std::vector<uint8_t> ReadFile(const std::string& path) override;
    std::string GetGameId() override { return mGameId; }
    uint8_t GetRevision() override { return mRevision; }

    const std::map<std::string, FstEntry>& GetFiles() const { return mFiles; }

    static bool Probe(IBlockReader& reader);

private:
    void ParseFst(const std::vector<uint8_t>& fst);
    uint32_t GetDolSize();

    std::shared_ptr<IBlockReader> mReader;
    std::string mGameId;
    uint8_t mRevision;
    uint32_t mDolOffset;
    uint32_t mFstOffset;
    uint32_t mFstSize;
    std::map<std::string, FstEntry> mFiles;
};

class FolderDisc : public IDiscFileSystem {
public:
    explicit FolderDisc(std::filesystem::path root);

    bool Exists(const std::string& path) override;
    std::vector<uint8_t> ReadFile(const std::string& path) override;
    std::string GetGameId() override;
    uint8_t GetRevision() override;

    static bool Probe(const std::filesystem::path& root);

private:
    std::filesystem::path mRoot;
    std::optional<std::vector<uint8_t>> mBoot;
    void LoadBoot();
};

uint32_t DolFileSize(const uint8_t* header);

} // namespace GC

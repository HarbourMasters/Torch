#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <vector>

namespace GC {

class IBlockReader {
public:
    virtual ~IBlockReader() = default;
    virtual void Read(uint64_t offset, void* dst, size_t size) = 0;
    virtual uint64_t Size() const = 0;

    std::vector<uint8_t> Read(uint64_t offset, size_t size) {
        std::vector<uint8_t> out(size);
        Read(offset, out.data(), size);
        return out;
    }
};

class RawImageReader : public IBlockReader {
public:
    explicit RawImageReader(const std::filesystem::path& path);
    void Read(uint64_t offset, void* dst, size_t size) override;
    uint64_t Size() const override { return mSize; }

private:
    std::ifstream mFile;
    uint64_t mSize;
    std::mutex mMutex;
};

class MemoryBlockReader : public IBlockReader {
public:
    explicit MemoryBlockReader(std::vector<uint8_t> data) : mData(std::move(data)) {}
    void Read(uint64_t offset, void* dst, size_t size) override;
    uint64_t Size() const override { return mData.size(); }

private:
    std::vector<uint8_t> mData;
};

class CisoReader : public IBlockReader {
public:
    explicit CisoReader(std::shared_ptr<IBlockReader> base);
    void Read(uint64_t offset, void* dst, size_t size) override;
    uint64_t Size() const override { return mSize; }

    static bool Probe(IBlockReader& base);

private:
    std::shared_ptr<IBlockReader> mBase;
    uint32_t mBlockSize;
    uint64_t mSize;
    std::vector<int64_t> mBlockOffsets;
};

class GczReader : public IBlockReader {
public:
    explicit GczReader(std::shared_ptr<IBlockReader> base);
    void Read(uint64_t offset, void* dst, size_t size) override;
    uint64_t Size() const override { return mDataSize; }

    static bool Probe(IBlockReader& base);

private:
    void LoadBlock(uint32_t index);

    std::shared_ptr<IBlockReader> mBase;
    uint64_t mDataSize;
    uint32_t mBlockSize;
    uint64_t mDataStart;
    uint64_t mCompressedSize;
    std::vector<uint64_t> mBlockPointers;
    std::vector<uint8_t> mCache;
    int64_t mCachedBlock = -1;
    std::mutex mMutex;
};

std::shared_ptr<IBlockReader> OpenDiscImage(const std::filesystem::path& path);
std::shared_ptr<IBlockReader> WrapDiscImage(std::shared_ptr<IBlockReader> base);

uint32_t ReadBE32(const uint8_t* p);
uint16_t ReadBE16(const uint8_t* p);
void WriteBE32(uint8_t* p, uint32_t v);
void WriteBE16(uint8_t* p, uint16_t v);

} // namespace GC

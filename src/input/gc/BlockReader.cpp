#include "BlockReader.h"

#include <cstring>
#include <stdexcept>
#include <string>

#if __has_include(<zlib.h>)
#include <zlib.h>
#define GCZ_HAS_ZLIB
#endif

namespace GC {

static constexpr uint32_t CISO_HEADER_SIZE = 0x8000;
static constexpr uint32_t CISO_MAP_SIZE = CISO_HEADER_SIZE - 8;
static constexpr uint32_t GCZ_MAGIC = 0xB10BC001;
static constexpr uint32_t GCZ_HEADER_SIZE = 32;

uint32_t ReadBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint16_t ReadBE16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

void WriteBE32(uint8_t* p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

void WriteBE16(uint8_t* p, uint16_t v) {
    p[0] = v >> 8;
    p[1] = v;
}

static uint32_t ReadLE32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

static uint64_t ReadLE64(const uint8_t* p) {
    return uint64_t(ReadLE32(p)) | (uint64_t(ReadLE32(p + 4)) << 32);
}

RawImageReader::RawImageReader(const std::filesystem::path& path) : mFile(path, std::ios::binary) {
    if (!mFile.is_open()) {
        throw std::runtime_error("Failed to open disc image " + path.string());
    }
    mFile.seekg(0, std::ios::end);
    mSize = static_cast<uint64_t>(mFile.tellg());
}

void RawImageReader::Read(uint64_t offset, void* dst, size_t size) {
    if (offset + size > mSize) {
        throw std::runtime_error("Disc read out of range at 0x" + std::to_string(offset));
    }
    std::lock_guard<std::mutex> lock(mMutex);
    mFile.clear();
    mFile.seekg(static_cast<std::streamoff>(offset));
    mFile.read(static_cast<char*>(dst), static_cast<std::streamsize>(size));
    if (!mFile) {
        throw std::runtime_error("Disc read failed at 0x" + std::to_string(offset));
    }
}

void MemoryBlockReader::Read(uint64_t offset, void* dst, size_t size) {
    if (offset + size > mData.size()) {
        throw std::runtime_error("Disc read out of range at 0x" + std::to_string(offset));
    }
    std::memcpy(dst, mData.data() + offset, size);
}

bool CisoReader::Probe(IBlockReader& base) {
    if (base.Size() < CISO_HEADER_SIZE) {
        return false;
    }
    uint8_t magic[4];
    base.Read(0, magic, 4);
    return std::memcmp(magic, "CISO", 4) == 0;
}

CisoReader::CisoReader(std::shared_ptr<IBlockReader> base) : mBase(std::move(base)) {
    auto header = mBase->Read(0, CISO_HEADER_SIZE);
    mBlockSize = ReadLE32(header.data() + 4);
    if (mBlockSize == 0) {
        throw std::runtime_error("CISO: invalid block size");
    }

    mBlockOffsets.resize(CISO_MAP_SIZE, -1);
    uint64_t next = CISO_HEADER_SIZE;
    uint32_t last = 0;
    for (uint32_t i = 0; i < CISO_MAP_SIZE; i++) {
        if (header[8 + i] == 1) {
            mBlockOffsets[i] = static_cast<int64_t>(next);
            next += mBlockSize;
            last = i + 1;
        }
    }
    mSize = static_cast<uint64_t>(last) * mBlockSize;
}

void CisoReader::Read(uint64_t offset, void* dst, size_t size) {
    auto* out = static_cast<uint8_t*>(dst);
    while (size > 0) {
        const uint64_t block = offset / mBlockSize;
        const uint64_t inner = offset % mBlockSize;
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(size, mBlockSize - inner));
        if (block < mBlockOffsets.size() && mBlockOffsets[block] >= 0) {
            mBase->Read(static_cast<uint64_t>(mBlockOffsets[block]) + inner, out, chunk);
        } else {
            std::memset(out, 0, chunk);
        }
        out += chunk;
        offset += chunk;
        size -= chunk;
    }
}

bool GczReader::Probe(IBlockReader& base) {
    if (base.Size() < GCZ_HEADER_SIZE) {
        return false;
    }
    uint8_t magic[4];
    base.Read(0, magic, 4);
    return ReadLE32(magic) == GCZ_MAGIC;
}

GczReader::GczReader(std::shared_ptr<IBlockReader> base) : mBase(std::move(base)) {
    auto header = mBase->Read(0, GCZ_HEADER_SIZE);
    mCompressedSize = ReadLE64(header.data() + 8);
    mDataSize = ReadLE64(header.data() + 16);
    mBlockSize = ReadLE32(header.data() + 24);
    const uint32_t numBlocks = ReadLE32(header.data() + 28);
    if (mBlockSize == 0) {
        throw std::runtime_error("GCZ: invalid block size");
    }

    auto pointers = mBase->Read(GCZ_HEADER_SIZE, static_cast<size_t>(numBlocks) * 8);
    mBlockPointers.resize(numBlocks);
    for (uint32_t i = 0; i < numBlocks; i++) {
        mBlockPointers[i] = ReadLE64(pointers.data() + i * 8);
    }
    mDataStart = GCZ_HEADER_SIZE + static_cast<uint64_t>(numBlocks) * 12;
}

void GczReader::LoadBlock(uint32_t index) {
    if (mCachedBlock == index) {
        return;
    }
    constexpr uint64_t uncompressedFlag = 1ULL << 63;
    const uint64_t start = mBlockPointers[index] & ~uncompressedFlag;
    const uint64_t end = index + 1 < mBlockPointers.size() ? (mBlockPointers[index + 1] & ~uncompressedFlag)
                                                           : mCompressedSize;
    auto raw = mBase->Read(mDataStart + start, static_cast<size_t>(end - start));
    mCache.assign(mBlockSize, 0);

    if (mBlockPointers[index] & uncompressedFlag) {
        std::memcpy(mCache.data(), raw.data(), std::min<size_t>(raw.size(), mBlockSize));
    } else {
#ifdef GCZ_HAS_ZLIB
        uLongf destLen = mBlockSize;
        if (uncompress(mCache.data(), &destLen, raw.data(), static_cast<uLong>(raw.size())) != Z_OK) {
            throw std::runtime_error("GCZ: failed to inflate block " + std::to_string(index));
        }
#else
        throw std::runtime_error("GCZ: built without zlib");
#endif
    }
    mCachedBlock = index;
}

void GczReader::Read(uint64_t offset, void* dst, size_t size) {
    if (offset + size > mDataSize) {
        throw std::runtime_error("GCZ read out of range at 0x" + std::to_string(offset));
    }
    std::lock_guard<std::mutex> lock(mMutex);
    auto* out = static_cast<uint8_t*>(dst);
    while (size > 0) {
        const uint32_t block = static_cast<uint32_t>(offset / mBlockSize);
        const uint64_t inner = offset % mBlockSize;
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(size, mBlockSize - inner));
        LoadBlock(block);
        std::memcpy(out, mCache.data() + inner, chunk);
        out += chunk;
        offset += chunk;
        size -= chunk;
    }
}

std::shared_ptr<IBlockReader> WrapDiscImage(std::shared_ptr<IBlockReader> base) {
    if (CisoReader::Probe(*base)) {
        return std::make_shared<CisoReader>(base);
    }
    if (GczReader::Probe(*base)) {
        return std::make_shared<GczReader>(base);
    }
    return base;
}

std::shared_ptr<IBlockReader> OpenDiscImage(const std::filesystem::path& path) {
    return WrapDiscImage(std::make_shared<RawImageReader>(path));
}

} // namespace GC

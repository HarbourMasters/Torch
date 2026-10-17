#include "GcmDisc.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace GC {

uint32_t DolFileSize(const uint8_t* header) {
    uint32_t size = 0x100;
    for (int i = 0; i < 18; i++) {
        const uint32_t offset = ReadBE32(header + i * 4);
        const uint32_t length = ReadBE32(header + 0x90 + i * 4);
        if (offset != 0 && length != 0) {
            size = std::max(size, offset + length);
        }
    }
    return size;
}

bool GcmDisc::Probe(IBlockReader& reader) {
    if (reader.Size() < BOOT_SIZE) {
        return false;
    }
    uint8_t magic[4];
    reader.Read(0x1C, magic, 4);
    return ReadBE32(magic) == MAGIC;
}

GcmDisc::GcmDisc(std::shared_ptr<IBlockReader> reader) : mReader(std::move(reader)) {
    auto boot = mReader->Read(0, BOOT_SIZE);
    if (ReadBE32(boot.data() + 0x1C) != MAGIC) {
        throw std::runtime_error("Not a GameCube disc image");
    }
    mGameId = std::string(reinterpret_cast<const char*>(boot.data()), 6);
    mRevision = boot[0x07];
    mDolOffset = ReadBE32(boot.data() + 0x420);
    mFstOffset = ReadBE32(boot.data() + 0x424);
    mFstSize = ReadBE32(boot.data() + 0x428);
    ParseFst(mReader->Read(mFstOffset, mFstSize));
}

void GcmDisc::ParseFst(const std::vector<uint8_t>& fst) {
    if (fst.size() < 12 || !(fst[0] & 1)) {
        throw std::runtime_error("GCM: malformed FST root");
    }
    const uint32_t count = ReadBE32(fst.data() + 8);
    const size_t stringTable = static_cast<size_t>(count) * 12;
    if (stringTable > fst.size()) {
        throw std::runtime_error("GCM: FST entry count exceeds FST size");
    }

    auto nameAt = [&](uint32_t offset) {
        const size_t start = stringTable + offset;
        if (start >= fst.size()) {
            throw std::runtime_error("GCM: FST name out of range");
        }
        size_t end = start;
        while (end < fst.size() && fst[end] != 0) {
            end++;
        }
        return std::string(reinterpret_cast<const char*>(fst.data() + start), end - start);
    };

    std::vector<std::pair<uint32_t, std::string>> dirs = { { count, "" } };
    for (uint32_t i = 1; i < count; i++) {
        while (dirs.size() > 1 && i >= dirs.back().first) {
            dirs.pop_back();
        }
        const uint8_t* e = fst.data() + i * 12;
        const bool isDir = e[0] & 1;
        const std::string name = nameAt(ReadBE32(e) & 0xFFFFFF);
        const std::string path = dirs.back().second + name;
        if (isDir) {
            dirs.emplace_back(ReadBE32(e + 8), path + "/");
        } else {
            mFiles["files/" + path] = { ReadBE32(e + 4), ReadBE32(e + 8) };
        }
    }
}

uint32_t GcmDisc::GetDolSize() {
    auto header = mReader->Read(mDolOffset, 0x100);
    return DolFileSize(header.data());
}

bool GcmDisc::Exists(const std::string& path) {
    if (path == "sys/main.dol" || path == "sys/boot.bin" || path == "sys/bi2.bin" || path == "sys/fst.bin") {
        return true;
    }
    return mFiles.contains(path);
}

std::vector<uint8_t> GcmDisc::ReadFile(const std::string& path) {
    if (path == "sys/main.dol") {
        return mReader->Read(mDolOffset, GetDolSize());
    }
    if (path == "sys/boot.bin") {
        return mReader->Read(0, 0x440);
    }
    if (path == "sys/bi2.bin") {
        return mReader->Read(0x440, 0x2000);
    }
    if (path == "sys/fst.bin") {
        return mReader->Read(mFstOffset, mFstSize);
    }
    auto it = mFiles.find(path);
    if (it == mFiles.end()) {
        throw std::runtime_error("GCM: file not found: " + path);
    }
    return mReader->Read(it->second.offset, it->second.size);
}

FolderDisc::FolderDisc(std::filesystem::path root) : mRoot(std::move(root)) {}

bool FolderDisc::Probe(const std::filesystem::path& root) {
    std::error_code ec;
    return std::filesystem::is_directory(root, ec) && std::filesystem::exists(root / "sys" / "main.dol", ec);
}

bool FolderDisc::Exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(mRoot / path, ec);
}

std::vector<uint8_t> FolderDisc::ReadFile(const std::string& path) {
    std::ifstream input(mRoot / path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Folder disc: file not found: " + path);
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

void FolderDisc::LoadBoot() {
    if (!mBoot.has_value()) {
        mBoot = Exists("sys/boot.bin") ? ReadFile("sys/boot.bin") : std::vector<uint8_t>();
    }
}

std::string FolderDisc::GetGameId() {
    LoadBoot();
    if (mBoot->size() < 8) {
        return "GAMECUBE";
    }
    return std::string(reinterpret_cast<const char*>(mBoot->data()), 6);
}

uint8_t FolderDisc::GetRevision() {
    LoadBoot();
    return mBoot->size() < 8 ? 0 : (*mBoot)[7];
}

} // namespace GC

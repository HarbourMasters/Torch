#include "ZWrapper.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <cstring>

#include "spdlog/spdlog.h"
#include <Companion.h>
#include <miniz/zip_file.hpp>

namespace fs = std::filesystem;

ZWrapper::ZWrapper(const std::string& path) {
    this->mPath = path;
}

ZWrapper::~ZWrapper() {
    if (this->mZip != nullptr) {
        if (this->mOpen) {
            mz_zip_writer_end(reinterpret_cast<mz_zip_archive*>(this->mZip));
        }
        delete reinterpret_cast<mz_zip_archive*>(this->mZip);
        this->mZip = nullptr;
    }
}

int32_t ZWrapper::CreateArchive(void) {
    auto* zip = new mz_zip_archive;
    std::memset(zip, 0, sizeof(mz_zip_archive));

    if (!mz_zip_writer_init_file(zip, this->mPath.c_str(), 0)) {
        SPDLOG_ERROR("[ZWrapper] Could not open '{}' for writing", this->mPath);
        delete zip;
        return -1;
    }

    this->mZip = reinterpret_cast<mz_zip_archive_tag*>(zip);
    this->mOpen = true;
    SPDLOG_INFO("Loaded ZIP (O2R) archive: {}", this->mPath.c_str());
    return 0;
}

bool ZWrapper::AddFile(const std::string& path, std::vector<char> data) {
    const size_t fileSize = data.size();

    if (Companion::Instance != nullptr && Companion::Instance->IsDebug()) {
        SPDLOG_INFO("Creating debug file: debug/{}", path);
        std::string dpath = "debug/" + path;
        if (!fs::exists(fs::path(dpath).parent_path())) {
            fs::create_directories(fs::path(dpath).parent_path());
        }
        std::ofstream stream(dpath, std::ios::binary);
        stream.write(data.data(), fileSize);
        stream.close();
    }

    if (this->mZip == nullptr || !this->mOpen) {
        SPDLOG_ERROR("[ZWrapper] Cannot add '{}': the archive is not open", path);
        return false;
    }

    std::lock_guard<std::mutex> guard(this->mMutex);

    if (!mz_zip_writer_add_mem(reinterpret_cast<mz_zip_archive*>(this->mZip), path.c_str(), data.data(), fileSize,
                               MZ_DEFAULT_COMPRESSION)) {
        // miniz's own error says nothing useful, so log which entry blew up and how big.
        SPDLOG_ERROR("[ZWrapper] Failed to add '{}' ({} bytes)", path, fileSize);
        return false;
    }

    return true;
}

int32_t ZWrapper::Close(void) {
    if (this->mZip == nullptr || !this->mOpen) {
        return -1;
    }

    auto* zip = reinterpret_cast<mz_zip_archive*>(this->mZip);
    int32_t result = 0;

    if (!mz_zip_writer_finalize_archive(zip)) {
        SPDLOG_ERROR("[ZWrapper] Failed to finalize '{}'", this->mPath);
        result = -1;
    }
    mz_zip_writer_end(zip);
    this->mOpen = false;

    return result;
}

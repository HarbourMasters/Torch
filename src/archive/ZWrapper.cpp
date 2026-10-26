#include "ZWrapper.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <cstring>
#include <stdexcept>

#include "spdlog/spdlog.h"
#include <Companion.h>
#include <miniz/zip_file.hpp>

namespace fs = std::filesystem;

ZWrapper::ZWrapper(const std::string& path) {
    this->mPath = path;
    this->mTempPath = path + ".tmp";
}

ZWrapper::~ZWrapper() {
    if (this->mZip != nullptr) {
        if (this->mOpen) {
            mz_zip_writer_end(reinterpret_cast<mz_zip_archive*>(this->mZip));
        }
        delete reinterpret_cast<mz_zip_archive*>(this->mZip);
        this->mZip = nullptr;
    }
    if (!this->mFinalized) {
        std::error_code ec;
        fs::remove(this->mTempPath, ec);
    }
}

int32_t ZWrapper::CreateArchive(void) {
    auto* zip = new mz_zip_archive;
    std::memset(zip, 0, sizeof(mz_zip_archive));

    if (!mz_zip_writer_init_file(zip, this->mTempPath.c_str(), 0)) {
        delete zip;
        throw std::runtime_error("[ZWrapper] Could not open '" + this->mTempPath + "' for writing");
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
        throw std::runtime_error("[ZWrapper] Cannot add '" + path + "': the archive is not open");
    }

    std::lock_guard<std::mutex> guard(this->mMutex);

    if (!mz_zip_writer_add_mem(reinterpret_cast<mz_zip_archive*>(this->mZip), path.c_str(), data.data(), fileSize,
                               MZ_BEST_COMPRESSION)) {
        // miniz's own error says nothing useful, so log which entry blew up and how big.
        SPDLOG_ERROR("[ZWrapper] Failed to add '{}' ({} bytes)", path, fileSize);
        throw std::runtime_error("[ZWrapper] Failed to add '" + path + "'");
    }

    return true;
}

int32_t ZWrapper::Close(void) {
    if (this->mZip == nullptr || !this->mOpen) {
        return -1;
    }

    auto* zip = reinterpret_cast<mz_zip_archive*>(this->mZip);
    const bool finalized = mz_zip_writer_finalize_archive(zip);
    mz_zip_writer_end(zip);
    this->mOpen = false;

    if (!finalized) {
        throw std::runtime_error("[ZWrapper] Failed to finalize '" + this->mPath + "'");
    }

    std::error_code ec;
    fs::rename(this->mTempPath, this->mPath, ec);
    if (ec) {
        throw std::runtime_error("[ZWrapper] Could not move '" + this->mTempPath + "' to '" + this->mPath +
                                 "': " + ec.message());
    }
    this->mFinalized = true;

    return 0;
}

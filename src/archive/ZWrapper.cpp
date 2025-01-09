#include "ZWrapper.h"
#include <filesystem>
#include <iostream>
#include <fstream>

#include "spdlog/spdlog.h"
#include <Companion.h>

namespace fs = std::filesystem;

ZWrapper::ZWrapper(const std::string& path) {
    mPath = path;
}

int32_t ZWrapper::CreateArchive(void) {
    int openErr;
    zip_t* zip;

    if(fs::exists(mPath)) {
        fs::remove(mPath);
    }
    
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        zip = zip_open(mPath.c_str(), ZIP_CREATE, &openErr);
    }
    
    if (zip == nullptr) {
        zip_error_t error;
        zip_error_init_with_code(&error, openErr);
        SPDLOG_ERROR("Failed to create ZIP (O2R) file. Error: {}", zip_error_strerror(&error));
        zip_error_fini(&error);
        return -1;
    }
    mZip = zip;
    SPDLOG_INFO("Loaded ZIP (O2R) archive: {}", mPath.c_str());
    return 0;
}

bool ZWrapper::CreateFile(const std::string& path, std::vector<char> data) {
    char* fileData = data.data();
    size_t fileSize = data.size();
    zip_source_t* source = zip_source_buffer(mZip, fileData, fileSize, 0);

    if (source == nullptr) {
        zip_error_t* zipError = zip_get_error(mZip);
        SPDLOG_ERROR("Failed to create ZIP source. Error: {}", zip_error_strerror(zipError));
        zip_source_free(source);
        zip_error_fini(zipError);
        return false;
    }

    if (zip_file_add(mZip, path.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0) {
        zip_error_t* zipError = zip_get_error(mZip);
        SPDLOG_ERROR("Failed to add file to ZIP. Error: {}", zip_error_strerror(zipError));
        zip_source_free(source);
        zip_error_fini(zipError);
        return false;
    }

    return true;
}

int32_t ZWrapper::Close(void) {
    int err;
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        err = zip_close(mZip);
    }
    if (err < 0) {
        zip_error_t* zipError = zip_get_error(mZip);
        SPDLOG_ERROR("Failed to close ZIP (O2R) file. Error: {}", zip_error_strerror(zipError));
        printf("fail\n");
        zip_error_fini(zipError);
        return -1;
    }

    return 0;
}

#include "SWrapper.h"
#include <filesystem>
#include <iostream>
#include <fstream>

#include "spdlog/spdlog.h"

namespace fs = std::filesystem;
bool debugMode = true;

SWrapper::SWrapper(const std::string& path) {
    if(fs::exists(path)) {
        fs::remove(path);
    }

    if(!SFileCreateArchive(path.c_str(), MPQ_CREATE_LISTFILE | MPQ_CREATE_ATTRIBUTES | MPQ_CREATE_ARCHIVE_V2, 4096, &this->hMpq)){
        SPDLOG_ERROR("Failed to create archive {} with error code {}", path, GetLastError());
        return;
    }
}

bool SWrapper::CreateFile(const std::string& path, std::vector<char> data) {
    if(debugMode){
        SPDLOG_INFO("Creating debug file: debug/{}", path);
        std::string dpath = "debug/" + path;
        if(!fs::exists(fs::path(dpath).parent_path())){
            fs::create_directories(fs::path(dpath).parent_path());
        }
        std::ofstream stream(dpath, std::ios::binary);
        stream.write((char*)data.data(), data.size());
        stream.close();
    }

    HANDLE hFile;
#ifdef _WIN32
    SYSTEMTIME sysTime;
    GetSystemTime(&sysTime);
    FILETIME t;
    SystemTimeToFileTime(&sysTime, &t);
    ULONGLONG theTime = static_cast<uint64_t>(t.dwHighDateTime) << (sizeof(t.dwHighDateTime) * 8) | t.dwLowDateTime;
#else
    time_t theTime;
    time(&theTime);
#endif

    char* raw = (char*) data.data();
    size_t size = data.size();

    if(size == 0){
        SPDLOG_ERROR("File at path {} is empty", path);
        std::cout << "File empty: " << path << std::endl;
        return false;
    }

    if(size >> 32){
        SPDLOG_ERROR("File at path {} is too large with size {}", path, size);
        return false;
    }

    if(!SFileCreateFile(this->hMpq, path.c_str(), theTime, size, 0, MPQ_FILE_COMPRESS, &hFile)){
        SPDLOG_ERROR("Failed to create file at path {} with error {}", path, GetLastError());
        return false;
    }

    if(!SFileWriteFile(hFile, (void*) raw, size, MPQ_COMPRESSION_ZLIB)){
        SPDLOG_ERROR("Failed to write file at path {} with error {}", path, GetLastError());
        return false;
    }

    if(!SFileCloseFile(hFile)){
        SPDLOG_ERROR("Failed to close file at path {} with error {}", path, GetLastError());
        return false;
    }

    return true;
}

void SWrapper::Close() {
    if(this->hMpq == nullptr) {
        SPDLOG_ERROR("Archive already closed");
        return;
    }
    SFileCloseArchive(this->hMpq);
}
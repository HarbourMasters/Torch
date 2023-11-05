#include "SWrapper.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

SWrapper::SWrapper(const std::string& path) {
    if(fs::exists(path)) {
        fs::remove(path);
    }

    if(!SFileCreateArchive(path.c_str(), MPQ_CREATE_LISTFILE | MPQ_CREATE_ATTRIBUTES | MPQ_CREATE_ARCHIVE_V2, 4096, &this->hMpq)){
        std::cout << "Failed to create archive: " << path << std::endl;
        std::cout << GetLastError() << std::endl;
        return;
    }
}

bool SWrapper::CreateFile(const std::string& path, std::vector<char> data) {
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
        std::cout << "File empty: " << path << std::endl;
        return false;
    }

    if(size >> 32){
        std::cout << "File too large: " << path << std::endl;
        return false;
    }

    if(!SFileCreateFile(this->hMpq, path.c_str(), theTime, size, 0, MPQ_FILE_COMPRESS, &hFile)){
        std::cout << "Failed to create file: " << path << std::endl;
        std::cout << GetLastError() << std::endl;
        return false;
    }

    if(!SFileWriteFile(hFile, (void*) raw, size, MPQ_COMPRESSION_ZLIB)){
        std::cout << "Failed to write file: " << path << std::endl;
        std::cout << GetLastError() << std::endl;
        return false;
    }

    if(!SFileCloseFile(hFile)){
        std::cout << "Failed to close file: " << path << std::endl;
        std::cout << GetLastError() << std::endl;
        return false;
    }

    return true;
}

void SWrapper::Close() {
    if(this->hMpq == nullptr) {
        std::cout << "Archive already closed" << std::endl;
        return;
    }
    SFileCloseArchive(this->hMpq);
}
#pragma once

#include <vector>
#include <string>
#include <StormLib/src/StormLib.h>
#include "BinaryWrapper.h"

class SWrapper : public BinaryWrapper {
public:
    SWrapper(const std::string& path);

    int32_t CreateArchive(void);
    bool CreateFile(const std::string& path, std::vector<char> data);
    int32_t Close(void);
private:
    HANDLE hMpq{};
};

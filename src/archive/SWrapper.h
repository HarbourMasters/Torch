#pragma once

#include <vector>
#include <string>
#include <StormLib/src/StormLib.h>
#include "BinaryWrapper.h"

class SWrapper : public BinaryWrapper {
public:
    explicit SWrapper(const std::string& path);

    int32_t CreateArchive(void) override;
    bool CreateFile(const std::string& path, std::vector<char> data) override;
    int32_t Close(void) override;
private:
    HANDLE hMpq{};
};

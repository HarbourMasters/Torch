#pragma once

#include <vector>
#include <string>
#include "BinaryWrapper.h"

class ZWrapper : public BinaryWrapper {
public:
    explicit ZWrapper(const std::string& path);

    int32_t CreateArchive(void) override;
    bool AddFile(const std::string& path, std::vector<char> data) override;
    int32_t Close(void) override;
};

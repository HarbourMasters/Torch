#pragma once

#include <vector>
#include <string>
#include <zip.h>
#include "BinaryWrapper.h"

class ZWrapper : public BinaryWrapper {
public:
    ZWrapper(const std::string& path);

    int32_t CreateArchive(void);
    bool CreateFile(const std::string& path, std::vector<char> data);
    int32_t Close(void);
private:
    zip_t* mZip;
};

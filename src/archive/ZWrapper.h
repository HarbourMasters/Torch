#pragma once

#include <vector>
#include <string>
#include "BinaryWrapper.h"

struct mz_zip_archive_tag;

class ZWrapper : public BinaryWrapper {
public:
    explicit ZWrapper(const std::string& path);
    ~ZWrapper() override;

    int32_t CreateArchive(void) override;
    bool AddFile(const std::string& path, std::vector<char> data) override;
    int32_t Close(void) override;

private:
    struct mz_zip_archive_tag* mZip = nullptr;
    bool mOpen = false;
};

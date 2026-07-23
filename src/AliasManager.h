#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "archive/BinaryWrapper.h"

class AliasManager {
public:
    static AliasManager* Instance;

    void Register(const std::string& primaryPath, const std::string& aliasPath);
    void WriteAliases(const std::string& primaryPath, BinaryWrapper* wrapper,
                      const std::vector<char>& data);
    void Clear();

private:
    std::unordered_map<std::string, std::vector<std::string>> mAliases;
};

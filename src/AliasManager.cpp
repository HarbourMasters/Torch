#include "AliasManager.h"
#include "utils/TorchUtils.h"

static AliasManager sAliasManagerInstance;
AliasManager* AliasManager::Instance = &sAliasManagerInstance;

void AliasManager::Register(const std::string& primaryPath, const std::string& aliasPath) {
    mAliases[primaryPath].push_back(aliasPath);
}

void AliasManager::WriteAliases(const std::string& primaryPath, BinaryWrapper* wrapper,
                                const std::vector<char>& data) {
    if (!Torch::contains(mAliases, primaryPath)) return;

    for (auto& alias : mAliases[primaryPath]) {
        wrapper->AddFile(alias, data);
    }
    mAliases.erase(primaryPath);
}

void AliasManager::Clear() {
    mAliases.clear();
}

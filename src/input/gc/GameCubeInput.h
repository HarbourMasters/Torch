#pragma once

#include "input/InputSource.h"
#include "GcmDisc.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace GC {

struct Window {
    uint32_t segment;
    uint32_t imageOffset;
    uint32_t size;
};

class GameCubeInput : public InputSource {
public:
    explicit GameCubeInput(std::shared_ptr<IDiscFileSystem> disc) : mDisc(std::move(disc)) {}

    static std::shared_ptr<GameCubeInput> FromPath(const std::filesystem::path& path);
    static std::shared_ptr<GameCubeInput> FromMemory(std::vector<uint8_t>& data);

    void Open() override;
    void Build(YAML::Node& rom) override;

    std::string GetHash() override { return mDolHash; }
    std::string GetTitle() override { return mTitle; }
    uint32_t GetVersionCRC() override { return mVersionCRC; }
    std::vector<uint8_t>& GetImage() override { return mImage; }
    void LogInfo() override;

    const std::vector<Window>& GetWindows() const { return mWindows; }
    const std::unordered_map<std::string, std::string>& GetRelHashSemantics() const { return mRelHashSemantics; }

private:
    struct Placement {
        uint32_t segment;
        uint32_t offset;
        std::vector<uint8_t> data;
        std::vector<std::string> names;
    };

    void PlaceDol(YAML::Node node, std::vector<Placement>& out);
    void PlaceRel(YAML::Node node, std::vector<Placement>& out);
    void PlaceFile(YAML::Node node, std::vector<Placement>& out);
    void Assemble(std::vector<Placement>& placements);

    std::shared_ptr<IDiscFileSystem> mDisc;
    std::vector<uint8_t> mDol;
    std::string mDolHash;
    std::string mTitle;
    uint8_t mRevision = 0;
    uint32_t mVersionCRC = 0;
    std::vector<uint8_t> mImage;
    std::vector<Window> mWindows;
    std::unordered_map<std::string, std::string> mRelHashSemantics;
};

std::string Sha1Hex(const std::vector<uint8_t>& data);

} // namespace GC

#pragma once

#include "factories/BaseFactory.h"

#include <array>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace AC {

struct DListTarget {
    std::string path;
    YAML::Node node;
    uint32_t offset = 0;
};

class DListResolver {
public:
    virtual ~DListResolver() = default;
    virtual std::optional<DListTarget> FindAsset(uint32_t addr) = 0;
    virtual std::optional<DListTarget> FindVtx(uint32_t addr) = 0;
    virtual bool IsRgb5a3Lossless(uint32_t addr, uint32_t width, uint32_t height) = 0;
    virtual void OnDirectTexture(uint32_t addr) {}
    virtual std::optional<uint32_t> MapRuntimeBuffer(uint32_t addr) { return std::nullopt; }
    virtual std::vector<DListTarget> FindVariants(uint32_t addr) { return {}; }
    virtual std::optional<uint32_t> DlIndex(uint32_t segment, uint32_t offset, const std::string& name) {
        return std::nullopt;
    }
};

constexpr uint32_t DL_V1_PREFIX = 2;
constexpr uint32_t NO_INDEX = 0xFFFFFFFF;

using Triangle = std::array<uint8_t, 3>;

size_t DecodeTrin(const uint32_t* words, size_t gfxCount, std::vector<Triangle>& out);

size_t CommandLength(const uint32_t* words, size_t gfxCount);

std::vector<uint32_t> NormalizeDisplayList(const std::vector<uint32_t>& words, DListResolver& resolver,
                                           const std::string& name, std::vector<uint32_t>* indexMap = nullptr);

void WriteDisplayListV1(std::ostream& write, const std::vector<uint32_t>& words, const std::string& path);

namespace DListHelpers {

std::optional<ExportResult> Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                   YAML::Node& node, std::string* replacement);

std::optional<std::optional<std::shared_ptr<IParsedData>>> Parse(std::vector<uint8_t>& raw_buffer, YAML::Node& node);

void Reset();
void Finalize();

} // namespace DListHelpers
} // namespace AC

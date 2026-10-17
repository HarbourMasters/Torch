#pragma once

#include "factories/BaseFactory.h"

#include <string>
#include <vector>

namespace AC {

enum class StructArrayType : uint32_t { U8 = 0, S8 = 1, U16 = 2, S16 = 3, U32 = 4, S32 = 5, U64 = 6, F32 = 7 };

struct StructArrayField {
    uint32_t size;
    uint32_t repeat;
    StructArrayType type;
};

class StructArrayData : public IParsedData {
public:
    StructArrayType mType;
    uint32_t mCount;
    std::vector<uint8_t> mBytes;

    StructArrayData(StructArrayType type, uint32_t count, std::vector<uint8_t> bytes)
        : mType(type), mCount(count), mBytes(std::move(bytes)) {}
};

class StructArrayBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class StructArrayFactory : public BaseFactory {
public:
    explicit StructArrayFactory(std::vector<std::string> fixedLayout = {}) : mFixedLayout(std::move(fixedLayout)) {}
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, StructArrayBinaryExporter)
        };
    }

private:
    std::vector<std::string> mFixedLayout;
};

} // namespace AC

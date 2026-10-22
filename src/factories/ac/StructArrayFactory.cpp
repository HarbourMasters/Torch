#include "StructArrayFactory.h"
#include "ACUtils.h"
#include "utils/TorchUtils.h"

#include <map>

namespace AC {

static StructArrayField ParseField(const std::string& spec) {
    static const std::map<std::string, std::pair<uint32_t, StructArrayType>> types = {
        { "u8", { 1, StructArrayType::U8 } },   { "s8", { 1, StructArrayType::S8 } },
        { "pad", { 1, StructArrayType::U8 } },  { "u16", { 2, StructArrayType::U16 } },
        { "s16", { 2, StructArrayType::S16 } }, { "u32", { 4, StructArrayType::U32 } },
        { "s32", { 4, StructArrayType::S32 } }, { "f32", { 4, StructArrayType::F32 } },
    };
    const auto star = spec.find('*');
    const auto base = spec.substr(0, star);
    const uint32_t repeat = star == std::string::npos ? 1 : (uint32_t)std::stoul(spec.substr(star + 1), nullptr, 0);
    const auto it = types.find(base);
    if (it == types.end() || repeat == 0) {
        throw std::runtime_error("AC:STRUCT_ARRAY: bad field '" + spec + "'");
    }
    return { it->second.first, repeat, it->second.second };
}

ExportResult StructArrayBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                               std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto data = std::static_pointer_cast<StructArrayData>(raw);
    auto writer = LUS::BinaryWriter();
    WriteHeader(writer, Torch::ResourceType::GenericArray, 0);
    writer.Write((uint32_t)data->mType);
    writer.Write(data->mCount);
    const uint8_t* p = data->mBytes.data();
    for (uint32_t i = 0; i < data->mCount; i++) {
        switch (data->mType) {
            case StructArrayType::U8:
            case StructArrayType::S8:
                writer.Write(p[i]);
                break;
            case StructArrayType::U16:
            case StructArrayType::S16:
                writer.Write(Be16(p + i * 2));
                break;
            case StructArrayType::U32:
            case StructArrayType::S32:
            case StructArrayType::F32:
                writer.Write(Be32(p + i * 4));
                break;
            default:
                throw std::runtime_error("AC:STRUCT_ARRAY: unsupported element type");
        }
    }
    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> StructArrayFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto name = NameOf(node);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    std::vector<std::string> specs = mFixedLayout;
    if (specs.empty()) {
        if (!node["fields"] || !node["fields"].IsSequence()) {
            throw std::runtime_error(name + ": AC:STRUCT_ARRAY needs `fields`");
        }
        for (const auto& f : node["fields"]) {
            specs.push_back(f.as<std::string>());
        }
    }
    std::vector<StructArrayField> fields;
    uint32_t stride = 0;
    for (const auto& spec : specs) {
        fields.push_back(ParseField(spec));
        stride += fields.back().size * fields.back().repeat;
    }

    uint32_t count;
    if (node["count"]) {
        count = GetSafeNode<uint32_t>(node, "count");
    } else {
        const auto size = GetSafeNode<uint32_t>(node, "size");
        if (size % stride != 0) {
            throw std::runtime_error(name + ": size 0x" + Torch::to_hex(size, false) +
                                     " is not a multiple of the record size 0x" + Torch::to_hex(stride, false));
        }
        count = size / stride;
    }
    auto raw = ReadBytes(offset, count * stride, name);

    if (fields.size() == 1) {
        return std::make_shared<StructArrayData>(fields[0].type, count * fields[0].repeat, std::move(raw));
    }

    std::vector<uint8_t> bytes(raw.size());
    size_t pos = 0;
    for (uint32_t r = 0; r < count; r++) {
        for (const auto& f : fields) {
            for (uint32_t k = 0; k < f.repeat; k++, pos += f.size) {
                for (uint32_t b = 0; b < f.size; b++) {
                    bytes[pos + b] = raw[pos + f.size - 1 - b];
                }
            }
        }
    }
    return std::make_shared<StructArrayData>(StructArrayType::U8, count * stride, std::move(bytes));
}

} // namespace AC

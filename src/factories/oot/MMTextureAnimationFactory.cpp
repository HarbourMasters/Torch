#include "MMTextureAnimationFactory.h"
#include "OoTSceneUtils.h"
#include "spdlog/spdlog.h"
#include "Companion.h"

namespace OoT {

// ZAPD's TextureAnimationParamsType
enum class TexAnimType : int16_t {
    SingleScroll = 0,
    DualScroll = 1,
    ColorChange = 2,
    ColorChangeLERP = 3,
    ColorChangeLagrange = 4,
    TextureCycle = 5,
    Empty = 6,
};

class MMTextureAnimationData : public IParsedData {
public:
    std::vector<char> mBinary;
    explicit MMTextureAnimationData(std::vector<char> data) : mBinary(std::move(data)) {}
};

// Resolve a texture pointer to its asset path, for the TextureCycle list.
static std::string ResolveTexturePath(uint32_t ptr) {
    auto node = Companion::Instance->GetNodeByAddr(ptr);
    if (node.has_value()) {
        return std::get<0>(node.value());
    }
    SPDLOG_ERROR("MM texture animation: texture not found: 0x{:X}", ptr);
    return "";
}

std::vector<char> SerializeTextureAnimation(std::vector<uint8_t>& buffer, uint32_t segAddr,
                                            const std::string& resPath) {
    LUS::BinaryWriter w;
    BaseExporter::WriteHeader(w, Torch::ResourceType::MMTextureAnimation, 0);

    // The entry list is 8 bytes per entry -- segment (s8), type (s16 at +2),
    // paramsPtr (u32 at +4) -- and runs until an entry whose segment is <= 0, which
    // is itself included.
    struct Entry {
        int8_t segment;
        int16_t type;
        uint32_t paramsPtr;
    };
    std::vector<Entry> entries;
    for (uint32_t i = 0;; i++) {
        auto reader = ReadSubArray(buffer, segAddr + i * 8, 8);
        Entry e{};
        e.segment = static_cast<int8_t>(reader.ReadUByte());
        reader.ReadUByte();
        e.type = reader.ReadInt16();
        e.paramsPtr = reader.ReadUInt32();
        entries.push_back(e);
        if (e.segment <= 0) {
            break;
        }
        if (i > 64) { // no real list is this long; avoid running away on bad data
            SPDLOG_WARN("MM texture animation at 0x{:X}: entry list did not terminate", segAddr);
            break;
        }
    }

    w.Write(static_cast<uint32_t>(entries.size()));

    for (const auto& e : entries) {
        w.Write(e.segment);
        w.Write(e.type);

        switch (static_cast<TexAnimType>(e.type)) {
            case TexAnimType::SingleScroll:
            case TexAnimType::DualScroll: {
                const int count = (static_cast<TexAnimType>(e.type) == TexAnimType::DualScroll) ? 2 : 1;
                auto p = ReadSubArray(buffer, e.paramsPtr, count * 4);
                for (int r = 0; r < count; r++) {
                    w.Write(static_cast<int8_t>(p.ReadUByte())); // xStep
                    w.Write(static_cast<int8_t>(p.ReadUByte())); // yStep
                    w.Write(p.ReadUByte());                      // width
                    w.Write(p.ReadUByte());                      // height
                }
                break;
            }
            case TexAnimType::ColorChange:
            case TexAnimType::ColorChangeLERP:
            case TexAnimType::ColorChangeLagrange: {
                auto p = ReadSubArray(buffer, e.paramsPtr, 0x10);
                uint16_t animLength = p.ReadUInt16();
                uint16_t colorListCount = p.ReadUInt16();
                uint32_t primColorListAddr = p.ReadUInt32();
                uint32_t envColorListAddr = p.ReadUInt32();
                uint32_t frameDataListAddr = p.ReadUInt32();

                // Type 2 sizes its lists by animLength; 3 and 4 by colorListCount.
                const uint16_t listLength =
                    (static_cast<TexAnimType>(e.type) == TexAnimType::ColorChange) ? animLength : colorListCount;

                w.Write(animLength);
                w.Write(colorListCount);

                if (frameDataListAddr != 0) {
                    auto f = ReadSubArray(buffer, frameDataListAddr, listLength * 2);
                    w.Write(static_cast<uint16_t>(listLength));
                    for (uint16_t i = 0; i < listLength; i++) {
                        w.Write(f.ReadUInt16());
                    }
                } else {
                    w.Write(static_cast<uint16_t>(0));
                }

                if (primColorListAddr != 0) {
                    auto c = ReadSubArray(buffer, primColorListAddr, listLength * 5);
                    w.Write(static_cast<uint16_t>(listLength));
                    for (uint16_t i = 0; i < listLength; i++) {
                        for (int b = 0; b < 5; b++) { // r, g, b, a, lodFrac
                            w.Write(c.ReadUByte());
                        }
                    }
                } else {
                    w.Write(static_cast<uint16_t>(0));
                }

                if (envColorListAddr != 0) {
                    auto c = ReadSubArray(buffer, envColorListAddr, listLength * 4);
                    w.Write(static_cast<uint16_t>(listLength));
                    for (uint16_t i = 0; i < listLength; i++) {
                        for (int b = 0; b < 4; b++) { // r, g, b, a
                            w.Write(c.ReadUByte());
                        }
                    }
                } else {
                    w.Write(static_cast<uint16_t>(0));
                }
                break;
            }
            case TexAnimType::TextureCycle: {
                auto p = ReadSubArray(buffer, e.paramsPtr, 0x0C);
                uint16_t cycleLength = p.ReadUInt16();
                p.ReadUInt16(); // padding
                uint32_t textureListAddr = p.ReadUInt32();
                uint32_t textureIndexListAddr = p.ReadUInt32();

                // The index list is cycleLength bytes; the texture list is sized by
                // the largest index it names, inclusive.
                auto idx = ReadSubArray(buffer, textureIndexListAddr, cycleLength);
                std::vector<uint8_t> indices;
                uint8_t maxIndex = 0;
                for (uint16_t i = 0; i < cycleLength; i++) {
                    uint8_t v = idx.ReadUByte();
                    indices.push_back(v);
                    maxIndex = std::max(maxIndex, v);
                }

                const uint32_t textureCount = static_cast<uint32_t>(maxIndex) + 1;
                auto tex = ReadSubArray(buffer, textureListAddr, textureCount * 4);

                w.Write(cycleLength);
                w.Write(textureCount);
                for (uint32_t i = 0; i < textureCount; i++) {
                    w.Write(ResolveTexturePath(tex.ReadUInt32()));
                }
                for (const auto v : indices) {
                    w.Write(v);
                }
                break;
            }
            case TexAnimType::Empty: {
                w.Write(static_cast<uint32_t>(0)); // SEGMENTED_NULL
                break;
            }
            default: {
                SPDLOG_ERROR("MM texture animation {}: unknown params type {}", resPath, e.type);
                break;
            }
        }
    }

    std::stringstream ss;
    w.Finish(ss);
    auto str = ss.str();
    return std::vector<char>(str.begin(), str.end());
}

std::optional<std::shared_ptr<IParsedData>> MMTextureAnimationFactory::parse(std::vector<uint8_t>& buffer,
                                                                            YAML::Node& node) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto symbol = GetSafeNode<std::string>(node, "symbol", "");
    auto data = SerializeTextureAnimation(buffer, offset, symbol);
    if (data.empty()) {
        return std::nullopt;
    }
    return std::make_shared<MMTextureAnimationData>(std::move(data));
}

ExportResult MMTextureAnimationBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                      std::string& entryName, YAML::Node& node,
                                                      std::string* replacement) {
    auto anim = std::static_pointer_cast<MMTextureAnimationData>(raw);
    write.write(anim->mBinary.data(), anim->mBinary.size());
    return std::nullopt;
}

} // namespace OoT

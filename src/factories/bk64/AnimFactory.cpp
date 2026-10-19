#include "AnimFactory.h"

#include "BKByteUtils.h"
#include "BKEmitText.h"
#include "BKResource.h"
#include "Companion.h"
#include "spdlog/spdlog.h"
#include "types/RawBuffer.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

namespace BK64 {

ExportResult AnimCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                      YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto anim = std::static_pointer_cast<AnimData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "AnimationFile " << symbol << "_File = { " << anim->mStartFrame << ", " << anim->mEndFrame << ", "
          << anim->mFiles.size() << " };\n\n";

    write << "AnimationFileElement " << symbol << "_Data[] = {\n";
    for (const auto& file : anim->mFiles) {
        write << fourSpaceTab << "{\n";
        write << fourSpaceTab << fourSpaceTab << file.mBoneIndex << ", " << file.mTransformType << ", "
              << file.mData.size() << ",\n";
        for (const auto& fileData : file.mData) {
            write << fourSpaceTab << fourSpaceTab << "{ ";
            write << (uint32_t)fileData.unk0_15 << ", " << (uint32_t)fileData.unk0_14 << ", " << fileData.unk0_13
                  << ", " << fileData.unk2;
            write << " },\n";
        }
        write << fourSpaceTab << "},\n";
    }

    write << "};\n\n";

    return offset;
}

ExportResult BK64::AnimBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto bytes = WriteAnimResource(*std::static_pointer_cast<AnimData>(raw));
    write.write((const char*)bytes.data(), bytes.size());
    return std::nullopt;
}

ExportResult BK64::AnimModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                               std::string& entryName, YAML::Node& node, std::string* replacement) {
    *replacement += ".yaml";
    const std::string yaml =
        AnimToYaml(*std::static_pointer_cast<AnimData>(raw), GetSafeNode(node, "symbol", entryName));
    write.write(yaml.data(), yaml.size());
    return std::nullopt;
}

std::shared_ptr<AnimData> ReadAnimRom(const std::vector<uint8_t>& bytes, std::string& error) {
    size_t at = 0;
    const auto next = [&]() {
        if (at + 2 > bytes.size()) {
            throw std::runtime_error("the animation ends early");
        }
        at += 2;
        return ReadU16BE(&bytes[at - 2]);
    };
    try {
        const int16_t startFrame = (int16_t)next();
        const int16_t endFrame = (int16_t)next();
        const uint16_t fileCount = next();
        next();
        std::vector<AnimationFile> files;
        for (uint16_t i = 0; i < fileCount; i++) {
            const uint16_t element = next();
            const uint16_t dataCount = next();
            std::vector<AnimationFileData> data(dataCount);
            for (auto& key : data) {
                const uint16_t bits = next();
                key.unk0_15 = bits >> 15;
                key.unk0_14 = (bits >> 14) & 1;
                key.unk0_13 = bits & 0x3FFF;
                key.unk2 = (int16_t)next();
            }
            files.emplace_back((int16_t)(element >> 4), (int16_t)(element & 0xF), std::move(data));
        }
        return std::make_shared<AnimData>(startFrame, endFrame, std::move(files));
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}

std::vector<uint8_t> WriteAnimRom(const AnimData& anim) {
    std::vector<uint8_t> out;
    const auto put = [&](uint16_t value) {
        out.push_back((uint8_t)(value >> 8));
        out.push_back((uint8_t)value);
    };
    put(anim.mStartFrame);
    put(anim.mEndFrame);
    put((uint16_t)anim.mFiles.size());
    put(0);
    for (const auto& file : anim.mFiles) {
        put((uint16_t)(file.mBoneIndex << 4 | (file.mTransformType & 0xF)));
        put((uint16_t)file.mData.size());
        for (const auto& key : file.mData) {
            put((uint16_t)(key.unk0_15 << 15 | key.unk0_14 << 14 | key.unk0_13));
            put(key.unk2);
        }
    }
    return out;
}

std::shared_ptr<AnimData> ReadAnimResource(const std::vector<uint8_t>& resource, std::string& error) {
    try {
        ResourceReader reader(resource);
        int16_t startFrame, endFrame;
        uint32_t fileCount;
        reader.Read(startFrame);
        reader.Read(endFrame);
        reader.Read(fileCount);
        std::vector<AnimationFile> files;
        for (uint32_t i = 0; i < fileCount; i++) {
            int16_t bone, type;
            uint32_t dataCount;
            reader.Read(bone);
            reader.Read(type);
            reader.Read(dataCount);
            if (dataCount > resource.size()) {
                throw std::runtime_error("the resource ends early");
            }
            std::vector<AnimationFileData> data(dataCount);
            for (auto& key : data) {
                uint8_t flag15, flag14;
                uint16_t frame;
                reader.Read(flag15);
                reader.Read(flag14);
                reader.Read(frame);
                reader.Read(key.unk2);
                key.unk0_15 = flag15;
                key.unk0_14 = flag14;
                key.unk0_13 = frame;
            }
            files.emplace_back(bone, type, std::move(data));
        }
        return std::make_shared<AnimData>(startFrame, endFrame, std::move(files));
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}

std::vector<uint8_t> WriteAnimResource(const AnimData& anim) {
    auto writer = LUS::BinaryWriter();
    BaseExporter::WriteHeader(writer, Torch::ResourceType::BKAnimation, 0);

    writer.Write(anim.mStartFrame);
    writer.Write(anim.mEndFrame);
    writer.Write((uint32_t)anim.mFiles.size());

    for (const auto& file : anim.mFiles) {
        writer.Write(file.mBoneIndex);
        writer.Write(file.mTransformType);
        writer.Write((uint32_t)file.mData.size());
        for (const auto& fileData : file.mData) {
            writer.Write(static_cast<uint8_t>(fileData.unk0_15));
            writer.Write(static_cast<uint8_t>(fileData.unk0_14));
            writer.Write(static_cast<uint16_t>(fileData.unk0_13));
            writer.Write(fileData.unk2);
        }
    }
    const auto bytes = writer.ToVector();
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

std::string AnimToYaml(const AnimData& anim, const std::string& symbol) {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << symbol;
    out << YAML::Value;
    out.SetIndent(2);

    out << YAML::BeginMap;
    out << YAML::Key << "StartFrame";
    out << YAML::Value << anim.mStartFrame;
    out << YAML::Key << "EndFrame";
    out << YAML::Value << anim.mEndFrame;
    out << YAML::Key << "Elements";
    out << YAML::Value;

    out << YAML::BeginSeq;
    for (const auto& file : anim.mFiles) {
        out << YAML::BeginMap;
        out << YAML::Key << "BoneIndex";
        out << YAML::Value << file.mBoneIndex;
        out << YAML::Key << "TransformType";
        out << YAML::Value << file.mTransformType;
        out << YAML::Key << "Data";
        out << YAML::Value;
        out << YAML::BeginSeq;
        for (const auto& fileData : file.mData) {
            out << YAML::Flow << YAML::BeginSeq;
            out << YAML::Value << (uint32_t)fileData.unk0_15;
            out << YAML::Value << (uint32_t)fileData.unk0_14;
            out << YAML::Value << fileData.unk0_13;
            out << YAML::Value << fileData.unk2;
            out << YAML::EndSeq;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;
    out << YAML::EndMap;
    return std::string(out.c_str()) + "\n";
}

bool AnimFromYaml(const std::string& text, AnimData& out, std::string& error) {
    try {
        const YAML::Node root = YAML::Load(text);
        if (!root.IsMap() || root.size() != 1) {
            YamlFail("animation", "needs one top-level key, the asset's name");
        }
        const YAML::Node info = root.begin()->second;
        if (!info["Elements"]) {
            YamlFail("animation", "needs Elements");
        }
        std::vector<AnimationFile> files;
        const YAML::Node elements = info["Elements"];
        for (size_t i = 0; i < elements.size(); i++) {
            const YAML::Node elem = elements[i];
            const std::string where = "Elements " + std::to_string(i);
            if (!elem["Data"]) {
                YamlFail(where, "needs Data");
            }
            std::vector<AnimationFileData> data;
            for (size_t k = 0; k < elem["Data"].size(); k++) {
                const YAML::Node key = elem["Data"][k];
                const std::string at = where + " Data " + std::to_string(k);
                if (!key.IsSequence() || key.size() != 4) {
                    YamlFail(at, "needs 4 numbers");
                }
                AnimationFileData entry;
                entry.unk0_15 = YamlNumber(key[0], at, 1);
                entry.unk0_14 = YamlNumber(key[1], at, 1);
                entry.unk0_13 = YamlNumber(key[2], at, 0x3FFF);
                entry.unk2 = YamlShort(key[3], at);
                data.push_back(entry);
            }
            files.emplace_back((int16_t)YamlNumber(elem, "BoneIndex", where, 0xFFF),
                               (int16_t)YamlNumber(elem, "TransformType", where, 0xF), std::move(data));
        }
        out = AnimData(YamlShort(info, "StartFrame", "animation"), YamlShort(info, "EndFrame", "animation"),
                       std::move(files));
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::optional<std::shared_ptr<IParsedData>> AnimFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    std::string error;
    auto anim = ReadAnimRom(std::vector<uint8_t>(segment.data, segment.data + segment.size), error);
    if (!anim) {
        SPDLOG_ERROR("Animation {}: {}", GetSafeNode<std::string>(node, "symbol", ""), error);
        return std::nullopt;
    }
    return anim;
}

std::optional<std::shared_ptr<IParsedData>> AnimFactory::parse_modding(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto anim = std::make_shared<AnimData>(0, 0, std::vector<AnimationFile>());
    std::string error;
    if (!AnimFromYaml(std::string((char*)buffer.data(), buffer.size()), *anim, error)) {
        YamlFail(GetSafeNode<std::string>(node, "symbol"), error);
    }
    return anim;
}

} // namespace BK64

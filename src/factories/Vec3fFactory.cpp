#include "Vec3fFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"

#define FORMAT_FLOAT(x, w, p) std::dec << std::setfill(' ') << std::fixed << std::setprecision(p) << std::setw(w) << x

Vec3fData::Vec3fData(std::vector<Vec3f> vecs): mVecs(vecs) {
    mMaxPrec = 1;
    mMaxWidth = 3;
    for(Vec3f v : vecs) {
        mMaxPrec = std::max(mMaxPrec, v.precision());
        mMaxWidth = std::max(mMaxWidth, v.width());
    }
}

ExportResult Vec3fHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if(Companion::Instance->IsOTRMode()){
        write << "static const char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern Vec3f " << symbol << "[];\n";
    return std::nullopt;
}


int GetPrecision(Vec3f v) {
    return std::max(std::max(GetPrecision(v.x), GetPrecision(v.y)), GetPrecision(v.z));
}

ExportResult Vec3fCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto vecData = std::static_pointer_cast<Vec3fData>(raw);
    auto off = offset;
    int i;

    if(IS_SEGMENTED(off)) {
        off = SEGMENT_OFFSET(off);
    }
    if (Companion::Instance->IsDebug()) {
        write << "// 0x" << std::uppercase << std::hex << off << "\n";
    }

    write << "Vec3f " << symbol << "[] = {";

    int cols = 120 / (3 * vecData->mMaxWidth + 8);
    i = 0;
    for(Vec3f v : vecData->mVecs) {
        if((i++ % cols) == 0) {
            write << "\n" << fourSpaceTab;
        }
        write << FORMAT_FLOAT(v, vecData->mMaxWidth, vecData->mMaxPrec) << ", ";
    }

    write << "\n};\n";

    if (Companion::Instance->IsDebug()) {
        write << "// Count: " << vecData->mVecs.size() << " Vec3fs\n";
    }

    return (IS_SEGMENTED(off) ? SEGMENT_OFFSET(off) : off) + vecData->mVecs.size() * sizeof(Vec3f);
}

ExportResult Vec3fBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> Vec3fFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    std::vector<Vec3f> vecs;
    const auto count = GetSafeNode<int>(node, "count");
    auto [root, segment] = Decompressor::AutoDecode(node, buffer, 0x1000);
    LUS::BinaryReader reader(segment.data, segment.size);
    reader.SetEndianness(LUS::Endianness::Big);

    for(int i = 0; i < count; i++) {
        auto vx = reader.ReadFloat();
        auto vy = reader.ReadFloat();
        auto vz = reader.ReadFloat();

        vecs.push_back(Vec3f(vx, vy, vz));
    }

    return std::make_shared<Vec3fData>(vecs);
}

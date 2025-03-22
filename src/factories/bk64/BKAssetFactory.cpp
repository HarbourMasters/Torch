#include "BKAssetFactory.h"
//#include "librarezip.h"

#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include "types/RawBuffer.h"
//#include "librarezip.h"

// just a note: bk64 usv1.0/pal assets.bin offset = 0x5E90

namespace BK64 {

ExportResult AssetHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                         YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern BK64::Asset " << symbol << ";\n";

    return std::nullopt;
}

ExportResult AssetCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                       YAML::Node& node, std::string* replacement) {
    /* example minimal yaml entry from SF64
    D_TI1_7006F74:
    { type: SF64:ANIM, offset: 0x7006F74, symbol: D_TI1_7006F74 }
    */
    /*asset from BK64 usv1.,
    anim_002e:
    {type: BK64:BINARY, offset: 0x0000c528, symbol: 002e, compressed: 1, size: 6488, t_flag: 3, subtype:
    BK64::ANIMATION}
    */
   auto symbol = GetSafeNode(node, "symbol", entryName);
   auto offset = GetSafeNode<uint32_t>(node, "offset");
   auto type = GetSafeNode<std::string>(node, "subtype");

   auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

   if(Companion::Instance->IsOTRMode()){
       write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
       return std::nullopt;
   }

   write << GetSafeNode<std::string>(node, "ctype", "u8") << " " << symbol << "[] = {\n" << tab_t;

   for (int i = 0; i < data.size(); i++) {
       if ((i % 15 == 0) && i != 0) {
           write << "\n" << tab_t;
       }

       write << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int) data[i] << ", ";
   }
   write << "\n};\n";

   if (Companion::Instance->IsDebug()) {
       write << "// size: 0x" << std::hex << std::uppercase << data.size() << "\n";
   }

   return offset + data.size();
}

ExportResult BK64::AssetBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                               std::string& entryName, YAML::Node& node, std::string* replacement) {
    /* example minimal yaml entry from SF64
    D_TI1_7006F74:
    { type: SF64:ANIM, offset: 0x7006F74, symbol: D_TI1_7006F74 }
    */
    /*asset from BK64 usv1.0
    anim_002e:
    {type: BK64:BINARY, offset: 0x0000c528, symbol: 002e, compressed: 1, size: 6488, t_flag: 3, subtype:
    BK64::ANIMATION}
    */
    auto writer = LUS::BinaryWriter();
    auto data = std::static_pointer_cast<RawBuffer>(raw)->mBuffer;

    WriteHeader(writer, Torch::ResourceType::None, 0);
    writer.Write((uint32_t) data.size());
    writer.Write((char*) data.data(), data.size());
    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> AssetFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto size = GetSafeNode<size_t>(node, "size");
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    return std::make_shared<RawBuffer>(segment.data, segment.size);
}

} // namespace BK64
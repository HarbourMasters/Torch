#include "BaseFactory.h"

#ifdef BUILD_UI
#include <sstream>
#include <unordered_map>
#include "Companion.h"
#include "ui/Widgets.h"

float BaseFactoryUI::GetItemHeight(const ParseResultData&) {
    return UI::AssetCardHeight();
}

void BaseFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    UI::BeginAssetBody();

    // Exporter output is stable per asset; cache it instead of re-exporting
    // every frame.
    static std::unordered_map<std::string, std::string> sPreviewCache;
    auto cached = sPreviewCache.find(item.name);
    if (cached == sPreviewCache.end()) {
        std::string text;
        const auto factory = Companion::Instance->GetFactory(item.type);
        if (factory.has_value() && factory.value()->CanPreviewCode() && item.data.has_value()) {
            if (const auto exporter = factory.value()->GetExporter(ExportType::Code); exporter.has_value()) {
                try {
                    std::ostringstream out;
                    std::string entryName = item.name;
                    std::string replacement = item.name;
                    YAML::Node node = item.node;
                    exporter.value()->Export(out, item.data.value(), entryName, node, &replacement);
                    text = out.str();
                } catch (const std::exception& e) {
                    text = std::string("// preview unavailable: ") + e.what();
                } catch (...) {
                    text = "// preview unavailable";
                }
            }
        }

        if (text.empty()) {
            // No code preview: fall back to the asset's YAML config.
            YAML::Node node = item.node;
            text = YAML::Dump(node);
        }
        cached = sPreviewCache.emplace(item.name, std::move(text)).first;
    }
    const std::string& text = cached->second;

    ImGui::TextUnformatted(text.c_str());
    UI::EndAssetBody();
}
#endif

void BaseExporter::WriteHeader(LUS::BinaryWriter& writer, Torch::ResourceType resType, int32_t version) {
    writer.Write((int8_t)(Torch::Endianness::Native)); // 0x00 - Endianness
    writer.Write((int8_t)0);                           // 0x01 - Is Asset Custom
    writer.Write((int8_t)0);                           // 0x02 -
    writer.Write((int8_t)0);                           // 0x03
    writer.Write((uint32_t)resType);                   // 0x04
    writer.Write((uint32_t)version);                   // 0x08
    writer.Write((uint64_t)0xDEADBEEFDEADBEEF);        // id, 0x0C
    writer.Write((uint32_t)0);                         // 0x10
    writer.Write((uint64_t)0);                         // ROM CRC, 0x14
    writer.Write((uint32_t)0);                         // ROM Enum, 0x1C

    while (writer.GetBaseAddress() < 0x40)
        writer.Write((uint32_t)0); // To be used at a later date!
}
#include "TextPreview.h"

#ifdef BUILD_UI

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "Companion.h"
#include "DialogFactory.h"
#include "DictionaryFactory.h"
#include "imgui.h"
#include "types/RawBuffer.h"
#include "ui/Widgets.h"

namespace SM64 {
namespace {

// SM64 US charmap (generated from the decomp charmap.txt), byte -> UTF-8.
// 0x9E space and 0xFE/0xFF control codes are handled in DecodeText. Bytes with
// no US glyph (Japanese kana, multi-byte lead bytes) fall through to \xNN.
const std::unordered_map<uint8_t, const char*>& Charmap() {
    static const std::unordered_map<uint8_t, const char*> kMap = {
        { 0x00, "0" },  { 0x01, "1" },  { 0x02, "2" },  { 0x03, "3" },  { 0x04, "4" },   { 0x05, "5" },
        { 0x06, "6" },  { 0x07, "7" },  { 0x08, "8" },  { 0x09, "9" },  { 0x0A, "A" },   { 0x0B, "B" },
        { 0x0C, "C" },  { 0x0D, "D" },  { 0x0E, "E" },  { 0x0F, "F" },  { 0x10, "G" },   { 0x11, "H" },
        { 0x12, "I" },  { 0x13, "J" },  { 0x14, "K" },  { 0x15, "L" },  { 0x16, "M" },   { 0x17, "N" },
        { 0x18, "O" },  { 0x19, "P" },  { 0x1A, "Q" },  { 0x1B, "R" },  { 0x1C, "S" },   { 0x1D, "T" },
        { 0x1E, "U" },  { 0x1F, "V" },  { 0x20, "W" },  { 0x21, "X" },  { 0x22, "Y" },   { 0x23, "Z" },
        { 0x24, "a" },  { 0x25, "b" },  { 0x26, "c" },  { 0x27, "d" },  { 0x28, "e" },   { 0x29, "f" },
        { 0x2A, "g" },  { 0x2B, "h" },  { 0x2C, "i" },  { 0x2D, "j" },  { 0x2E, "k" },   { 0x2F, "l" },
        { 0x30, "m" },  { 0x31, "n" },  { 0x32, "o" },  { 0x33, "p" },  { 0x34, "q" },   { 0x35, "r" },
        { 0x36, "s" },  { 0x37, "t" },  { 0x38, "u" },  { 0x39, "v" },  { 0x3A, "w" },   { 0x3B, "x" },
        { 0x3C, "y" },  { 0x3D, "z" },  { 0x3E, "'" },  { 0x3F, "." },
        { 0x50, "\xe2\x96\xb2" }, { 0x51, "\xe2\x96\xbc" }, { 0x52, "\xe2\x97\x80" }, { 0x53, "\xe2\x96\xb6" },
        { 0x54, "[A]" }, { 0x55, "[B]" }, { 0x56, "[C]" }, { 0x57, "[Z]" }, { 0x58, "[R]" },
        { 0x60, "\xc3\xa0" }, { 0x61, "\xc3\xa2" }, { 0x62, "\xc3\xa4" }, { 0x64, "\xc3\x80" },
        { 0x65, "\xc3\x82" }, { 0x66, "\xc3\x84" }, { 0x6F, "," },        { 0x70, "\xc3\xa8" },
        { 0x71, "\xc3\xaa" }, { 0x72, "\xc3\xab" }, { 0x73, "\xc3\xa9" }, { 0x74, "\xc3\x88" },
        { 0x75, "\xc3\x8a" }, { 0x76, "\xc3\x8b" }, { 0x77, "\xc3\x89" }, { 0x80, "\xc3\xb9" },
        { 0x81, "\xc3\xbb" }, { 0x82, "\xc3\xbc" }, { 0x84, "\xc3\x99" }, { 0x85, "\xc3\x9b" },
        { 0x86, "\xc3\x9c" }, { 0x91, "\xc3\xb4" }, { 0x92, "\xc3\xb6" }, { 0x95, "\xc3\x94" },
        { 0x96, "\xc3\x96" }, { 0x9F, "-" },        { 0xA1, "\xc3\xae" }, { 0xA2, "\xc3\xaf" },
        { 0xD0, "/" },        { 0xD1, "the" },      { 0xD2, "you" },
        { 0xE0, "[%]" },      { 0xE1, "(" },        { 0xE3, ")" },
        { 0xE4, "+" },        { 0xE5, "&" },        { 0xE6, ":" },        { 0xEC, "\xc3\x9f" },
        { 0xED, "\xc3\x87" }, { 0xEE, "\xc3\xa7" }, { 0xF2, "!" },        { 0xF3, "%" },
        { 0xF4, "?" },        { 0xF7, "~" },        { 0xF9, "$" },        { 0xFA, "\xe2\x98\x85" },
        { 0xFB, "\xc3\x97" }, { 0xFD, "\xe2\x98\x86" },
    };
    return kMap;
}

std::string DecodeText(const uint8_t* data, size_t len) {
    std::string out;
    const auto& map = Charmap();
    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = data[i];
        if (b == 0xFF) { // string terminator
            break;
        }
        if (b == 0xFE) { // newline
            out += '\n';
            continue;
        }
        if (b == 0x9E) { // space
            out += ' ';
            continue;
        }
        const auto it = map.find(b);
        if (it != map.end()) {
            out += it->second;
        } else {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\x%02X", b);
            out += buf;
        }
    }
    return out;
}

// Read-only wrapped text box sized to the card body.
void TextBox(const char* id, const std::string& text, float height) {
    ImGui::BeginChild(id, ImVec2(0, height), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

} // namespace

float DialogFactoryUI::GetItemHeight(const ParseResultData&) {
    return ImGui::GetTextLineHeightWithSpacing() * 2.0f + 180.0f + ImGui::GetStyle().ItemSpacing.y * 3.0f;
}

void DialogFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    const auto dialog = std::static_pointer_cast<DialogData>(item.data.value());
    ImGui::TextDisabled("dialog  \xe2\x80\x94  %u lines/box, width %u, left offset %u", dialog->mLinesPerBox,
                        dialog->mWidth, dialog->mLeftOffset);
    TextBox("##dialogtext", DecodeText(dialog->mText.data(), dialog->mText.size()), 150.0f);
}

float TextFactoryUI::GetItemHeight(const ParseResultData&) {
    return ImGui::GetTextLineHeightWithSpacing() + 120.0f + ImGui::GetStyle().ItemSpacing.y * 3.0f;
}

void TextFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    const auto& bytes = std::static_pointer_cast<RawBuffer>(item.data.value())->mBuffer;
    TextBox("##text", DecodeText(bytes.data(), bytes.size()), 100.0f);
}

float DictionaryFactoryUI::GetItemHeight(const ParseResultData&) {
    return ImGui::GetTextLineHeightWithSpacing() * 2.0f + 220.0f + ImGui::GetStyle().ItemSpacing.y * 3.0f;
}

void DictionaryFactoryUI::DrawUI(const ParseResultData& item) {
    UI::AssetHeader(item.name, item.type);
    if (!item.data.has_value()) {
        ImGui::TextDisabled("no data");
        return;
    }
    const auto dict = std::static_pointer_cast<DictionaryData>(item.data.value());
    ImGui::TextDisabled("dictionary  \xe2\x80\x94  %zu entries", dict->mDictionary.size());
    if (ImGui::BeginTable("##dicttable", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 200.0f))) {
        ImGui::TableSetupColumn("key");
        ImGui::TableSetupColumn("text");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& [key, value] : dict->mDictionary) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(key.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(DecodeText(value.data(), value.size()).c_str());
        }
        ImGui::EndTable();
    }
}

} // namespace SM64

#endif // BUILD_UI

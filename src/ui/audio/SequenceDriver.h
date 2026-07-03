#pragma once

#ifdef BUILD_UI

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Companion.h"
#include "factories/BaseFactory.h"
#include "ui/audio/SequenceSynth.h"

// Sequence-format drivers: each game/library version interprets its own
// sequence data into a RenderedAudio. Drivers register by asset type string;
// SequencePreviewUI provides the playback controls for any registered type.
namespace UI {

class SequenceDriver {
public:
    virtual ~SequenceDriver() = default;
    virtual bool Render(const ParseResultData& item, int option, RenderedAudio& out) = 0;
    // Selectable render options (e.g. sound fonts); empty hides the picker.
    virtual std::vector<std::string> Options(const ParseResultData& item) { return {}; }
    // Initial option index (e.g. the font the game maps to this sequence).
    virtual int DefaultOption(const ParseResultData& item) { return 0; }
};

inline std::unordered_map<std::string, std::shared_ptr<SequenceDriver>>& SequenceDrivers() {
    static std::unordered_map<std::string, std::shared_ptr<SequenceDriver>> drivers;
    return drivers;
}

inline void RegisterSequenceDriver(const std::string& type, std::shared_ptr<SequenceDriver> driver) {
    SequenceDrivers()[type] = std::move(driver);
}

inline SequenceDriver* GetSequenceDriver(const std::string& type) {
    auto& drivers = SequenceDrivers();
    const auto it = drivers.find(type);
    return it != drivers.end() ? it->second.get() : nullptr;
}

// Playback row (play/stop, volume, seek) for any asset type with a driver.
class SequencePreviewUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

} // namespace UI

#endif // BUILD_UI

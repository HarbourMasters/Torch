#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "factories/BaseFactory.h"

#ifdef BUILD_UI
#include "ui/audio/SequenceDriver.h"
#endif

// Preview-only PM64 audio assets. When preview mode is on, the PM64:AUDIO
// factory walks the SBN container and registers one PM64:BGM asset per song
// and one PM64:BK_SAMPLE asset per bank instrument (see pmret src/audio).
namespace PM64Audio {

// One BK instrument: VADPCM wave data plus its codebook, loop, tuning and
// envelope presets (press/release command list pairs).
struct Instrument {
    std::vector<uint8_t> wav;
    std::vector<int16_t> book; // codebook s16s (order 2)
    int32_t loopStart = 0;
    int32_t loopEnd = 0;
    int32_t loopCount = 0;
    uint16_t keyBase = 4800; // cents
    int32_t sampleRate = 32000;
    uint8_t type = 0;
    std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> envelopes;
};

struct Bank {
    char name[5] = {};
    std::shared_ptr<Instrument> instruments[16];
};

// Banks parsed from ROM, keyed by BK file ROM offset; filled during parsing so
// the BGM driver can resolve (bank set, patch) at preview time.
std::map<uint32_t, std::shared_ptr<Bank>>& BankRegistry();
std::shared_ptr<Bank> ParseBank(const std::vector<uint8_t>& rom, uint32_t off);

// Global PER drum table and PRG program table (parsed from the SBN's INIT
// extra files by the PM64:AUDIO factory).
struct DrumInfo {
    uint16_t bankPatch = 0;
    uint16_t keyBase = 4800;
    uint8_t volume = 127;
    int8_t pan = 64;
    uint8_t reverb = 0;
    uint8_t randTune = 0, randVolume = 0, randPan = 0, randReverb = 0;
};

struct ProgramInfo {
    uint16_t bankPatch = 0;
    uint8_t volume = 127;
    int8_t pan = 64;
    uint8_t reverb = 0;
    int8_t coarseTune = 0, fineTune = 0;
};

struct GlobalData {
    std::vector<DrumInfo> perDrums;
    std::vector<ProgramInfo> prgPrograms;
};
GlobalData& Globals();

// Preview asset registration is opt-in so extraction runs stay clean.
void SetPreviewAssets(bool enabled);
bool PreviewAssets();

// Walks the SBN container (big-endian ROM data) and registers the per-song
// and per-instrument preview assets. Called by the PM64:AUDIO factory.
void RegisterPreviewAssets(const std::vector<uint8_t>& rom, uint32_t sbnBase);

} // namespace PM64Audio

class PM64BkSampleData : public IParsedData {
public:
    std::shared_ptr<PM64Audio::Bank> bank;
    int slot = 0;
};

// Preview assets still need an exporter registered or ParseNode discards the
// parse result; Binary writes the raw payload.
class PM64BkSampleBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class PM64BkSampleFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, PM64BkSampleBinaryExporter)
        };
    }
};

class PM64BgmData : public IParsedData {
public:
    std::vector<uint8_t> file;
};

class PM64BgmBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class PM64BgmFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, PM64BgmBinaryExporter)
        };
    }
};

#ifdef BUILD_UI
class PM64BkSampleFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

// Offline BGM interpreter (port of pmret au_bgm_player_update_playing).
class SequencePlayerPM64 : public UI::SequenceDriver {
public:
    bool Render(const ParseResultData& item, int option, UI::RenderedAudio& out) override;
    std::vector<std::string> Options(const ParseResultData& item) override;
};
#endif

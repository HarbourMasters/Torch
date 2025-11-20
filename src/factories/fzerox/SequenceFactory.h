#pragma once

#include <factories/BaseFactory.h>
#include <unordered_set>

namespace FZX {

typedef std::variant<uint8_t, uint16_t, uint32_t> SeqArg;

enum class SeqArgType {
    U8, S16, CS16
};

enum class SequenceState {
    player,
    channel,
    layer,
    data,
    envelope
};

struct SeqLabelInfo {
    uint32_t pos;
    SequenceState state;
    uint32_t channel;
    uint32_t layer;
    bool largeNotes;
    SeqLabelInfo(uint32_t pos, SequenceState state, uint32_t channel, uint32_t layer, bool largeNotes) : pos(pos), state(state), channel(channel), layer(layer), largeNotes(largeNotes) {}
};

struct SeqCommand {
    uint8_t cmd;
    uint32_t pos;
    SequenceState state;
    int32_t channel;
    int32_t layer;
    uint32_t size;
    std::vector<SeqArg> args;
    bool operator<(const SeqCommand& command) const {
        return pos < command.pos;
    }
};

class SequenceData : public IParsedData {
public:
    std::vector<SeqCommand> mCmds;
    std::unordered_set<uint32_t> mLabels;
    bool mHasFooter;

    SequenceData(std::vector<SeqCommand> cmds, std::unordered_set<uint32_t> labels, bool hasFooter) : mCmds(std::move(cmds)), mLabels(std::move(labels)), mHasFooter(hasFooter) {}
};

class SequenceHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SequenceBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SequenceCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName, YAML::Node& node, std::string* replacement) override;
};

class SequenceFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Code, SequenceCodeExporter)
            REGISTER(Header, SequenceHeaderExporter)
            REGISTER(Binary, SequenceBinaryExporter)
        };
    }
};
}

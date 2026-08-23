#include "MusicFactory.h"

#include "Companion.h"
#include "spdlog/spdlog.h"
#include "utils/Decompressor.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

namespace BK64 {
namespace {

constexpr uint8_t kMetaPrefix = 0xFF;
constexpr uint8_t kEndOfTrack = 0x2F;
constexpr uint8_t kSetTempo = 0x51;
constexpr uint8_t kLoopStart = 0x2E;
constexpr uint8_t kLoopEnd = 0x2D;
constexpr uint8_t kBlockCode = 0xFE;
constexpr uint32_t kLoopEndBytes = 6;

class TrackReader {
  public:
    TrackReader(const uint8_t* data, size_t size, uint32_t start, uint32_t end)
        : mData(data), mSize(size), mLoc(start), mEnd(end) {
    }
    bool Failed() const {
        return mFailed;
    }
    bool AtEnd() const {
        return mBackupLen == 0 && mLoc >= mEnd;
    }
    uint32_t Loc() const {
        return mLoc;
    }
    bool InBlock() const {
        return mBackupLen != 0;
    }

    uint8_t Byte() {
        if (mBackupLen != 0) {
            const uint8_t byte = At(mBackup);
            mBackup++;
            mBackupLen--;
            return byte;
        }
        uint8_t byte = At(mLoc);
        mLoc++;
        if (byte != kBlockCode) {
            return byte;
        }
        const uint8_t next = At(mLoc);
        mLoc++;
        if (next == kBlockCode) {
            return kBlockCode;
        }
        const uint32_t high = next;
        const uint32_t low = At(mLoc);
        mLoc++;
        const uint32_t len = At(mLoc);
        mLoc++;
        const uint32_t back = (high << 8) | low;
        if (len == 0 || back + 4 > mLoc) {
            mFailed = true;
            return 0;
        }
        mBackup = mLoc - (back + 4);
        mBackupLen = len;
        byte = At(mBackup);
        mBackup++;
        mBackupLen--;
        return byte;
    }

    uint32_t VarLen() {
        uint32_t value = Byte();
        if (value & 0x80) {
            value &= 0x7F;
            uint8_t next;
            do {
                next = Byte();
                value = (value << 7) + (next & 0x7F);
            } while ((next & 0x80) && !mFailed);
        }
        return value;
    }

    const uint8_t* TakeRaw(uint32_t count) {
        if (mBackupLen != 0 || mLoc + count > mSize) {
            mFailed = true;
            return nullptr;
        }
        const uint8_t* at = mData + mLoc;
        mLoc += count;
        return at;
    }

  private:
    uint8_t At(uint32_t at) {
        if (at >= mSize) {
            mFailed = true;
            return 0;
        }
        return mData[at];
    }

    const uint8_t* mData;
    size_t mSize;
    uint32_t mLoc;
    uint32_t mEnd;
    uint32_t mBackup = 0;
    uint32_t mBackupLen = 0;
    bool mFailed = false;
};

bool DecodeTrack(const uint8_t* data, size_t size, uint32_t start, uint32_t end, std::vector<MusicEvent>& out) {
    TrackReader reader(data, size, start, end);
    uint8_t running = 0;

    while (!reader.AtEnd() && !reader.Failed()) {
        MusicEvent event;
        event.delta = reader.VarLen();
        const uint8_t status = reader.Byte();

        if (status == kMetaPrefix) {
            const uint8_t type = reader.Byte();
            if (type == kSetTempo) {
                const uint32_t high = reader.Byte(), middle = reader.Byte(), low = reader.Byte();
                event.kind = static_cast<uint8_t>(MusicEventKind::Tempo);
                event.aux = (high << 16) | (middle << 8) | low;
                running = 0;
            } else if (type == kEndOfTrack) {
                event.kind = static_cast<uint8_t>(MusicEventKind::End);
                out.push_back(event);
                return !reader.Failed();
            } else if (type == kLoopStart) {
                event.byte1 = reader.Byte();
                event.byte2 = reader.Byte();
                event.kind = static_cast<uint8_t>(MusicEventKind::LoopStart);
                running = 0;
            } else if (type == kLoopEnd) {
                const uint8_t* raw = reader.TakeRaw(kLoopEndBytes);
                if (raw == nullptr) {
                    return false;
                }
                event.byte1 = raw[0];
                event.byte2 = raw[1];
                event.aux = (static_cast<uint32_t>(raw[2]) << 24) | (static_cast<uint32_t>(raw[3]) << 16) |
                            (static_cast<uint32_t>(raw[4]) << 8) | raw[5];
                event.kind = static_cast<uint8_t>(MusicEventKind::LoopEnd);
                running = 0;
            } else {
                return false;
            }
        } else {
            event.kind = static_cast<uint8_t>(MusicEventKind::Midi);
            if (status & 0x80) {
                event.status = status;
                event.byte1 = reader.Byte();
                running = status;
            } else {
                if (running == 0) {
                    return false;
                }
                event.status = running;
                event.byte1 = status;
            }
            const uint8_t kind = event.status & 0xF0;
            if (kind != 0xC0 && kind != 0xD0) {
                event.byte2 = reader.Byte();
                if (kind == 0x90) {
                    event.aux = reader.VarLen();
                }
            }
        }
        out.push_back(event);
    }
    return !reader.Failed();
}

std::optional<uint32_t> TrackIdFromSymbol(const std::string& symbol) {
    const size_t at = symbol.rfind("COMUSIC_");
    if (at == std::string::npos) {
        return std::nullopt;
    }
    size_t pos = at + 8;
    uint32_t id = 0;
    size_t digits = 0;
    for (; pos < symbol.size() && std::isxdigit(static_cast<unsigned char>(symbol[pos])); pos++, digits++) {
        const char digit = symbol[pos];
        const uint32_t value =
            (digit <= '9') ? static_cast<uint32_t>(digit - '0') : static_cast<uint32_t>(std::tolower(digit) - 'a' + 10);
        id = id * 16 + value;
    }
    if (digits == 0) {
        return std::nullopt;
    }
    return id;
}

uint32_t VanillaVolume(const YAML::Node& node) {
    const YAML::Node& config = Companion::Instance->GetCurrentFileConfig();
    if (!node["symbol"] || !config["music_volumes"]) {
        return kDefaultMusicVolume;
    }
    const auto id = TrackIdFromSymbol(node["symbol"].as<std::string>());
    if (!id.has_value()) {
        return kDefaultMusicVolume;
    }
    const YAML::Node& entry = config["music_volumes"][static_cast<int>(*id)];
    return entry ? entry.as<uint32_t>() : kDefaultMusicVolume;
}

void PushVarLen(std::vector<uint8_t>& out, uint32_t value) {
    uint8_t buffer[5];
    int count = 0;
    buffer[count++] = static_cast<uint8_t>(value & 0x7F);
    while ((value >>= 7) != 0) {
        buffer[count++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
    }
    while (count > 0) {
        out.push_back(buffer[--count]);
    }
}

void PushBE(std::vector<uint8_t>& out, uint32_t value, int bytes) {
    for (int i = bytes - 1; i >= 0; i--) {
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

void PushTag(std::vector<uint8_t>& out, const char* tag) {
    out.insert(out.end(), tag, tag + 4);
}

struct TimedEvent {
    uint32_t tick = 0;
    int order = 0;
    std::vector<uint8_t> bytes;
};

std::vector<uint8_t> BuildSmfTrack(const MusicTrack& track, const std::string& lead = std::string()) {
    std::vector<TimedEvent> timed;
    uint32_t tick = 0;
    int sequence = 0;

    if (!lead.empty()) {
        TimedEvent meta;
        meta.tick = 0;
        meta.order = -2;
        meta.bytes = { 0xFF, 0x06, static_cast<uint8_t>(lead.size()) };
        meta.bytes.insert(meta.bytes.end(), lead.begin(), lead.end());
        timed.push_back(meta);
    }

    for (const MusicEvent& event : track.events) {
        tick += event.delta;
        const MusicEventKind kind = static_cast<MusicEventKind>(event.kind);

        if (kind == MusicEventKind::Midi) {
            TimedEvent noteOn;
            noteOn.tick = tick;
            noteOn.order = 1 + sequence++;
            noteOn.bytes = { event.status, event.byte1 };
            const uint8_t type = event.status & 0xF0;
            if (type != 0xC0 && type != 0xD0) {
                noteOn.bytes.push_back(event.byte2);
            }
            timed.push_back(noteOn);

            if (type == 0x90) {
                TimedEvent off;
                off.tick = tick + event.aux;
                off.order = -1;
                off.bytes = { static_cast<uint8_t>(0x80 | (event.status & 0x0F)), event.byte1, 0 };
                timed.push_back(off);
            }
        } else if (kind == MusicEventKind::Tempo) {
            TimedEvent meta;
            meta.tick = tick;
            meta.order = 1 + sequence++;
            meta.bytes = { 0xFF,
                           0x51,
                           0x03,
                           static_cast<uint8_t>(event.aux >> 16),
                           static_cast<uint8_t>(event.aux >> 8),
                           static_cast<uint8_t>(event.aux) };
            timed.push_back(meta);
        } else if (kind == MusicEventKind::LoopStart || kind == MusicEventKind::LoopEnd ||
                   kind == MusicEventKind::End) {
            const char* text = kind == MusicEventKind::LoopStart ? "loopStart"
                               : kind == MusicEventKind::LoopEnd ? "loopEnd"
                                                                 : "track_end";
            TimedEvent meta;
            meta.tick = tick;
            meta.order = 1 + sequence++;
            meta.bytes = { 0xFF, 0x06 };
            const size_t len = std::strlen(text);
            meta.bytes.push_back(static_cast<uint8_t>(len));
            meta.bytes.insert(meta.bytes.end(), text, text + len);
            timed.push_back(meta);
        }
    }

    std::stable_sort(timed.begin(), timed.end(), [](const TimedEvent& left, const TimedEvent& right) {
        return left.tick != right.tick ? left.tick < right.tick : left.order < right.order;
    });

    std::vector<uint8_t> body;
    uint32_t last = 0;
    for (const TimedEvent& event : timed) {
        PushVarLen(body, event.tick - last);
        last = event.tick;
        body.insert(body.end(), event.bytes.begin(), event.bytes.end());
    }
    PushVarLen(body, 0);
    body.push_back(0xFF);
    body.push_back(0x2F);
    body.push_back(0x00);

    std::vector<uint8_t> chunk;
    PushTag(chunk, "MTrk");
    PushBE(chunk, static_cast<uint32_t>(body.size()), 4);
    chunk.insert(chunk.end(), body.begin(), body.end());
    return chunk;
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> MusicFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    const uint8_t* data = segment.data;
    const size_t size = segment.size;

    if (size < 68) {
        SPDLOG_ERROR("Music: only {} bytes, too small for a sequence header", size);
        return std::nullopt;
    }

    auto music = std::make_shared<MusicData>(std::vector<uint8_t>(data, data + size));
    music->mVolume = VanillaVolume(node);

    uint32_t offsets[16];
    for (int i = 0; i < 16; i++) {
        offsets[i] = (static_cast<uint32_t>(data[i * 4]) << 24) | (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
                     (static_cast<uint32_t>(data[i * 4 + 2]) << 8) | data[i * 4 + 3];
    }
    music->mDivision = (static_cast<uint32_t>(data[64]) << 24) | (static_cast<uint32_t>(data[65]) << 16) |
                       (static_cast<uint32_t>(data[66]) << 8) | data[67];

    for (int i = 0; i < 16; i++) {
        MusicTrack track;
        track.index = static_cast<uint32_t>(i);
        track.present = offsets[i] != 0;
        if (track.present) {
            uint32_t end = static_cast<uint32_t>(size);
            for (int j = i + 1; j < 16; j++) {
                if (offsets[j] != 0) {
                    end = offsets[j];
                    break;
                }
            }
            if (end < offsets[i] || end > size) {
                SPDLOG_WARN("Music: track {} spans 0x{:X}..0x{:X} in a 0x{:X}-byte sequence", i, offsets[i], end, size);
                track.present = false;
            } else if (!DecodeTrack(data, size, offsets[i], end, track.events)) {
                SPDLOG_ERROR("Music: track {} does not decode", i);
                return std::nullopt;
            }
        }
        music->mTracks.push_back(std::move(track));
    }

    return music;
}

ExportResult MusicBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                         YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    auto music = std::static_pointer_cast<MusicData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKMusic, 0);
    writer.Write(music->mVolume);
    writer.Write(static_cast<uint32_t>(music->mBuffer.size()));
    writer.Write(reinterpret_cast<char*>(music->mBuffer.data()), music->mBuffer.size());
    writer.Finish(write);
    return std::nullopt;
}

ExportResult MusicModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName,
                                          YAML::Node& node, std::string* replacement) {
    auto music = std::static_pointer_cast<MusicData>(raw);
    *replacement += ".mid";

    std::vector<std::vector<uint8_t>> chunks;
    for (const MusicTrack& track : music->mTracks) {
        if (track.present) {
            const std::string lead = chunks.empty() ? "volume " + std::to_string(music->mVolume) : std::string();
            chunks.push_back(BuildSmfTrack(track, lead));
        }
    }

    std::vector<uint8_t> out;
    PushTag(out, "MThd");
    PushBE(out, 6, 4);
    PushBE(out, 1, 2);
    PushBE(out, static_cast<uint32_t>(chunks.size()), 2);
    PushBE(out, music->mDivision, 2);
    for (const std::vector<uint8_t>& chunk : chunks) {
        out.insert(out.end(), chunk.begin(), chunk.end());
    }

    write.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return std::nullopt;
}

namespace {

constexpr uint8_t kLoopForever = 0xFF;

struct SmfEvent {
    uint32_t tick = 0;
    int order = 0;
    int track = -1;
    uint8_t kind = 0;
    uint8_t status = 0;
    uint8_t byte1 = 0;
    uint8_t byte2 = 0;
    uint32_t aux = 0;
};

class SmfReader {
  public:
    SmfReader(const uint8_t* data, size_t size) : mData(data), mSize(size) {
    }

    bool Ok() const {
        return !mFailed;
    }
    size_t At() const {
        return mAt;
    }
    void Seek(size_t at) {
        mAt = at;
    }

    uint8_t U8() {
        if (mAt >= mSize) {
            mFailed = true;
            return 0;
        }
        return mData[mAt++];
    }
    uint16_t U16() {
        const uint16_t high = U8();
        return static_cast<uint16_t>((high << 8) | U8());
    }
    uint32_t U32() {
        const uint32_t high = U16();
        return (high << 16) | U16();
    }
    uint32_t VarLen() {
        uint32_t value = 0;
        for (int i = 0; i < 4; i++) {
            const uint8_t b = U8();
            value = (value << 7) | (b & 0x7F);
            if (!(b & 0x80)) {
                break;
            }
        }
        return value;
    }
    bool Tag(const char* tag) {
        if (mAt + 4 > mSize) {
            mFailed = true;
            return false;
        }
        const bool match = std::memcmp(mData + mAt, tag, 4) == 0;
        mAt += 4;
        return match;
    }

  private:
    const uint8_t* mData;
    size_t mSize;
    size_t mAt = 0;
    bool mFailed = false;
};

bool TextIs(const std::vector<uint8_t>& text, const char* want) {
    const size_t n = std::strlen(want);
    if (text.size() < n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (std::tolower(text[i]) != want[i]) {
            return false;
        }
    }
    return true;
}

bool ReadSmfTrack(SmfReader& reader, size_t end, std::vector<SmfEvent>& out, int& sequence, uint32_t endTicks[16],
                  std::optional<uint32_t>& volume) {
    uint32_t tick = 0;
    uint8_t running = 0;
    int chunkChannel = -1;
    bool touched[16] = { false };
    bool haveEndMarker = false;
    uint32_t endMarkerTick = 0;
    std::vector<SmfEvent> pending;

    while (reader.At() < end && reader.Ok()) {
        tick += reader.VarLen();
        uint8_t status = reader.U8();

        if (status == 0xFF) {
            const uint8_t meta = reader.U8();
            const uint32_t length = reader.VarLen();
            std::vector<uint8_t> text;
            for (uint32_t i = 0; i < length; i++) {
                text.push_back(reader.U8());
            }
            running = 0;
            if (meta == 0x2F) {
                const uint32_t stop = haveEndMarker ? endMarkerTick : tick;
                bool any = false;
                for (int c = 0; c < 16; c++) {
                    if (touched[c]) {
                        endTicks[c] = std::max(endTicks[c], stop);
                        any = true;
                    }
                }
                if (!any) {
                    endTicks[0] = std::max(endTicks[0], stop);
                }
                break;
            }
            SmfEvent event;
            event.tick = tick;
            event.order = sequence++;
            if (meta == 0x51 && text.size() >= 3) {
                event.kind = static_cast<uint8_t>(MusicEventKind::Tempo);
                event.aux = (static_cast<uint32_t>(text[0]) << 16) | (static_cast<uint32_t>(text[1]) << 8) | text[2];
                event.track = 0;
                pending.push_back(event);
            } else if ((meta == 0x06 || meta == 0x01) && TextIs(text, "loopstart")) {
                event.kind = static_cast<uint8_t>(MusicEventKind::LoopStart);
                pending.push_back(event);
            } else if ((meta == 0x06 || meta == 0x01) && TextIs(text, "volume")) {
                uint32_t value = 0;
                bool any = false;
                for (size_t i = 6; i < text.size(); i++) {
                    if (std::isdigit(text[i])) {
                        value = value * 10 + static_cast<uint32_t>(text[i] - '0');
                        any = true;
                    } else if (any) {
                        break;
                    }
                }
                if (any) {
                    volume = std::min<uint32_t>(value, kDefaultMusicVolume);
                }
            } else if ((meta == 0x06 || meta == 0x01) && TextIs(text, "track_end")) {
                haveEndMarker = true;
                endMarkerTick = tick;
            } else if ((meta == 0x06 || meta == 0x01) && TextIs(text, "loopend")) {
                event.kind = static_cast<uint8_t>(MusicEventKind::LoopEnd);
                event.byte1 = kLoopForever;
                event.byte2 = kLoopForever;
                pending.push_back(event);
            }
            continue;
        }

        if (status == 0xF0 || status == 0xF7) {
            const uint32_t length = reader.VarLen();
            for (uint32_t i = 0; i < length; i++) {
                reader.U8();
            }
            running = 0;
            continue;
        }

        uint8_t first;
        if (status & 0x80) {
            running = status;
            first = reader.U8();
        } else {
            if (running == 0) {
                return false;
            }
            first = status;
            status = running;
        }

        SmfEvent event;
        event.tick = tick;
        event.order = sequence++;
        event.kind = static_cast<uint8_t>(MusicEventKind::Midi);
        event.status = status;
        event.byte1 = first;
        event.track = status & 0x0F;
        const uint8_t kind = status & 0xF0;
        if (kind != 0xC0 && kind != 0xD0) {
            event.byte2 = reader.U8();
        }
        touched[event.track] = true;
        if (chunkChannel < 0) {
            chunkChannel = event.track;
        } else if (chunkChannel != event.track) {
            chunkChannel = 0x7F;
        }
        pending.push_back(event);
    }

    const int markerTrack = (chunkChannel >= 0 && chunkChannel < 16) ? chunkChannel : 0;
    for (SmfEvent& event : pending) {
        if (event.track < 0) {
            event.track = markerTrack;
        }
        out.push_back(event);
    }
    return reader.Ok();
}

void FoldNoteOffs(std::vector<SmfEvent>& events) {
    std::map<uint32_t, std::vector<size_t>> open;
    uint32_t lastTick = 0;
    for (const SmfEvent& event : events) {
        lastTick = std::max(lastTick, event.tick);
    }

    std::vector<bool> drop(events.size(), false);
    for (size_t i = 0; i < events.size(); i++) {
        SmfEvent& event = events[i];
        if (event.kind != static_cast<uint8_t>(MusicEventKind::Midi)) {
            continue;
        }
        const uint8_t kind = event.status & 0xF0;
        const uint32_t key = (static_cast<uint32_t>(event.track) << 8) | event.byte1;

        if (kind == 0x90 && event.byte2 != 0) {
            open[key].push_back(i);
        } else if (kind == 0x80 || (kind == 0x90 && event.byte2 == 0)) {
            auto it = open.find(key);
            if (it != open.end() && !it->second.empty()) {
                const size_t start = it->second.back();
                it->second.pop_back();
                events[start].aux = event.tick - events[start].tick;
            }
            drop[i] = true;
        }
    }
    for (auto& [key, indices] : open) {
        for (size_t start : indices) {
            events[start].aux = lastTick > events[start].tick ? lastTick - events[start].tick : 1;
        }
    }

    std::vector<SmfEvent> kept;
    kept.reserve(events.size());
    for (size_t i = 0; i < events.size(); i++) {
        if (!drop[i]) {
            kept.push_back(events[i]);
        }
    }
    events.swap(kept);
}

void PushByte(std::vector<uint8_t>& out, uint8_t byte) {
    out.push_back(byte);
    if (byte == kBlockCode) {
        out.push_back(kBlockCode);
    }
}

void PushSeqVarLen(std::vector<uint8_t>& out, uint32_t value) {
    uint8_t buffer[5];
    int count = 0;
    buffer[count++] = static_cast<uint8_t>(value & 0x7F);
    while ((value >>= 7) != 0) {
        buffer[count++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
    }
    while (count > 0) {
        PushByte(out, buffer[--count]);
    }
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> MusicFactory::parse_modding(std::vector<uint8_t>& buffer,
                                                                        YAML::Node& node) {
    SmfReader reader(buffer.data(), buffer.size());
    if (!reader.Tag("MThd")) {
        SPDLOG_ERROR("Music: not a MIDI file");
        return std::nullopt;
    }
    const uint32_t headerLength = reader.U32();
    const uint16_t format = reader.U16();
    const uint16_t trackCount = reader.U16();
    const uint16_t division = reader.U16();
    reader.Seek(8 + headerLength);

    if (format > 1) {
        SPDLOG_ERROR("Music: format {} is not supported; save as format 0 or 1", format);
        return std::nullopt;
    }
    if ((division & 0x8000) != 0) {
        SPDLOG_ERROR("Music: SMPTE timing is not supported; use ticks per quarter note");
        return std::nullopt;
    }

    std::vector<SmfEvent> events;
    uint32_t endTicks[16] = { 0 };
    std::optional<uint32_t> volume;
    int sequence = 0;
    for (uint16_t i = 0; i < trackCount && reader.Ok(); i++) {
        if (!reader.Tag("MTrk")) {
            SPDLOG_ERROR("Music: track {} is not an MTrk chunk", i);
            return std::nullopt;
        }
        const uint32_t size = reader.U32();
        const size_t end = reader.At() + size;
        if (!ReadSmfTrack(reader, end, events, sequence, endTicks, volume)) {
            SPDLOG_ERROR("Music: track {} does not parse", i);
            return std::nullopt;
        }
        reader.Seek(end);
    }
    if (!reader.Ok()) {
        SPDLOG_ERROR("Music: file ended early");
        return std::nullopt;
    }

    int muting = 0;
    int callbacks = 0;
    for (const SmfEvent& event : events) {
        if (event.kind != static_cast<uint8_t>(MusicEventKind::Midi) || (event.status & 0xF0) != 0xB0) {
            continue;
        }
        if (event.byte1 == 0x7E || event.byte1 == 0x7F) {
            muting++;
        } else if (event.byte1 >= 0x6A && event.byte1 <= 0x77) {
            callbacks++;
        }
    }
    if (muting != 0) {
        SPDLOG_WARN("Music: {} controller 126/127 event(s) will mute or unmute a channel, not set mono or poly mode",
                    muting);
    }
    if (callbacks != 0) {
        SPDLOG_WARN("Music: {} controller event(s) in 106-119 signal the game rather than the synth", callbacks);
    }

    FoldNoteOffs(events);
    std::stable_sort(events.begin(), events.end(), [](const SmfEvent& a, const SmfEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });

    std::vector<uint8_t> out(68, 0);
    uint32_t offsets[16] = { 0 };
    size_t written = 0;

    for (int track = 0; track < 16; track++) {
        std::vector<const SmfEvent*> mine;
        for (const SmfEvent& event : events) {
            if (event.track == track) {
                mine.push_back(&event);
            }
        }
        if (mine.empty()) {
            continue;
        }
        offsets[track] = static_cast<uint32_t>(out.size());
        written++;

        uint32_t last = 0;
        std::vector<size_t> loopTargets;
        for (const SmfEvent* event : mine) {
            PushSeqVarLen(out, event->tick - last);
            last = event->tick;

            switch (static_cast<MusicEventKind>(event->kind)) {
                case MusicEventKind::Midi: {
                    PushByte(out, event->status);
                    PushByte(out, event->byte1);
                    const uint8_t kind = event->status & 0xF0;
                    if (kind != 0xC0 && kind != 0xD0) {
                        PushByte(out, event->byte2);
                        if (kind == 0x90) {
                            PushSeqVarLen(out, event->aux);
                        }
                    }
                    break;
                }
                case MusicEventKind::Tempo:
                    PushByte(out, kMetaPrefix);
                    PushByte(out, kSetTempo);
                    PushByte(out, static_cast<uint8_t>(event->aux >> 16));
                    PushByte(out, static_cast<uint8_t>(event->aux >> 8));
                    PushByte(out, static_cast<uint8_t>(event->aux));
                    break;
                case MusicEventKind::LoopStart:
                    PushByte(out, kMetaPrefix);
                    PushByte(out, kLoopStart);
                    PushByte(out, 0);
                    PushByte(out, 0);
                    loopTargets.push_back(out.size());
                    break;
                case MusicEventKind::LoopEnd: {
                    PushByte(out, kMetaPrefix);
                    PushByte(out, kLoopEnd);
                    const size_t target = loopTargets.empty() ? offsets[track] : loopTargets.back();
                    if (!loopTargets.empty()) {
                        loopTargets.pop_back();
                    }
                    out.push_back(event->byte1);
                    out.push_back(event->byte2);
                    const uint32_t back = static_cast<uint32_t>(out.size() + 4 - target);
                    out.push_back(static_cast<uint8_t>(back >> 24));
                    out.push_back(static_cast<uint8_t>(back >> 16));
                    out.push_back(static_cast<uint8_t>(back >> 8));
                    out.push_back(static_cast<uint8_t>(back));
                    break;
                }
                default:
                    break;
            }
        }
        PushSeqVarLen(out, endTicks[track] > last ? endTicks[track] - last : 0);
        PushByte(out, kMetaPrefix);
        PushByte(out, kEndOfTrack);
    }

    for (int i = 0; i < 16; i++) {
        out[i * 4 + 0] = static_cast<uint8_t>(offsets[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(offsets[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(offsets[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(offsets[i]);
    }
    out[64] = static_cast<uint8_t>(division >> 24);
    out[65] = static_cast<uint8_t>(division >> 16);
    out[66] = static_cast<uint8_t>(division >> 8);
    out[67] = static_cast<uint8_t>(division);

    auto music = std::make_shared<MusicData>(std::move(out));
    music->mDivision = division;
    music->mVolume = volume.value_or(VanillaVolume(node));
    if (volume.has_value() && *volume != VanillaVolume(node)) {
        SPDLOG_INFO("Music: slot volume set to {} of {}", *volume, kDefaultMusicVolume);
    }
    SPDLOG_INFO("Music: built a sequence from MIDI -- {} track(s), division {}, {} bytes", written, division,
                music->mBuffer.size());
    return music;
}

} // namespace BK64

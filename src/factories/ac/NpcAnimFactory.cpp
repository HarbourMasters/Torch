#include "NpcAnimFactory.h"
#include "ACUtils.h"

#include <algorithm>

namespace AC {

static NpcSeData ReadSeData(uint32_t addr, const std::string& what) {
    NpcSeData out;
    if (addr == 0) {
        return out;
    }
    Cursor c(ReadBytes(addr, 8, what), what);
    out.present = true;
    out.numCheckFrames = c.S32();
    const uint32_t table = c.U32();
    if (out.numCheckFrames < 0 || out.numCheckFrames > 0x1000) {
        throw std::runtime_error(what + ": implausible num_check_frames " + std::to_string(out.numCheckFrames));
    }
    Cursor t(ReadBytes(table, out.numCheckFrames * 4, what + " check_frame_tbl"), what);
    for (int32_t i = 0; i < out.numCheckFrames; i++) {
        out.frames.push_back(t.S32());
    }
    return out;
}

static void WriteSeData(LUS::BinaryWriter& writer, const NpcSeData& se) {
    writer.Write((uint8_t)(se.present ? 1 : 0));
    if (!se.present) {
        return;
    }
    writer.Write(se.numCheckFrames);
    writer.Write((uint32_t)se.frames.size());
    for (const auto f : se.frames) {
        writer.Write(f);
    }
}

ExportResult NpcAnimBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                           std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto npc = std::static_pointer_cast<NpcAnimData>(raw);
    auto writer = LUS::BinaryWriter();
    WriteHeader(writer, Torch::ResourceType::ACNpcAnimation, 0);
    WriteKeyFrameAnimBody(writer, npc->mAnim);
    writer.Write(npc->mStartFrame);
    writer.Write(npc->mEndFrame);
    writer.Write(npc->mMode);
    writer.Write(npc->mMorphCounter);
    WriteBytes(writer, npc->mEyeSeq);
    writer.Write(npc->mEyeSeqType);
    writer.Write(npc->mEyeSeqStopFrame);
    WriteBytes(writer, npc->mMouthSeq);
    writer.Write(npc->mMouthSeqType);
    writer.Write(npc->mMouthSeqStopFrame);
    writer.Write(npc->mFeelEffectSetFrame);
    writer.Write(npc->mFeelEffectType);
    writer.Write((uint8_t)(npc->mHasFeel ? 1 : 0));
    if (npc->mHasFeel) {
        writer.Write(npc->mFeelType);
        writer.Write(npc->mFeelMax);
        writer.Write(npc->mFeelSetNum);
        writer.Write((uint32_t)npc->mFeelSet.size());
        for (const auto v : npc->mFeelSet) {
            writer.Write(v);
        }
    }
    writer.Write((uint8_t)(npc->mHasSe ? 1 : 0));
    if (npc->mHasSe) {
        WriteSeData(writer, npc->mLFoot);
        WriteSeData(writer, npc->mRFoot);
        writer.Write(npc->mOtherSeType);
        writer.Write((uint32_t)npc->mOtherSe.size());
        for (const auto& other : npc->mOtherSe) {
            WriteSeData(writer, other.se);
            writer.Write(other.seNo);
        }
    }
    writer.Finish(write);
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> NpcAnimFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto name = NameOf(node);
    auto npc = std::make_shared<NpcAnimData>();
    npc->mAnim = ReadKeyFrameAnim(offset, node, name);

    Cursor c(ReadBytes(offset + 0x14, 0x2C, name), name);
    npc->mStartFrame = c.F32();
    npc->mEndFrame = c.F32();
    npc->mMode = c.S32();
    npc->mMorphCounter = c.F32();
    const uint32_t eyePtr = c.U32();
    npc->mEyeSeqType = c.S16();
    npc->mEyeSeqStopFrame = c.S16();
    const uint32_t mouthPtr = c.U32();
    npc->mMouthSeqType = c.S16();
    npc->mMouthSeqStopFrame = c.S16();
    npc->mFeelEffectSetFrame = c.S16();
    npc->mFeelEffectType = c.S16();
    const uint32_t feelPtr = c.U32();
    const uint32_t sePtr = c.U32();

    const uint32_t frames = std::max<int32_t>(npc->mAnim.mFrames, 0);
    auto sequence = [&](uint32_t ptr, const std::string& key) {
        const auto declared = CountHint(node, key);
        if (declared.has_value() && declared.value() < frames) {
            throw std::runtime_error(name + ": " + key + " has " + std::to_string(declared.value()) +
                                     " entries for " + std::to_string(frames) + " frames");
        }
        return ReadBytes(ptr, std::max(frames, declared.value_or(0)), name + " " + key);
    };
    if (eyePtr != 0) {
        npc->mEyeSeq = sequence(eyePtr, "eye_seq");
    }
    if (mouthPtr != 0) {
        npc->mMouthSeq = sequence(mouthPtr, "mouth_seq");
    }
    if (feelPtr != 0) {
        Cursor f(ReadBytes(feelPtr, 8, name + " feel_effect"), name);
        npc->mHasFeel = true;
        npc->mFeelType = f.S16();
        npc->mFeelMax = f.U8();
        npc->mFeelSetNum = f.U8();
        const uint32_t setPtr = f.U32();
        Cursor s(ReadBytes(setPtr, npc->mFeelSetNum * 4, name + " feel set_p"), name);
        for (uint32_t i = 0; i < npc->mFeelSetNum; i++) {
            npc->mFeelSet.push_back(s.U32());
        }
    }
    if (sePtr != 0) {
        Cursor t(ReadBytes(sePtr, 16, name + " se_data_table"), name);
        npc->mHasSe = true;
        const uint32_t lfoot = t.U32();
        const uint32_t rfoot = t.U32();
        npc->mOtherSeType = t.S32();
        const uint32_t otherPtr = t.U32();
        npc->mLFoot = ReadSeData(lfoot, name + " lfoot_data");
        npc->mRFoot = ReadSeData(rfoot, name + " rfoot_data");
        if (npc->mOtherSeType < 0 || npc->mOtherSeType > 3) {
            throw std::runtime_error(name + ": other_se_type " + std::to_string(npc->mOtherSeType) + " out of range");
        }
        if (npc->mOtherSeType > 0) {
            Cursor o(ReadBytes(otherPtr, npc->mOtherSeType * 12, name + " other_se_data"), name);
            for (int32_t i = 0; i < npc->mOtherSeType; i++) {
                const int32_t num = o.S32();
                const uint32_t table = o.U32();
                NpcOtherSeData other;
                other.seNo = o.U16();
                o.Skip(2);
                other.se.present = true;
                other.se.numCheckFrames = num;
                if (num < 0 || num > 0x1000) {
                    throw std::runtime_error(name + ": implausible other_se num_check_frames");
                }
                Cursor ft(ReadBytes(table, num * 4, name + " other_se check_frame_tbl"), name);
                for (int32_t k = 0; k < num; k++) {
                    other.se.frames.push_back(ft.S32());
                }
                npc->mOtherSe.push_back(other);
            }
        }
    }
    return npc;
}

} // namespace AC

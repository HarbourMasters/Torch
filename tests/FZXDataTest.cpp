#include <gtest/gtest.h>
#include "factories/fzerox/EADAnimationFactory.h"
#include "factories/fzerox/EADLimbFactory.h"
#include "factories/fzerox/SoundFontFactory.h"
#include "factories/fzerox/SequenceFactory.h"
#include "lib/binarytools/BinaryReader.h"
#include "lib/binarytools/endianness.h"
#include <vector>

// EADAnimationData
TEST(FZXDataTest, EADAnimationDataConstruction) {
    FZX::EADAnimationData anim(30, 10, 0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000);
    EXPECT_EQ(anim.mFrameCount, 30);
    EXPECT_EQ(anim.mLimbCount, 10);
    EXPECT_EQ(anim.mScaleData, 0x1000u);
    EXPECT_EQ(anim.mScaleInfo, 0x2000u);
    EXPECT_EQ(anim.mRotationData, 0x3000u);
    EXPECT_EQ(anim.mRotationInfo, 0x4000u);
    EXPECT_EQ(anim.mPositionData, 0x5000u);
    EXPECT_EQ(anim.mPositionInfo, 0x6000u);
}

TEST(FZXDataTest, EADAnimationFactoryExporters) {
    FZX::EADAnimationFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_FALSE(factory.GetExporter(ExportType::Modding).has_value());
}

// EADLimbData
TEST(FZXDataTest, EADLimbDataConstruction) {
    FZX::EADLimbData limb(
        0x06001000,
        Vec3f(1.0f, 2.0f, 3.0f),
        Vec3f(10.0f, 20.0f, 30.0f),
        Vec3s(100, 200, 300),
        0x06002000, 0x06003000, 0x06004000, 0x06005000, 5);

    EXPECT_EQ(limb.mDl, 0x06001000u);
    EXPECT_FLOAT_EQ(limb.mScale.x, 1.0f);
    EXPECT_FLOAT_EQ(limb.mScale.z, 3.0f);
    EXPECT_FLOAT_EQ(limb.mPos.y, 20.0f);
    EXPECT_EQ(limb.mRot.x, 100);
    EXPECT_EQ(limb.mRot.z, 300);
    EXPECT_EQ(limb.mNextLimb, 0x06002000u);
    EXPECT_EQ(limb.mChildLimb, 0x06003000u);
    EXPECT_EQ(limb.mAssociatedLimb, 0x06004000u);
    EXPECT_EQ(limb.mAssociatedLimbDL, 0x06005000u);
    EXPECT_EQ(limb.mLimbId, 5);
}

TEST(FZXDataTest, EADLimbFactoryExporters) {
    FZX::EADLimbFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_FALSE(factory.GetExporter(ExportType::Modding).has_value());
}

// SoundFont types
TEST(FZXDataTest, TunedSampleConstruction) {
    FZX::TunedSample ts = {"sample_ref", 1.5f};
    EXPECT_EQ(ts.sampleRef, "sample_ref");
    EXPECT_FLOAT_EQ(ts.tuning, 1.5f);
}

TEST(FZXDataTest, DrumConstruction) {
    FZX::Drum drum = {10, 64, 0, {"sample", 1.0f}, "env_ref"};
    EXPECT_EQ(drum.adsrDecayIndex, 10);
    EXPECT_EQ(drum.pan, 64);
    EXPECT_EQ(drum.isRelocated, 0);
    EXPECT_EQ(drum.tunedSample.sampleRef, "sample");
    EXPECT_EQ(drum.envelopeRef, "env_ref");
}

TEST(FZXDataTest, InstrumentConstruction) {
    FZX::Instrument inst = {
        0, 20, 100, 5, "env",
        {"low_sample", 0.5f},
        {"normal_sample", 1.0f},
        {"high_sample", 2.0f}
    };
    EXPECT_EQ(inst.normalRangeLo, 20);
    EXPECT_EQ(inst.normalRangeHi, 100);
    EXPECT_EQ(inst.adsrDecayIndex, 5);
    EXPECT_FLOAT_EQ(inst.lowPitchTunedSample.tuning, 0.5f);
    EXPECT_FLOAT_EQ(inst.normalPitchTunedSample.tuning, 1.0f);
    EXPECT_FLOAT_EQ(inst.highPitchTunedSample.tuning, 2.0f);
}

TEST(FZXDataTest, AdpcmBookConstruction) {
    FZX::AdpcmBook book = {2, 4, {1, 2, 3, 4, 5, 6, 7, 8}};
    EXPECT_EQ(book.order, 2);
    EXPECT_EQ(book.numPredictors, 4);
    ASSERT_EQ(book.book.size(), 8u);
    EXPECT_EQ(book.book[0], 1);
}

TEST(FZXDataTest, AdpcmLoopConstruction) {
    FZX::AdpcmLoop loop = {100, 5000, 3, {10, 20, 30}};
    EXPECT_EQ(loop.start, 100u);
    EXPECT_EQ(loop.end, 5000u);
    EXPECT_EQ(loop.count, 3u);
    ASSERT_EQ(loop.predictorState.size(), 3u);
}

TEST(FZXDataTest, SoundFontDataConstruction) {
    FZX::SoundFontEntry entry = {
        "drum_0", FZX::DataType::Drum,
        FZX::Drum{0, 64, 0, {"sample", 1.0f}, "env"}
    };
    std::vector<FZX::SoundFontEntry> entries = {entry};
    FZX::SoundFontData data(entries, true);
    ASSERT_EQ(data.mEntries.size(), 1u);
    EXPECT_EQ(data.mEntries[0].name, "drum_0");
    EXPECT_EQ(data.mEntries[0].type, FZX::DataType::Drum);
    EXPECT_TRUE(data.mSupportSfx);
}

TEST(FZXDataTest, SoundFontFactoryExporters) {
    FZX::SoundFontFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_FALSE(factory.GetExporter(ExportType::Modding).has_value());
}

// Sequence types
TEST(FZXDataTest, SeqCommandComparison) {
    FZX::SeqCommand cmd1 = {0x90, 0, FZX::SequenceState::player, 0, 0, 1, {}};
    FZX::SeqCommand cmd2 = {0x91, 10, FZX::SequenceState::channel, 1, 0, 1, {}};
    EXPECT_TRUE(cmd1 < cmd2);
    EXPECT_FALSE(cmd2 < cmd1);
}

TEST(FZXDataTest, SeqLabelInfoConstruction) {
    FZX::SeqLabelInfo label(0x100, FZX::SequenceState::layer, 2, 1, true);
    EXPECT_EQ(label.pos, 0x100u);
    EXPECT_EQ(label.state, FZX::SequenceState::layer);
    EXPECT_EQ(label.channel, 2u);
    EXPECT_EQ(label.layer, 1u);
    EXPECT_TRUE(label.largeNotes);
}

TEST(FZXDataTest, SequenceDataConstruction) {
    FZX::SeqCommand cmd = {0xFE, 0, FZX::SequenceState::player, -1, -1, 1, {}};
    std::vector<FZX::SeqCommand> cmds = {cmd};
    std::unordered_set<uint32_t> labels = {0, 100, 200};
    FZX::SequenceData data(cmds, labels, true);
    ASSERT_EQ(data.mCmds.size(), 1u);
    EXPECT_EQ(data.mCmds[0].cmd, 0xFE);
    EXPECT_EQ(data.mLabels.size(), 3u);
    EXPECT_TRUE(data.mHasFooter);
}

TEST(FZXDataTest, SequenceFactoryExporters) {
    FZX::SequenceFactory factory;
    EXPECT_TRUE(factory.GetExporter(ExportType::Code).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Header).has_value());
    EXPECT_TRUE(factory.GetExporter(ExportType::Binary).has_value());
    EXPECT_FALSE(factory.GetExporter(ExportType::Modding).has_value());
}

// BinaryReader parse test — EADAnimation header format
TEST(FZXDataTest, ManualEADAnimationHeaderParse) {
    // 28 bytes: int16_t frameCount + int16_t limbCount + 6×uint32_t pointers
    std::vector<uint8_t> buf = {
        0x00, 0x1E,              // frameCount = 30
        0x00, 0x0A,              // limbCount = 10
        0x00, 0x00, 0x10, 0x00,  // scaleData = 0x1000
        0x00, 0x00, 0x20, 0x00,  // scaleInfo = 0x2000
        0x00, 0x00, 0x30, 0x00,  // rotationData = 0x3000
        0x00, 0x00, 0x40, 0x00,  // rotationInfo = 0x4000
        0x00, 0x00, 0x50, 0x00,  // positionData = 0x5000
        0x00, 0x00, 0x60, 0x00,  // positionInfo = 0x6000
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    auto frameCount = reader.ReadInt16();
    auto limbCount = reader.ReadInt16();
    auto scaleData = reader.ReadUInt32();
    auto scaleInfo = reader.ReadUInt32();
    auto rotationData = reader.ReadUInt32();
    auto rotationInfo = reader.ReadUInt32();
    auto positionData = reader.ReadUInt32();
    auto positionInfo = reader.ReadUInt32();

    FZX::EADAnimationData anim(frameCount, limbCount, scaleData, scaleInfo,
                                rotationData, rotationInfo, positionData, positionInfo);

    EXPECT_EQ(anim.mFrameCount, 30);
    EXPECT_EQ(anim.mLimbCount, 10);
    EXPECT_EQ(anim.mScaleData, 0x1000u);
    EXPECT_EQ(anim.mScaleInfo, 0x2000u);
    EXPECT_EQ(anim.mRotationData, 0x3000u);
    EXPECT_EQ(anim.mRotationInfo, 0x4000u);
    EXPECT_EQ(anim.mPositionData, 0x5000u);
    EXPECT_EQ(anim.mPositionInfo, 0x6000u);
}

// BinaryReader parse test — EADLimb format
TEST(FZXDataTest, ManualEADLimbParse) {
    // 52 bytes: u32 dl + 3×float scale + 3×float pos + 3×int16 rot + i16 pad
    //           + u32 next + u32 child + u32 assocLimb + u32 assocDL + i16 limbId
    std::vector<uint8_t> buf = {
        0x06, 0x00, 0x10, 0x00,              // dl = 0x06001000
        0x3F, 0x80, 0x00, 0x00,              // scale.x = 1.0f
        0x40, 0x00, 0x00, 0x00,              // scale.y = 2.0f
        0x40, 0x40, 0x00, 0x00,              // scale.z = 3.0f
        0x41, 0x20, 0x00, 0x00,              // pos.x = 10.0f
        0x41, 0xA0, 0x00, 0x00,              // pos.y = 20.0f
        0x41, 0xF0, 0x00, 0x00,              // pos.z = 30.0f
        0x00, 0x64,                          // rot.x = 100
        0x00, 0xC8,                          // rot.y = 200
        0x01, 0x2C,                          // rot.z = 300
        0x00, 0x00,                          // pad
        0x06, 0x00, 0x20, 0x00,              // nextLimb = 0x06002000
        0x06, 0x00, 0x30, 0x00,              // childLimb = 0x06003000
        0x06, 0x00, 0x40, 0x00,              // associatedLimb = 0x06004000
        0x06, 0x00, 0x50, 0x00,              // associatedLimbDL = 0x06005000
        0x00, 0x05,                          // limbId = 5
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    auto dl = reader.ReadUInt32();
    auto sx = reader.ReadFloat(); auto sy = reader.ReadFloat(); auto sz = reader.ReadFloat();
    Vec3f scale(sx, sy, sz);
    auto px = reader.ReadFloat(); auto py = reader.ReadFloat(); auto pz = reader.ReadFloat();
    Vec3f pos(px, py, pz);
    auto rx = reader.ReadInt16(); auto ry = reader.ReadInt16(); auto rz = reader.ReadInt16();
    Vec3s rot(rx, ry, rz);
    reader.ReadInt16(); // pad
    auto nextLimb = reader.ReadUInt32();
    auto childLimb = reader.ReadUInt32();
    auto assocLimb = reader.ReadUInt32();
    auto assocDL = reader.ReadUInt32();
    auto limbId = reader.ReadInt16();

    FZX::EADLimbData limb(dl, scale, pos, rot, nextLimb, childLimb, assocLimb, assocDL, limbId);

    EXPECT_EQ(limb.mDl, 0x06001000u);
    EXPECT_FLOAT_EQ(limb.mScale.x, 1.0f);
    EXPECT_FLOAT_EQ(limb.mScale.y, 2.0f);
    EXPECT_FLOAT_EQ(limb.mScale.z, 3.0f);
    EXPECT_FLOAT_EQ(limb.mPos.x, 10.0f);
    EXPECT_FLOAT_EQ(limb.mPos.y, 20.0f);
    EXPECT_FLOAT_EQ(limb.mPos.z, 30.0f);
    EXPECT_EQ(limb.mRot.x, 100);
    EXPECT_EQ(limb.mRot.y, 200);
    EXPECT_EQ(limb.mRot.z, 300);
    EXPECT_EQ(limb.mNextLimb, 0x06002000u);
    EXPECT_EQ(limb.mChildLimb, 0x06003000u);
    EXPECT_EQ(limb.mAssociatedLimb, 0x06004000u);
    EXPECT_EQ(limb.mAssociatedLimbDL, 0x06005000u);
    EXPECT_EQ(limb.mLimbId, 5);
}

// DataType enum coverage
TEST(FZXDataTest, DataTypeValues) {
    EXPECT_NE(FZX::DataType::Drum, FZX::DataType::Sfx);
    EXPECT_NE(FZX::DataType::Instrument, FZX::DataType::Envelope);
    EXPECT_NE(FZX::DataType::Sample, FZX::DataType::Book);
    EXPECT_NE(FZX::DataType::Loop, FZX::DataType::DrumOffsets);
}

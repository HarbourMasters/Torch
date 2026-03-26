#include <gtest/gtest.h>
#include "factories/fzerox/EADAnimationFactory.h"
#include "factories/fzerox/EADLimbFactory.h"
#include "factories/fzerox/SoundFontFactory.h"
#include "factories/fzerox/SequenceFactory.h"
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

// DataType enum coverage
TEST(FZXDataTest, DataTypeValues) {
    EXPECT_NE(FZX::DataType::Drum, FZX::DataType::Sfx);
    EXPECT_NE(FZX::DataType::Instrument, FZX::DataType::Envelope);
    EXPECT_NE(FZX::DataType::Sample, FZX::DataType::Book);
    EXPECT_NE(FZX::DataType::Loop, FZX::DataType::DrumOffsets);
}

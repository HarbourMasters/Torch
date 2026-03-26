#include <gtest/gtest.h>
#include "factories/MtxFactory.h"
#include "lib/binarytools/BinaryReader.h"
#include "lib/binarytools/endianness.h"
#include <vector>

// Replicate the FIXTOF macro locally for testing
#define TEST_FIXTOF(x) ((float)((x) / 65536.0f))

TEST(MtxFactoryTest, MtxSSize) {
    // MtxS: 16 uint16_t intParts + 16 uint16_t fracParts = 64 bytes
    EXPECT_EQ(sizeof(MtxS), 64u);
}

TEST(MtxFactoryTest, MtxDataConstruction) {
    MtxRaw raw = {};
    // Set identity matrix diagonal
    raw.mtx[0] = 1.0f; raw.mtx[5] = 1.0f; raw.mtx[10] = 1.0f; raw.mtx[15] = 1.0f;
    std::vector<MtxRaw> mtxs = {raw};
    MtxData data(mtxs);
    ASSERT_EQ(data.mMtxs.size(), 1u);
    EXPECT_FLOAT_EQ(data.mMtxs[0].mtx[0], 1.0f);
    EXPECT_FLOAT_EQ(data.mMtxs[0].mtx[5], 1.0f);
    EXPECT_FLOAT_EQ(data.mMtxs[0].mtx[10], 1.0f);
    EXPECT_FLOAT_EQ(data.mMtxs[0].mtx[15], 1.0f);
    EXPECT_FLOAT_EQ(data.mMtxs[0].mtx[1], 0.0f);
}

TEST(MtxFactoryTest, HasExpectedExporters) {
    MtxFactory factory;
    auto exporters = factory.GetExporters();
    EXPECT_TRUE(exporters.count(ExportType::Code));
    EXPECT_TRUE(exporters.count(ExportType::Header));
    EXPECT_TRUE(exporters.count(ExportType::Binary));
}

TEST(MtxFactoryTest, FixedPointConversionIdentity) {
    // 1.0 in N64 fixed-point: int=0x0001, frac=0x0000
    // (0x0001 << 16) | 0x0000 = 0x00010000 = 65536
    // FIXTOF(65536) = 65536 / 65536.0 = 1.0
    uint16_t int_part = 0x0001;
    uint16_t frac_part = 0x0000;
    float result = TEST_FIXTOF((int32_t)((int_part << 16) | frac_part));
    EXPECT_FLOAT_EQ(result, 1.0f);
}

TEST(MtxFactoryTest, FixedPointConversionZero) {
    uint16_t int_part = 0x0000;
    uint16_t frac_part = 0x0000;
    float result = TEST_FIXTOF((int32_t)((int_part << 16) | frac_part));
    EXPECT_FLOAT_EQ(result, 0.0f);
}

TEST(MtxFactoryTest, FixedPointConversionHalf) {
    // 0.5 in fixed-point: int=0x0000, frac=0x8000
    // (0x0000 << 16) | 0x8000 = 0x00008000 = 32768
    // FIXTOF(32768) = 32768 / 65536.0 = 0.5
    uint16_t int_part = 0x0000;
    uint16_t frac_part = 0x8000;
    float result = TEST_FIXTOF((int32_t)((int_part << 16) | frac_part));
    EXPECT_FLOAT_EQ(result, 0.5f);
}

TEST(MtxFactoryTest, FixedPointConversionNegative) {
    // -1.0 in fixed-point: int=0xFFFF, frac=0x0000
    // (0xFFFF << 16) | 0x0000 = 0xFFFF0000 interpreted as int32_t = -65536
    // FIXTOF(-65536) = -65536 / 65536.0 = -1.0
    uint16_t int_part = 0xFFFF;
    uint16_t frac_part = 0x0000;
    float result = TEST_FIXTOF((int32_t)((int_part << 16) | frac_part));
    EXPECT_FLOAT_EQ(result, -1.0f);
}

TEST(MtxFactoryTest, ManualMatrixParse) {
    // Build a 64-byte big-endian buffer for a 4x4 identity matrix in N64 fixed-point:
    // First 32 bytes: 16 uint16_t integer parts (row-major)
    // Next 32 bytes: 16 uint16_t fractional parts
    // Identity: diagonal int=1, frac=0; off-diagonal int=0, frac=0
    std::vector<uint8_t> buf = {
        // Integer parts (16 uint16_t, big-endian)
        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // row 0: 1,0,0,0
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,  // row 1: 0,1,0,0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,  // row 2: 0,0,1,0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,  // row 3: 0,0,0,1
        // Fractional parts (16 uint16_t, all zero)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    LUS::BinaryReader reader(buf.data(), buf.size());
    reader.SetEndianness(Torch::Endianness::Big);

    // Read 16 int parts then 16 frac parts, convert to float
    uint16_t intParts[16];
    uint16_t fracParts[16];
    for (int i = 0; i < 16; i++) {
        intParts[i] = reader.ReadUInt16();
    }
    for (int i = 0; i < 16; i++) {
        fracParts[i] = reader.ReadUInt16();
    }

    float mtx[16];
    for (int i = 0; i < 16; i++) {
        mtx[i] = TEST_FIXTOF((int32_t)((intParts[i] << 16) | fracParts[i]));
    }

    // Verify identity matrix
    for (int i = 0; i < 16; i++) {
        float expected = (i % 5 == 0) ? 1.0f : 0.0f;  // diagonal elements
        EXPECT_FLOAT_EQ(mtx[i], expected) << "element " << i;
    }
}

#undef TEST_FIXTOF

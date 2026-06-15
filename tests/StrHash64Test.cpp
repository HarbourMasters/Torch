#include <gtest/gtest.h>
#include "strhash64/StrHash64.h"
#include <cstring>

TEST(StrHash64Test, CRC64EmptyString) {
    // CRC64("") with no bytes processed returns INITIAL_CRC64
    EXPECT_EQ(CRC64(""), INITIAL_CRC64);
}

TEST(StrHash64Test, CRC64Deterministic) {
    uint64_t hash = CRC64("hello");
    EXPECT_EQ(CRC64("hello"), hash);
    EXPECT_NE(hash, 0u);
    EXPECT_NE(hash, INITIAL_CRC64);
}

TEST(StrHash64Test, CRC64CaseSensitive) {
    EXPECT_NE(CRC64("hello"), CRC64("Hello"));
}

TEST(StrHash64Test, CRC64DifferentStrings) {
    EXPECT_NE(CRC64("abc"), CRC64("def"));
    EXPECT_NE(CRC64("test"), CRC64("tset"));
}

TEST(StrHash64Test, CRC64SingleChar) {
    uint64_t a = CRC64("a");
    uint64_t b = CRC64("b");
    EXPECT_NE(a, b);
    EXPECT_NE(a, 0u);
}

// CRC64 does NOT apply final ~crc, but crc64/update_crc64 DO.
// They are intentionally different functions.

TEST(StrHash64Test, Crc64BufferDeterministic) {
    const char* str = "hello";
    uint64_t hash = crc64(str, strlen(str));
    EXPECT_EQ(crc64(str, strlen(str)), hash);
    EXPECT_NE(hash, 0u);
}

TEST(StrHash64Test, Crc64DifferentFromCRC64) {
    // crc64() applies ~crc at the end, CRC64() does not
    const char* str = "hello";
    uint64_t hash1 = CRC64(str);
    uint64_t hash2 = crc64(str, strlen(str));
    // They should be bitwise complements of each other
    EXPECT_EQ(hash1, ~hash2);
}

TEST(StrHash64Test, Crc64EmptyBuffer) {
    // crc64 with len=0 still applies ~INITIAL_CRC64 = 0
    uint64_t hash = crc64("", 0);
    EXPECT_EQ(hash, ~INITIAL_CRC64);
    EXPECT_EQ(hash, 0u);
}

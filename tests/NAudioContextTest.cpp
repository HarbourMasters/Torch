#include <gtest/gtest.h>
#include "factories/naudio/v1/AudioContext.h"
#include <cstring>

// GetMediumStr tests
TEST(NAudioContextTest, MediumRam) { EXPECT_STREQ(AudioContext::GetMediumStr(0), "Ram"); }
TEST(NAudioContextTest, MediumUnk) { EXPECT_STREQ(AudioContext::GetMediumStr(1), "Unk"); }
TEST(NAudioContextTest, MediumCart) { EXPECT_STREQ(AudioContext::GetMediumStr(2), "Cart"); }
TEST(NAudioContextTest, MediumDisk) { EXPECT_STREQ(AudioContext::GetMediumStr(3), "Disk"); }
TEST(NAudioContextTest, MediumGap) { EXPECT_STREQ(AudioContext::GetMediumStr(4), "ERROR"); }
TEST(NAudioContextTest, MediumRamUnloaded) { EXPECT_STREQ(AudioContext::GetMediumStr(5), "RamUnloaded"); }
TEST(NAudioContextTest, MediumOutOfRange) { EXPECT_STREQ(AudioContext::GetMediumStr(255), "ERROR"); }

// GetCachePolicyStr tests
TEST(NAudioContextTest, CacheTemporary) { EXPECT_STREQ(AudioContext::GetCachePolicyStr(0), "Temporary"); }
TEST(NAudioContextTest, CachePersistent) { EXPECT_STREQ(AudioContext::GetCachePolicyStr(1), "Persistent"); }
TEST(NAudioContextTest, CacheEither) { EXPECT_STREQ(AudioContext::GetCachePolicyStr(2), "Either"); }
TEST(NAudioContextTest, CachePermanent) { EXPECT_STREQ(AudioContext::GetCachePolicyStr(3), "Permanent"); }
TEST(NAudioContextTest, CacheError) { EXPECT_STREQ(AudioContext::GetCachePolicyStr(4), "ERROR"); }

// GetCodecStr tests
TEST(NAudioContextTest, CodecADPCM) { EXPECT_STREQ(AudioContext::GetCodecStr(0), "ADPCM"); }
TEST(NAudioContextTest, CodecS8) { EXPECT_STREQ(AudioContext::GetCodecStr(1), "S8"); }
TEST(NAudioContextTest, CodecS16MEM) { EXPECT_STREQ(AudioContext::GetCodecStr(2), "S16MEM"); }
TEST(NAudioContextTest, CodecADPCMSMALL) { EXPECT_STREQ(AudioContext::GetCodecStr(3), "ADPCMSMALL"); }
TEST(NAudioContextTest, CodecREVERB) { EXPECT_STREQ(AudioContext::GetCodecStr(4), "REVERB"); }
TEST(NAudioContextTest, CodecS16) { EXPECT_STREQ(AudioContext::GetCodecStr(5), "S16"); }
TEST(NAudioContextTest, CodecUNK6) { EXPECT_STREQ(AudioContext::GetCodecStr(6), "UNK6"); }
TEST(NAudioContextTest, CodecUNK7) { EXPECT_STREQ(AudioContext::GetCodecStr(7), "UNK7"); }
TEST(NAudioContextTest, CodecError) { EXPECT_STREQ(AudioContext::GetCodecStr(8), "ERROR"); }

#pragma once

#include <cstdint>
#include <vector>

// Type definitions to match original code
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;

namespace RareZip {

// Constants
#define COMP_HEADER_SIZE 6
#define WSIZE 0x8000
#define BMAX 16
#define N_MAX 288

// These macros are used in the decompression algorithm
#define NEXTBYTE() (compressionInputBuffer[inputBuffer++])
#define NEEDBITS(n) {while(k<(n)){b|=((u32)NEXTBYTE())<<k;k+=8;}}
#define DUMPBITS(n) {b>>=(n);k-=(n);}

// Huffman table structure
struct huft {
    u8 e;                /* number of extra bits or operation */
    u8 b;                /* number of bits in this code or subcode */
    union {
        u16 n;           /* literal, length base, or distance base */
        struct huft *t;  /* pointer to next level of table */
    } v;
};

// Main decompression class
class Decompressor {
public:
    Decompressor();
    
    // Main decompression function
    std::vector<u8> Decompress(const u8* src, size_t maxSize);
    
    // Helper function to get uncompressed size
    static u32 GetUncompressedSize(const u8* src);

private:
    // Tables and constants from the original code
    u8 bitLengthCodeOrder[19]; 
    u16 literalCopyLengths[31];
    u8 literalExtraBits[31];
    u16 distanceCopyOffsets[30];
    u8 distanceExtraBits[30];
    u16 compressionBitMask[17];
    
    // Variables used during decompression
    s32 literalBits;
    s32 distanceBits;
    u8 *compressionInputBuffer;
    u8 *outputBuffer;
    u32 inputBuffer;
    u32 writePointer;
    struct huft *currentHuft;
    u32 bitBuffer;
    u32 compressionBitCount;
    u32 compressionCRC1;
    u32 compressionCRC2;
    u32 huftCount;
    
    // Huffman table memory
    std::vector<huft> huftMemory;
    
    // Core decompression functions
    int BK_inflate();
    int bk_inflate_block(int *e);
    int bk_inflate_stored();
    int bk_inflate_fixed();
    int bk_inflate_dynamic();
    int bk_inflate_codes(struct huft *tl, struct huft *td, s32 bl, s32 bd);
    
    // Updated function declaration that matches the parameter types expected by callers
    int bk_huft_build(unsigned *b, unsigned n, unsigned s, u16 *d, u8 *e, struct huft **t, int *m);
    
    // Initialize tables and constants
    void InitTables();
};

} // namespace RareZip
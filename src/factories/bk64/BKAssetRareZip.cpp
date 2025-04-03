#include "BKAssetRareZip.h"
#include <cstring>

namespace RareZip {

Decompressor::Decompressor() {
    // Initialize the decompressor
    InitTables();
    
    // Reserve space for Huffman tables - sufficiently large for most cases
    huftMemory.resize(WSIZE);
    
    // Set up pointers
    literalBits = 9;
    distanceBits = 6;
}

void Decompressor::InitTables() {
    // Initialize the bit length code order
    static const u8 bitLengthOrder[] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    memcpy(bitLengthCodeOrder, bitLengthOrder, sizeof(bitLengthOrder));
    
    // Initialize the literal copy lengths
    static const u16 literalCopyLens[] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0
    };
    memcpy(literalCopyLengths, literalCopyLens, sizeof(literalCopyLens));
    
    // Initialize the literal extra bits
    static const u8 literalExtraBit[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 99, 99
    };
    memcpy(literalExtraBits, literalExtraBit, sizeof(literalExtraBit));
    
    // Initialize the distance copy offsets
    static const u16 distCopyOffsets[] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
    };
    memcpy(distanceCopyOffsets, distCopyOffsets, sizeof(distCopyOffsets));
    
    // Initialize the distance extra bits
    static const u8 distExtraBits[] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
        6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    memcpy(distanceExtraBits, distExtraBits, sizeof(distExtraBits));
    
    // Initialize the compression bit masks
    static const u16 bitMasks[] = {
        0x0000, 0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
        0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff
    };
    memcpy(compressionBitMask, bitMasks, sizeof(bitMasks));
}

u32 Decompressor::GetUncompressedSize(const u8* src) {
    return *((u32*)(src + 2));
}

std::vector<u8> Decompressor::Decompress(const u8* src, size_t maxSize) {
    // Get the uncompressed size from the header
    u32 uncompressedSize = GetUncompressedSize(src);
    
    // Prepare output buffer
    std::vector<u8> output(uncompressedSize);
    
    // Setup decompression state
    compressionInputBuffer = (u8*)src + COMP_HEADER_SIZE;
    outputBuffer = output.data();
    currentHuft = huftMemory.data();
    inputBuffer = 0;
    
    // Run the decompression
    BK_inflate();
    
    // Return the decompressed data
    return output;
}

int Decompressor::BK_inflate() {
    int e;      /* last block flag */
    int r;      /* result code */
    unsigned h; /* maximum huft's allocated */
    
    // Initialize for decompression
    writePointer = 0;
    compressionBitCount = 0;
    bitBuffer = 0;
    compressionCRC1 = 0;
    compressionCRC2 = -1;
    huftCount = 0;
    
    // Decompress until the last block
    h = 0;
    do {
        huftCount = 0;
        if ((r = bk_inflate_block(&e)) != 0)
            return r;
        if (huftCount > h)
            h = huftCount;
    } while (!e);
    
    // Undo too much lookahead. The next read will be byte aligned so we
    // can discard unused bits in the last meaningful byte.
    while (compressionBitCount >= 8) {
        compressionBitCount -= 8;
        inputBuffer--;
    }
    
    // Return success
    return 0;
}

int Decompressor::bk_inflate_block(int *e) {
    u32 t;               /* block type */
    register u32 b;      /* bit buffer */
    register unsigned k; /* number of bits in bit buffer */
    
    // Make local bit buffer
    b = bitBuffer;
    k = compressionBitCount;
    
    // Read in last block bit
    NEEDBITS(1)
    *e = (int)b & 1;
    DUMPBITS(1)
    
    // Read in block type
    NEEDBITS(2)
    t = (unsigned)b & 3;
    DUMPBITS(2)
    
    // Restore the global bit buffer
    bitBuffer = b;
    compressionBitCount = k;
    
    // Inflate that block type
    if (t == 2)
        return bk_inflate_dynamic();
    if (t == 0)
        return bk_inflate_stored();
    if (t == 1)
        return bk_inflate_fixed();
    
    // Bad block type
    return 2;
}

int Decompressor::bk_inflate_stored() {
    unsigned n;          /* number of bytes in block */
    unsigned w;          /* current window position */
    register u32 b;      /* bit buffer */
    register unsigned k; /* number of bits in bit buffer */
    
    // Make local copies of globals
    b = bitBuffer;
    k = compressionBitCount;
    w = writePointer;
    
    // Go to byte boundary
    n = k & 7;
    DUMPBITS(n);
    
    // Get the length and its complement
    NEEDBITS(16)
    n = ((unsigned)b & 0xffff);
    DUMPBITS(16)
    NEEDBITS(16)
    DUMPBITS(16)
    
    // Read and output the compressed data
    while (n--) {
        NEEDBITS(8)
        outputBuffer[w++] = (u8)b;
        compressionCRC1 += b & 0xFF;
        compressionCRC2 ^= (b & 0xFF) << (compressionCRC1 & 0x17);
        DUMPBITS(8)
    }
    
    // Restore globals
    writePointer = w;
    bitBuffer = b;
    compressionBitCount = k;
    return 0;
}

int Decompressor::bk_inflate_fixed() {
    int i;           /* temporary variable */
    struct huft *tl; /* literal/length code table */
    struct huft *td; /* distance code table */
    int bl;          /* lookup bits for tl */
    int bd;          /* lookup bits for td */
    unsigned l[288]; /* length list for bk_huft_build */
    
    // Set up literal table
    for (i = 0; i < 144; i++)
        l[i] = 8;
    for (; i < 256; i++)
        l[i] = 9;
    for (; i < 280; i++)
        l[i] = 7;
    for (; i < 288; i++)
        l[i] = 8;
    bl = 7;
    
    // Build the literal/length table
    tl = currentHuft + huftCount;
    bk_huft_build(l, 288, 257, literalCopyLengths, literalExtraBits, &tl, &bl);
    
    // Set up distance table
    for (i = 0; i < 30; i++)
        l[i] = 5;
    bd = 5;
    
    // Build the distance table
    td = currentHuft + huftCount;
    bk_huft_build(l, 30, 0, distanceCopyOffsets, distanceExtraBits, &td, &bd);
    
    // Decompress until an end-of-block code
    return bk_inflate_codes(tl, td, bl, bd);
}

int Decompressor::bk_inflate_dynamic() {
    int i;           /* temporary variables */
    unsigned j;
    unsigned l;      /* last length */
    unsigned m;      /* mask for bit lengths table */
    unsigned n;      /* number of lengths to get */
    struct huft *tl; /* literal/length code table */
    struct huft *td; /* distance code table */
    int bl;          /* lookup bits for tl */
    int bd;          /* lookup bits for td */
    unsigned nb;     /* number of bit length codes */
    unsigned nl;     /* number of literal/length codes */
    unsigned nd;     /* number of distance codes */
    unsigned ll[286 + 30]; /* literal/length and distance code lengths */
    register u32 b;  /* bit buffer */
    register unsigned k; /* number of bits in bit buffer */
    
    // Make local bit buffer
    b = bitBuffer;
    k = compressionBitCount;
    
    // Read in table lengths
    NEEDBITS(5)
    nl = 257 + ((unsigned)b & 0x1f);  /* number of literal/length codes */
    DUMPBITS(5)
    NEEDBITS(5)
    nd = 1 + ((unsigned)b & 0x1f);    /* number of distance codes */
    DUMPBITS(5)
    NEEDBITS(4)
    nb = 4 + ((unsigned)b & 0xf);     /* number of bit length codes */
    DUMPBITS(4)
    
    // Check lengths
    if (nl > 286 || nd > 30)
        return 1;  /* bad lengths */
    
    // Read in bit-length-code lengths
    for (j = 0; j < nb; j++) {
        NEEDBITS(3)
        ll[bitLengthCodeOrder[j]] = (unsigned)b & 7;
        DUMPBITS(3)
    }
    for (; j < 19; j++)
        ll[bitLengthCodeOrder[j]] = 0;
    
    // Build decoding table for trees--single level, 7 bit lookup
    bl = 7;
    tl = currentHuft + huftCount;
    bk_huft_build(ll, 19, 19, NULL, NULL, &tl, &bl);
    
    // Read in literal and distance code lengths
    n = nl + nd;
    m = compressionBitMask[bl];
    i = l = 0;
    while ((unsigned)i < n) {
        NEEDBITS((unsigned)bl)
        j = (td = tl + ((unsigned)b & m))->b;
        DUMPBITS(j)
        j = td->v.n;
        if (j < 16)              /* length of code in bits (0..15) */
            ll[i++] = l = j;     /* save last length in l */
        else if (j == 16) {      /* repeat last length 3 to 6 times */
            NEEDBITS(2)
            j = 3 + ((unsigned)b & 3);
            DUMPBITS(2)
            if ((unsigned)i + j > n)
                return 1;
            while (j--)
                ll[i++] = l;
        } else if (j == 17) {    /* 3 to 10 zero length codes */
            NEEDBITS(3)
            j = 3 + ((unsigned)b & 7);
            DUMPBITS(3)
            if ((unsigned)i + j > n)
                return 1;
            while (j--)
                ll[i++] = 0;
            l = 0;
        } else {                  /* j == 18: 11 to 138 zero length codes */
            NEEDBITS(7)
            j = 11 + ((unsigned)b & 0x7f);
            DUMPBITS(7)
            if ((unsigned)i + j > n)
                return 1;
            while (j--)
                ll[i++] = 0;
            l = 0;
        }
    }
    
    // Restore the global bit buffer
    bitBuffer = b;
    compressionBitCount = k;
    
    // Build the decoding tables for literal/length and distance codes
    bl = literalBits;
    tl = currentHuft + huftCount;
    bk_huft_build(ll, nl, 257, literalCopyLengths, literalExtraBits, &tl, &bl);
    
    bd = distanceBits;
    td = currentHuft + huftCount;
    bk_huft_build(ll + nl, nd, 0, distanceCopyOffsets, distanceExtraBits, &td, &bd);
    
    // Decompress until an end-of-block code
    return bk_inflate_codes(tl, td, bl, bd);
}

int Decompressor::bk_inflate_codes(struct huft *tl, struct huft *td, s32 bl, s32 bd) {
    register unsigned e;  /* table entry flag/number of extra bits */
    unsigned n, d;        /* length and index for copy */
    unsigned w;           /* current window position */
    struct huft *t;       /* pointer to table entry */
    unsigned ml, md;      /* masks for bl and bd bits */
    register u32 b;       /* bit buffer */
    register unsigned k;  /* number of bits in bit buffer */
    register u8 tmp;
    
    // Make local copies of globals
    b = bitBuffer;        /* initialize bit buffer */
    k = compressionBitCount;
    w = writePointer;     /* initialize window position */
    
    // Inflate the coded data
    ml = compressionBitMask[bl];    /* precompute masks for speed */
    md = compressionBitMask[bd];
    
    for (;;) { // Do until end of block
        NEEDBITS((unsigned)bl)
        if ((e = (t = tl + ((unsigned)b & ml))->e) > 16)
            do {
                DUMPBITS(t->b)
                e -= 16;
                NEEDBITS(e)
            } while ((e = (t = t->v.t + ((unsigned)b & compressionBitMask[e]))->e) > 16);
        DUMPBITS(t->b)
        
        if (e == 16) { // It's a literal
            tmp = (u8)t->v.n;
            outputBuffer[w++] = tmp;
            compressionCRC1 += tmp;
            compressionCRC2 ^= tmp << (compressionCRC1 & 0x17);
        } else if (e == 15) { // End of block
            break;
        } else { // It's a length
            // Get length of block to copy
            NEEDBITS(e)
            n = t->v.n + ((unsigned)b & compressionBitMask[e]);
            DUMPBITS(e);
            
            // Decode distance of block to copy
            NEEDBITS((unsigned)bd)
            if ((e = (t = td + ((unsigned)b & md))->e) > 16)
                do {
                    DUMPBITS(t->b)
                    e -= 16;
                    NEEDBITS(e)
                } while ((e = (t = t->v.t + ((unsigned)b & compressionBitMask[e]))->e) > 16);
            DUMPBITS(t->b)
            NEEDBITS(e)
            d = w - t->v.n - ((unsigned)b & compressionBitMask[e]);
            DUMPBITS(e)
            
            // Do the copy
            do {
                tmp = outputBuffer[d++];
                outputBuffer[w++] = tmp;
                compressionCRC1 += tmp;
                compressionCRC2 ^= tmp << (compressionCRC1 & 0x17);
            } while (--n);
        }
    }
    
    // Restore globals
    writePointer = w;    /* restore global window pointer */
    bitBuffer = b;       /* restore global bit buffer */
    compressionBitCount = k;
    
    return 0;
}

int Decompressor::bk_huft_build(unsigned *b, unsigned n, unsigned s, u16 *d, u8 *e, 
                              struct huft **t, int *m) {
    unsigned a;              /* counter for codes of length k */
    unsigned c[BMAX + 1];    /* bit length count table */
    unsigned f;              /* i repeats in table every f entries */
    int g;                   /* maximum code length */
    int h;                   /* table level */
    register unsigned i;     /* counter, current code */
    register unsigned j;     /* counter */
    register int k;          /* number of bits in current code */
    int l;                   /* bits per table (returned in m) */
    register unsigned *p;    /* pointer into c[], b[], or v[] */
    register struct huft *q; /* points to current table */
    struct huft r;           /* table entry for structure assignment */
    struct huft *u[BMAX];    /* table stack */
    unsigned v[N_MAX];       /* values in order of bit length */
    register int w;          /* bits before this table == (l * h) */
    unsigned x[BMAX + 1];    /* bit offsets, then code stack */
    unsigned *xp;            /* pointer into x */
    int y;                   /* number of dummy codes added */
    unsigned z;              /* number of entries in current table */
    
    // Generate counts for each bit length
    memset(c, 0, sizeof(c));
    p = b;
    i = n;
    do {
        c[*p]++;  /* assume all entries <= BMAX */
        p++;      /* Can't combine with above line (Solaris bug) */
    } while (--i);
    
    if (c[0] == n) {  /* null input--all zero length codes */
        *t = (struct huft *)NULL;
        *m = 0;
        return 0;
    }
    
    // Find minimum and maximum length, bound *m by those
    l = *m;
    for (j = 1; j <= BMAX; j++)
        if (c[j])
            break;
    k = j;  /* minimum code length */
    if ((unsigned)l < j)
        l = j;
    for (i = BMAX; i; i--)
        if (c[i])
            break;
    g = i;  /* maximum code length */
    if ((unsigned)l > i)
        l = i;
    *m = l;
    
    // Adjust last length count to fill out codes, if needed
    for (y = 1 << j; j < i; j++, y <<= 1)
        if ((y -= c[j]) < 0)
            return 2;  /* bad input: more codes than bits */
    if ((y -= c[i]) < 0)
        return 2;
    c[i] += y;
    
    // Generate starting offsets into the value table for each length
    x[1] = j = 0;
    p = c + 1;
    xp = x + 2;
    while (--i) {  /* note that i == g from above */
        *xp++ = (j += *p++);
    }
    
    // Make a table of values in order of bit lengths
    p = b;
    i = 0;
    do {
        if ((j = *p++) != 0)
            v[x[j]++] = i;
    } while (++i < n);
    
    // Generate the Huffman codes and for each, make the table entries
    x[0] = i = 0;               /* first Huffman code is zero */
    p = v;                      /* grab values in bit order */
    h = -1;                     /* no tables yet--level -1 */
    w = -l;                     /* bits decoded == (l * h) */
    u[0] = (struct huft *)NULL; /* just to keep compilers happy */
    q = (struct huft *)NULL;    /* ditto */
    z = 0;                      /* ditto */
    
    // Go through the bit lengths (k already is bits in shortest code)
    for (; k <= g; k++) {
        a = c[k];
        while (a--) {
            // Here i is the Huffman code of length k bits for value *p
            // Make tables up to required level
            while (k > w + l) {
                h++;
                w += l;  /* previous table always l bits */
                
                // Compute minimum size table less than or equal to l bits
                z = (z = g - w) > (unsigned)l ? l : z;  /* upper limit on table size */
                if ((f = 1 << (j = k - w)) > a + 1) {   /* try a k-w bit table */
                    // Too few codes for k-w bit table
                    f -= a + 1;  /* deduct codes from patterns left */
                    xp = c + k;
                    while (++j < z) {  /* try smaller tables up to z bits */
                        if ((f <<= 1) <= *++xp)
                            break;  /* enough codes to use up j bits */
                        f -= *xp;   /* else deduct codes from patterns */
                    }
                }
                
                z = 1 << j;  /* table entries for j-bit table */
                
                // Allocate and link in new table
                q = currentHuft + huftCount;
                huftCount += z + 1;  /* track memory usage */
                *t = q + 1;          /* link to list for huft_free() */
                *(t = &(q->v.t)) = (struct huft *)NULL;
                u[h] = ++q;          /* table starts after link */
                
                // Connect to last table, if there is one
                if (h) {
                    x[h] = i;           /* save pattern for backing up */
                    r.b = (u8)l;        /* bits to dump before this table */
                    r.e = (u8)(16 + j); /* bits in this table */
                    r.v.t = q;          /* pointer to this table */
                    j = i >> (w - l);   /* (get around Turbo C bug) */
                    u[h - 1][j] = r;    /* connect to last table */
                }
            }
            
            // Set up table entry in r
            r.b = (u8)(k - w);
            if (p >= v + n)
                r.e = 99;  /* out of values--invalid code */
            else if (*p < s) {
                r.e = (u8)(*p < 256 ? 16 : 15);  /* 256 is end-of-block code */
                r.v.n = *p;                      /* simple code is just the value */
                p++;                             /* one compiler does not like *p++ */
            } else {
                r.e = *(e + (*p - s));     /* non-simple--look up in lists */
                r.v.n = d[*p++ - s];
            }
            
            // Fill code-like entries with r
            f = 1 << (k - w);
            for (j = i >> w; j < z; j += f)
                q[j] = r;
            
            // Backwards increment the k-bit code i
            for (j = 1 << (k - 1); i & j; j >>= 1)
                i ^= j;
            i ^= j;
            
            // Backup over finished tables
            while ((i & ((1 << w) - 1)) != x[h]) {
                h--;      /* don't need to update q */
                w -= l;
            }
        }
    }
    
    // Return true (1) if we were given an incomplete table
    return y != 0 && g != 1;
}

} // namespace RareZip
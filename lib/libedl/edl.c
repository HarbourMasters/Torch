#include "edl.h"
#include <stdint.h>

/*data*/

/*800E0B40*/
static uint8_t _table1[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x14, 0x18, 0x1C,
    0x20, 0x28, 0x30, 0x38, 0x40, 0x50, 0x60, 0x70, 0x80, 0xA0, 0xC0, 0xE0, 0xFF, 0x00, 0x00, 0x00,
};

/*800E0B60*/
static uint8_t _table2[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 0, 0, 0,
};

/*800E0B80*/
static uint16_t _table3[30] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0006, 0x0008, 0x000C, 0x0010, 0x0018, 0x0020, 0x0030, 0x0040, 0x0060, 0x0080,
    0x00C0, 0x0100, 0x0180, 0x0200, 0x0300, 0x0400, 0x0600, 0x0800, 0x0C00, 0x1000, 0x1800, 0x2000, 0x3000, 0x4000, 0x6000,
};

/*800E0BBC*/
static int8_t _table4[36] = {
    0x0, 0x0, 0x0, 0x0, 0x1, 0x1, 0x2, 0x2, 0x3, 0x3, 0x4, 0x4, 0x5, 0x5, 0x6, 0x6, 0x7, 0x7,
    0x8, 0x8, 0x9, 0x9, 0xA, 0xA, 0xB, 0xB, 0xC, 0xC, 0xD, 0xD, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};

/*comm*/
/*8012CD90*/ uint32_t _what[288] ALIGNED(16);
/*80168820*/ uint32_t _samp[287] ALIGNED(16);
/*801978A0*/ uint32_t _when[288] ALIGNED(16);

/*.text*/
static void _parseEDLheader(EDLInfo *info);
static int32_t _swap(EDLInfo *info, uint32_t value);

/*800808D0*/
static void _decodeEDL0(EDLInfo *info)
{
    uint8_t *src;
    uint8_t *dst;
    int32_t i;
    int32_t size;

    dst = info->dst;
    i = size = info->dsize;
    src = &info->src[12];

    if ((src >= dst) || (dst >= &src[size]))
    {
        for (i = (size-1); i != -1; i--)
            *dst++ = *src++;
    }
    else
    {
        dst = &dst[size-1];
        src = &src[size-1];

        for (i = (size-1); i != -1; i--)
            *dst-- = *src--;
    }
}

#if SYS_ENDIAN == SYS_BIG_ENDIAN
#define SWAP32(A) A
#else
#define SWAP32(A) _swap(info, A)
#endif

#define GET_BITS_(OUTVAR,BITCOUNT,M) \
    { \
    count -= BITCOUNT; \
    if (count < 0) \
    { \
        OUTVAR = data; \
        data = next; \
        next = SWAP32(*in++); \
        OUTVAR = (OUTVAR + (data << (count + BITCOUNT))) & ((M<<BITCOUNT)-1); \
        data >>= -count; \
        count += 0x20; \
    } \
    else \
    { \
        OUTVAR = data & ((M<<BITCOUNT)-1); \
        data >>= BITCOUNT; \
    } \
    }

/*TODO*/
#define GET_BITS1(OUTVAR,BITCOUNT) GET_BITS_(OUTVAR,BITCOUNT,1)
#define GET_BITS2(OUTVAR,BITCOUNT) GET_BITS_(OUTVAR,BITCOUNT,one)

/*80080968*/
static void _decodeEDL1(EDLInfo *info)
{
    uint32_t number[0x21];
    uint8_t sp88[0x400];
    uint16_t large[0x600];
    uint16_t small[0x300];
    uint16_t *sp1694;
    int32_t sp169C;
    uint32_t sp16A4;
    uint32_t sp16BC;

    uint32_t i, j, k, l, m, n, o, p, q, r, s;
    int32_t count;
    int32_t *in;
    uint8_t *out;
    uint32_t data, next;
    uint8_t one;

    uint8_t *src;
    uint8_t *ptr1;
    uint8_t *dst;
    uint8_t *ptr2;

    src = &info->src[12];
    ptr1 = &src[info->csize] - 1;
    dst = info->dst;
    if ((uintptr_t)ptr1 >= (uintptr_t)dst)
    {
        ptr2 = &info->dst[info->dsize];
        if ((uintptr_t)(ptr2 - 1) >= (uintptr_t)ptr1)
        {
            i = 0;
            ptr1 = ptr2 - info->csize;
            ptr2 = src;
            do
            {
                *ptr1++ = *ptr2++;
                i++;
            } while (i < (uint32_t)info->csize);
            src = &dst[info->dsize] - info->csize;
        }
    }

    in = (int32_t *)src;
    data = SWAP32(*in++);
    next = SWAP32(*in++);
    out = info->dst;

    count = 32;
    sp16A4 = 0;
    one = 1; /*FAKEMATCH*/
    do
    {
        GET_BITS1(j, 1);

        /*mode 0 - */
        if (j == 0)
        {
            GET_BITS1(i, 15);

            for (i = i - 1; (int32_t)i != -1; i--)
            {
                GET_BITS1(j, 8);
                *out++ = j;
            }
        }
        else
        {
            /*mode 1 - tables*/
            sp169C = 1;
            do
            {
                sp1694 = &small[0];
                r = 8;
                if (sp169C-- != 0)
                {
                    sp1694 = &large[0];
                    r = 10;
                }

                GET_BITS1(j, 9);

                /*construct tables*/
                s = j;
                if (j != 0)
                {
                    for (k = 1; k < ARRAY_COUNT(number); k++)
                        number[k] = 0;

                    /*iterate to grab entries*/
                    k = 0;
                    do
                    {
                        GET_BITS1(j, 1);

                        if (j == one)
                            GET_BITS1(sp16A4, 4);

                        _what[k] = sp16A4;
                        number[sp16A4]++;
                        k++;
                    } while (k < s);

                    /*build occurance table*/
                    number[0] = 0;
                    m = 0;
                    for (k = 1; k < 16; k++)
                    {
                        for (l = 0; l < s; l++)
                        {
                            if (_what[l] == k)
                            {
                                _when[m] = l;
                                m++;
                            }
                        }
                    }

                    /*sort nibbles*/
                    m = 0;
                    for (i = 1; i < 16; i++)
                    {
                        for (k = 1; number[i] >= k; k++)
                        {
                            _what[m] = i;
                            m++;
                        }
                    }

                    /*generate bitsample table*/
                    _what[m] = 0;
                    k = 0;
                    m = 0;
                    for (i = _what[0]; _what[m] != 0; i++)
                    {
                        for (p = 0; _what[m] == i; p++, m++)
                        {
                            l = k | (one << i);
                            k++;
                            _samp[m] = 0;

                            do
                            {
                                _samp[m] <<= 1;
                                _samp[m] += (l & 1);
                                l >>= 1;
                            } while (l != one);
                        }
                        k <<= 1;
                    }

                    k = 0;
                    do
                    {
                        sp88[k] = 0;
                        k++;
                    } while (k < (uint32_t)(one << r));

                    m = 0;
                    for (i = 1; i < r; i++)
                    {
                        for (k = 1; number[i] >= k; k++, m++)
                        {
                            j = _samp[m];

                            do
                            {
                                sp1694[j] = (_when[m] << 7) + i;
                                j += (one << i);
                            } while (j < (uint32_t)(one << r));
                        }
                    }

                    n = m;
                    for (; i < 16; i++)
                    {
                        for (k = 1; number[i] >= k; k++, m++)
                        {
                            j = _samp[m];
                            l = _what[m];

                            if (sp88[j & ((one << r) - 1)] < l)
                                sp88[j & ((one << r) - 1)] = l;
                        }
                    }

                    j = 0;
                    k = 0;
                    do
                    {
                        if ((sp88[k] & 0xFF) != 0)
                        {
                            if (((sp88[k] & 0xFF) - r) >= 8)
                            {
                                info->result = -8;
                                return;
                            }
                            sp1694[k] = (j << 7) + ((sp88[k] - r) << 4);
                            j += one << (sp88[k] - r);
                        }
                        k++;
                    } while (k < (uint32_t)(one << r));

                    if (j >= 0x200)
                    {
                        info->result = -9;
                        return;
                    }

                    m = n;
                    i = r;
                    if (i < 16)
                    {
                        uint16_t *ptr;
                        o = one << i;
                        sp16BC = o - 1;
                        do
                        {
                            for (k = 1; number[i] >= k; k++, m++)
                            {
                                j = _samp[m];
                                l = j >> r;
                                ptr = &sp1694[(sp1694[j & sp16BC] >> 7) + o];
                                j = (sp1694[j & sp16BC] >> 4) & 7;
                                do
                                {
                                    ptr[l] = (_when[m] << 7) + _what[m];
                                    l += one << (_what[m] - r);
                                } while (l < (uint32_t)(one << j));
                            }
                            i++;
                        } while (i < 16);
                    }
                }
            } while (sp169C >= 0);

            /*write data*/
            do
            {
                if (count < 0xF)
                    q = data + (next << count);
                else
                    q = data;

                i = large[(q & 0x3FF)];
                if ((i & 0xF) == 0)
                {
                    uint16_t *a, *b;
                    j = i >> 7;
                    a = &large[0x400];
                    b = &a[((q >> 10) & ((one << ((i >> 4) & 7)) - 1)) + j];
                    i = b[0];
                }

                GET_BITS1(j, (i & 0xF));

                i >>= 7;
                if (i < 0x100)
                {
                    *out = i;
                    out += 1;
                }
                else if (i != 0x100)
                {
                    j = 0;
                    if (_table2[i-0x101] != 0)
                        GET_BITS2(j, _table2[i-0x101]);

                    dst = _table1; /*FAKEMATCH?*/
                    i = (dst[i-0x101] + j) + 3;
                    if (count < 0xF)
                        q = data + (next << count);
                    else
                        q = data;

                    l = small[q & 0xFF];
                    if ((l & 0xF) == 0)
                    {
                        uint16_t *a, *b;
                        j = l >> 7;
                        a = &small[0x100];
                        b = &a[((q >> 8) & ((one << ((l >> 4) & 7)) - 1)) + j];
                        l = b[0];
                    }

                    GET_BITS1(j, (l & 0xF));

                    /*pull number of bits*/
                    l >>= 7;
                    j = 0;
                    if (_table4[l] != 0)
                        GET_BITS2(j, _table4[l]);

                    l = _table3[l] + j;
                    ptr2 = (out - l) - 1;
                    if (l == 0)
                    {
                        do
                        {
                            *out++ = *ptr2;
                            i--;
                        } while (i != 0);
                    }
                    else
                    {
                        do
                        {
                            *out++ = *ptr2++;
                            i--;
                        } while (i != 0);
                    }
                }
                else
                    break;
            } while (1);
        }

        /*test EOF*/
        GET_BITS1(j, 1);
    } while (j == 0);
}

/*800813F8*/
static void _decodeEDL(EDLInfo *info) {
    _parseEDLheader(info);
    if (info->result == 0)
    {
        switch (info->type)
        {
        case 0:
            _decodeEDL0(info);
            break;
        case 1:
            _decodeEDL1(info);
            break;
        }
    }
}

/*80081460*/
static void _parseEDLheader(EDLInfo *info)
{
    if ((info->src[0] == 'E') && (info->src[1] == 'D') && (info->src[2] == 'L'))
    {
        info->file_endian = info->src[3] >> 7;
        info->type = info->src[3] & 0x7F;
        if (info->type < 2)
        {
            if (info->type >= 0)
            {
                info->result = 0;
                info->csize = _swap(info, *(uint32_t *)&info->src[4]);
                info->dsize = _swap(info, *(uint32_t *)&info->src[8]);
            }
            else
                info->result = -4;
        }
        else
            info->result = -4;
    }
    else
        info->result = -3;
}

/*8008151C*/
static int32_t _swap(EDLInfo *info, uint32_t value)
{
    if (info->file_endian == info->sys_endian)
        return value;
    else
        return (value >> 24) + ((value >> 8) & 0xFF00) + ((value & 0xFF00) << 8) + (value << 24);
}

/*8008155C*/
int32_t getEDLDecompressedSize(uint8_t *src)
{
    EDLInfo info;

    info.src = src;
    info.sys_endian = SYS_ENDIAN;
    _parseEDLheader(&info);
    if (info.result == 0)
        return info.dsize;
    else
        return 0;
}

/*80081598*/
int32_t decompressEDL(EDLInfo* info, void *src, void *dst)
{
    info->src = src;
    info->sys_endian = SYS_ENDIAN;
    info->dst = dst;
    _decodeEDL(info);
    return info->result;
}

/*800815CC*/
static int32_t _isEDL(uint8_t *src)
{
    EDLInfo info;

    info.sys_endian = SYS_ENDIAN;
    info.src = src;
    info.dst = 0;
    _parseEDLheader(&info);
    return info.result != -3;
}
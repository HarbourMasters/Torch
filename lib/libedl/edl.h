#pragma once

#include <stdint.h>

#define ALIGNED(x) __attribute__((aligned(x)))

#define ARRAY_COUNT(arr) \
    (int32_t)(sizeof(arr) / sizeof(arr[0]))

#define SYS_LITTLE_ENDIAN 0
#define SYS_BIG_ENDIAN 1

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SYS_ENDIAN SYS_LITTLE_ENDIAN
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SYS_ENDIAN SYS_BIG_ENDIAN
#else
#error Platform not supported
#endif

typedef struct {
    uint8_t *dst;
    uint8_t *src;
    int32_t csize;
    int32_t dsize;
    int32_t type;
    int32_t sys_endian;
    int32_t file_endian;
    int32_t result;
} EDLInfo;

typedef struct
{
    uint8_t *romstart;
    uint8_t *romend;
    uint8_t **handle;
    int32_t *offset;
} edlUnkStruct1;

#define EDL_FILE_MAX_SIZE 0x5B108

int32_t decompressEDL(EDLInfo* info, void *src, void *dst);
int32_t getEDLDecompressedSize(uint8_t *src);
void allocacheEDL(void *handle, int32_t size);
void edl_80081688(void *handle, int32_t id);
void edl_80081760(void *handle, int32_t id, void *dst);
uint8_t *edl_80081840(int16_t id, int16_t off);

extern edlUnkStruct1 D_800E0D18[32];
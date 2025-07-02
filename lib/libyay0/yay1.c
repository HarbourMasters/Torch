#include "yay1.h"
#include "libmio0/utils.h"
#include <string.h>
#include <stdlib.h>

// defines

#define GET_BIT(buf, bit) ((buf)[(bit) / 8] & (1 << (7 - ((bit) % 8))))

// types
typedef struct
{
   int *indexes;
   int allocated;
   int count;
   int start;
} lookback;

// functions
#define LOOKBACK_COUNT 256
#define LOOKBACK_INIT_SIZE 128
static lookback *lookback_init(void)
{
   lookback *lb = malloc(LOOKBACK_COUNT * sizeof(*lb));
   for (int i = 0; i < LOOKBACK_COUNT; i++) {
      lb[i].allocated = LOOKBACK_INIT_SIZE;
      lb[i].indexes = malloc(lb[i].allocated * sizeof(*lb[i].indexes));
      lb[i].count = 0;
      lb[i].start = 0;
   }
   return lb;
}

static void lookback_free(lookback *lb)
{
   for (int i = 0; i < LOOKBACK_COUNT; i++) {
      free(lb[i].indexes);
   }
   free(lb);
}

static inline void lookback_push(lookback *lkbk, unsigned char val, int index)
{
   lookback *lb = &lkbk[val];
   if (lb->count == lb->allocated) {
      lb->allocated *= 4;
      lb->indexes = realloc(lb->indexes, lb->allocated * sizeof(*lb->indexes));
   }
   lb->indexes[lb->count++] = index;
}

static void PUT_BIT(unsigned char *buf, int bit, int val)
{
   unsigned char mask = 1 << (7 - (bit % 8));
   unsigned int offset = bit / 8;
   buf[offset] = (buf[offset] & ~(mask)) | (val ? mask : 0);
}

// used to find longest matching stream in buffer
// buf: buffer
// start_offset: offset in buf to look back from
// max_search: max number of bytes to find
// found_offset: returned offset found (0 if none found)
// returns max length of matching stream (0 if none found)
static int find_longest(const unsigned char *buf, int start_offset, int max_search, int *found_offset, lookback *lkbk)
{
   int best_length = 0;
   int best_offset = 0;
   int cur_length;
   int search_len;
   int farthest, off, i;
   int lb_idx;
   const unsigned char first = buf[start_offset];
   lookback *lb = &lkbk[first];

   // buf
   //  |    off        start                  max
   //  V     |+i->       |+i->                 |
   //  |--------------raw-data-----------------|
   //        |+i->       |      |+i->
   //                       +cur_length

   // check at most the past 4096 values
   farthest = MAX(start_offset - 4096, 0);
   // find starting index
   for (lb_idx = lb->start; lb_idx < lb->count && lb->indexes[lb_idx] < farthest; lb_idx++) {}
   lb->start = lb_idx;
   for ( ; lb_idx < lb->count && lb->indexes[lb_idx] < start_offset; lb_idx++) {
      off = lb->indexes[lb_idx];
      // check at most requested max or up until start
      search_len = MIN(max_search, start_offset - off);
      for (i = 0; i < search_len; i++) {
         if (buf[start_offset + i] != buf[off + i]) {
            break;
         }
      }
      cur_length = i;
      // if matched up until start, continue matching in already matched parts
      if (cur_length == search_len) {
         // check at most requested max less current length
         search_len = max_search - cur_length;
         for (i = 0; i < search_len; i++) {
            if (buf[start_offset + cur_length + i] != buf[off + i]) {
               break;
            }
         }
         cur_length += i;
      }
      if (cur_length > best_length) {
         best_offset = start_offset - off;
         best_length = cur_length;
      }
   }

   // return best reverse offset and length (may be 0)
   *found_offset = best_offset;
   return best_length;
}

int32_t yay1_encode(const uint8_t *in_buf, uint32_t length, uint8_t* out_buf) {
    unsigned char *bit_buf;
    unsigned char *comp_buf;
    unsigned char *uncomp_buf;
    unsigned int bit_length;
    unsigned int comp_offset;
    unsigned int uncomp_offset;
    unsigned int bytes_proc = 0;
    int bytes_written;
    int bit_idx = 0;
    int comp_idx = 0;
    int uncomp_idx = 0;
    lookback *lookbacks;
 
    // initialize lookback buffer
    lookbacks = lookback_init();
 
    // allocate some temporary buffers worst case size
    bit_buf = malloc((length + 7) / 8); // 1-bit/byte
    comp_buf = malloc(length); // 16-bits/2bytes
    uncomp_buf = malloc(length); // all uncompressed
    memset(bit_buf, 0, (length + 7) / 8);
 
    // encode data
    // special case for first byte
    lookback_push(lookbacks, in_buf[0], 0);
    uncomp_buf[uncomp_idx] = in_buf[0];
    uncomp_idx += 1;
    bytes_proc += 1;
    PUT_BIT(bit_buf, bit_idx++, 1);
    while (bytes_proc < length) {
       int offset;
       int max_length = MIN(length - bytes_proc, 0x111);
       int longest_match = find_longest(in_buf, bytes_proc, max_length, &offset, lookbacks);
       // push current byte before checking next longer match
       lookback_push(lookbacks, in_buf[bytes_proc], bytes_proc);
       if (longest_match > 2) {
          int lookahead_offset;
          // lookahead to next byte to see if longer match
          int lookahead_length = MIN(length - bytes_proc - 1, 0x111);
          int lookahead_match = find_longest(in_buf, bytes_proc + 1, lookahead_length, &lookahead_offset, lookbacks);
          // better match found, use uncompressed + lookahead compressed
          if ((longest_match + 1) < lookahead_match) {
             // uncompressed byte
             uncomp_buf[uncomp_idx] = in_buf[bytes_proc];
             uncomp_idx++;
             PUT_BIT(bit_buf, bit_idx, 1);
             bytes_proc++;
             longest_match = lookahead_match;
             offset = lookahead_offset;
             bit_idx++;
             lookback_push(lookbacks, in_buf[bytes_proc], bytes_proc);
          }
          // first byte already pushed above
          for (int i = 1; i < longest_match; i++) {
             lookback_push(lookbacks, in_buf[bytes_proc + i], bytes_proc + i);
          }
          // compressed block
          comp_buf[comp_idx] = (((longest_match - 3) & 0x0F) << 4) |
                               (((offset - 1) >> 8) & 0x0F);
          comp_buf[comp_idx + 1] = (offset - 1) & 0xFF;
          comp_idx += 2;
          PUT_BIT(bit_buf, bit_idx, 0);
          bytes_proc += longest_match;
       } else {
          // uncompressed byte
          uncomp_buf[uncomp_idx] = in_buf[bytes_proc];
          uncomp_idx++;
          PUT_BIT(bit_buf, bit_idx, 1);
          bytes_proc++;
       }
       bit_idx++;
    }
 
    // compute final sizes and offsets
    // +7 so int division accounts for all bits
    bit_length = ((bit_idx + 7) / 8);
    // compressed data after control bits and aligned to 4-byte boundary
    comp_offset = ALIGN(YAY1_HEADER_LENGTH + bit_length, 4);
    uncomp_offset = comp_offset + comp_idx;
    bytes_written = uncomp_offset + uncomp_idx;
 
    // output header
    memcpy(out_buf, "Yay1", 4);
    write_u32_be(&out_buf[4], length);
    write_u32_be(&out_buf[8], comp_offset);
    write_u32_be(&out_buf[12], uncomp_offset);
    // output data
    memcpy(&out_buf[YAY1_HEADER_LENGTH], bit_buf, bit_length);
    memcpy(&out_buf[comp_offset], comp_buf, comp_idx);
    memcpy(&out_buf[uncomp_offset], uncomp_buf, uncomp_idx);
 
    // free allocated buffers
    free(bit_buf);
    free(comp_buf);
    free(uncomp_buf);
    lookback_free(lookbacks);
 
    return bytes_written;
}

uint8_t* yay1_decode(const uint8_t* in_buf, uint32_t* out_size){

    const uint8_t* in = in_buf;

    if(strncmp(in, "Yay1", 4) != 0){
        return NULL;
    }

    uint32_t decompressed_size = read_u32_be(in + 4);
    uint32_t link_table_offset = read_u32_be(in + 8);
    uint32_t chunk_offset      = read_u32_be(in + 12);

    uint32_t link_table_idx = link_table_offset;
    uint32_t chunk_idx = chunk_offset;
    uint32_t other_idx = 16;

    uint32_t mask_bit_counter = 0;
    uint32_t current_mask = 0;
    uint32_t idx = 0;

    uint8_t* out = malloc(decompressed_size);
    memset(out, 0, decompressed_size);
    *out_size = decompressed_size;

    while(idx < decompressed_size){
        if(mask_bit_counter == 0){
            current_mask = read_u32_be(in + other_idx);
            other_idx += 4;
            mask_bit_counter = 32;
        }

        if(current_mask & 0x80000000){
            out[idx] = in[chunk_idx];
            idx++;
            chunk_idx++;
        } else {
            uint16_t link = read_u16_be(in + link_table_idx);
            link_table_idx += 2;
            uint32_t offset = idx - (link & 0xFFF);
            uint32_t count = link >> 12;

            if(count == 0){
                uint8_t count_modifier = in[chunk_idx];
                chunk_idx++;
                count = count_modifier + 18;
            } else {
                count += 2;
            }

            for(size_t i = 0; i < count; i++){
                out[idx] = out[offset + i - 1];
                idx++;
            }
        }

        current_mask <<= 1;
        mask_bit_counter--;
    }

    return out;
}

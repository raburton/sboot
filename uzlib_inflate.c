/*
 * tinfgzip.c   - tiny gzip decompressor
 * tinflate.c  -  tiny inflate
 *
 * The original source headers as below for licence compliance and in
 * full acknowledgement of the originitor contributions.  Modified by
 * Terry Ellison 2018 to provide lightweight stream inflate for NodeMCU
 * Lua.  Modifications are under the standard NodeMCU MIT licence.
 * Further modification by Richard Burton (richardaburton@gmail.com)
 * for use with sBoot.
 *
 * Copyright (c) 2003 by Joergen Ibsen / Jibz
 * All Rights Reserved
 * http://www.ibsensoftware.com/
 *
 * Copyright (c) 2014-2016 by Paul Sokolovsky
 *
 * This software is provided 'as-is', without any express
 * or implied warranty.  In no event will the authors be
 * held liable for any damages arising from the use of
 * this software.
 *
 * Permission is granted to anyone to use this software
 * for any purpose, including commercial applications,
 * and to alter it and redistribute it freely, subject to
 * the following restrictions:
 *
 * 1. The origin of this software must not be
 *    misrepresented; you must not claim that you
 *    wrote the original software. If you use this
 *    software in a product, an acknowledgment in
 *    the product documentation would be appreciated
 *    but is not required.
 *
 * 2. Altered source versions must be plainly marked
 *    as such, and must not be misrepresented as
 *    being the original software.
 *
 * 3. This notice may not be removed or altered from
 *    any source distribution.
 */

#include <string.h>
#include <stdio.h>

#include "uzlib.h"

#ifdef DEBUG_COUNTS
#define DBG_PRINT(...) printf(__VA_ARGS__)
#define DBG_COUNT(n) (debugCounts[n]++)
#define DBG_ADD_COUNT(n,m) (debugCounts[n]+=m)
int32_t debugCounts[20];
#else
#define NDEBUG
#define DBG_PRINT(...)
#define DBG_COUNT(n)
#define DBG_ADD_COUNT(n,m)
#endif


#define SIZE(arr) (sizeof(arr) / sizeof(*(arr)))

int32_t dbg_break(void) {return 1;}

/* data structures */

typedef struct {
   uint16_t table[16];  /* table of code length counts */
   uint16_t trans[288]; /* code -> symbol translation table */
} UZLIB_TREE;

typedef struct {
	uint8_t decomp_buffer[32*1024];
	uint32_t decomp_pos;

	uint8_t *source;
	uint32_t source_len;
	uint32_t source_pos;
 /*
  * extra bits and base tables for length and distance codes
  */
  uint8_t  lengthBits[30];
  uint16_t lengthBase[30];
  uint8_t  distBits[30];
  uint16_t distBase[30];
 /*
  * special ordering of code length codes
  */
  uint8_t  clcidx[19];
 /*
  * dynamic length/symbol and distance trees
  */
  UZLIB_TREE ltree;
  UZLIB_TREE dtree;
 /*
  * methods encapsulate handling of the input and output streams
  */
  void (*put_bytes)(void*, uint8_t*, uint);
  uint32_t (*get_bytes)(void*);
  // user data passed to callbacks
  void *cb_data;
 /*
  * Other state values
  */
  uint32_t tag;
  uint32_t bitcount;
  uint32_t lzOffs;
  int32_t  bType;
  int32_t  bFinal;
  uint32_t curLen;
  uint32_t destSize;
  uint32_t checksum;
} UZLIB_DATA;



static const uint32_t tinf_crc32tab[16] = {
   0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190,
   0x6b6b51f4, 0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344,
   0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278,
   0xbdbdf21c
};

uint32_t uzlib_crc32(const uint8_t *data, uint32_t len, uint32_t crc)
{
   uint32_t i;

   for (i = 0; i < len; ++i) {
      crc ^= data[i];
      crc = tinf_crc32tab[crc & 0x0f] ^ (crc >> 4);
      crc = tinf_crc32tab[crc & 0x0f] ^ (crc >> 4);
   }

   return crc;
}

static uint8_t get_byte(UZLIB_DATA *d) {
	if (d->source_pos >= d->source_len) {
		d->source_len = d->get_bytes(d->cb_data);
		d->source_pos = 0;
	}
	//ets_printf("get 0x%02x\n", d->source[d->source_pos]);
	return d->source[d->source_pos++];
}

static void push_bytes(UZLIB_DATA *d) {
	// write out the buffer
	put_bytes(d->cb_data, d->decomp_buffer, d->decomp_pos);
	// update checksum
	d->checksum = uzlib_crc32(d->decomp_buffer, d->decomp_pos, d->checksum);
	// update length
	d->destSize += d->decomp_pos;
	// circle back to start
	d->decomp_pos = 0;
}

static void put_byte(UZLIB_DATA *d, uint8_t data) {
	// reached end of buffer?
	if (d->decomp_pos >= sizeof(d->decomp_buffer)) push_bytes(d);
	// store new byte
	//ets_printf("put 0x%02x (%c) at 0x%08x\n", data, data, decomp_pos);
	d->decomp_buffer[d->decomp_pos++] = data;
}

static uint8_t recall_byte(UZLIB_DATA *d, uint32_t offset) {
	if (offset < d->decomp_pos) {
		//ets_printf("recall1 0x%08x from 0x%08x (0x%08x), 0x%02x (%c)\n", offset, decomp_pos, decomp_pos-offset, decomp_buffer[decomp_pos-offset], decomp_buffer[decomp_pos-offset]);
		return d->decomp_buffer[d->decomp_pos-offset];
	} else {
		uint32_t remainder = offset-d->decomp_pos;
		//ets_printf("recall2 0x%08x from 0x%08x (0x%08x), 0x%02x (%c)\n", offset, decomp_pos, sizeof(decomp_buffer)-remainder, decomp_buffer[sizeof(decomp_buffer)-remainder], decomp_buffer[sizeof(decomp_buffer)-remainder]);
		return d->decomp_buffer[sizeof(d->decomp_buffer)-remainder];
	}
}

/*
 * Note on changes to layout, naming, etc.  This module combines extracts
 * from 3 code files from two sources (Sokolovsky, Ibsen et al) with perhaps
 * 30% from me Terry Ellison. These sources had inconsistent layout and
 * naming conventions, plus extra condtional handling of platforms that
 * cannot support NodeMCU. (This is intended to be run compiled and executed
 * on GCC POSIX and XENTA newlib environments.)  So I have (1) reformatted
 * this file in line with NodeMCU rules; (2) demoted all private data and
 * functions to static and removed the redundant name  prefixes; (3) reordered
 * functions into a more logic order; (4) added some ESP architecture
 * optimisations, for example these IoT devices are very RAM limited, so
 * statically allocating large RAM blocks is against programming guidelines.
 */

static void skip_bytes(UZLIB_DATA *d, int32_t num) {
  if (num)             /* Skip a fixed number of bytes */
    while (num--) (void) get_byte(d);
  else                 /* Skip to next nullchar */
    while (get_byte(d)) {}
}

static uint16_t get_uint16(UZLIB_DATA *d) {
  uint16_t v = get_byte(d);
  return v | (get_byte(d) << 8);
}

static uint32_t get_le_uint32 (UZLIB_DATA *d) {
  uint32_t v = get_uint16(d);
  return  v | ((uint) get_uint16(d) << 16);
}

/* get one bit from source stream */
static int32_t getbit (UZLIB_DATA *d) {
  uint32_t bit;

  /* check if tag is empty */
  if (!d->bitcount--) {
    /* load next tag */
    d->tag = get_byte(d);
    d->bitcount = 7;
  }

  /* shift bit out of tag */
  bit = d->tag & 0x01;
  d->tag >>= 1;

  return bit;
}

/* read a num bit value from a stream and add base */
static uint32_t read_bits (UZLIB_DATA *d, int32_t num, int32_t base) {
 /* This is an optimised version which doesn't call getbit num times */
  if (!num)
    return base;

  uint32_t i, n = (((uint)-1)<<num);
  for (i = d->bitcount; i < num; i +=8)
    d->tag |= ((uint)get_byte(d)) << i;

  n = d->tag & ~n;
  d->tag >>= num;
  d->bitcount = i - num;
  return base + n;
}

/* --------------------------------------------------- *
 * -- uninitialized global data (static structures) -- *
 * --------------------------------------------------- */

/*
 * Constants are stored in flash memory on the ESP8266 NodeMCU firmware
 * builds, but only word aligned data access are supported in hardare so
 * short and byte accesses are handled by a S/W exception handler and
 * are SLOW.  RAM is also at premium, especially static initialised vars,
 * so we malloc a single block on first call to hold all tables and call
 * the dynamic generator to generate malloced RAM tables that have the
 * same content as the above statically declared versions.
 *
 * This might seem a bit convolved but this runs faster and takes up
 * less memory than the static version on the ESP8266.
 */

#define CLCIDX_INIT \
"\x10\x11\x12\x00\x08\x07\x09\x06\x0a\x05\x0b\x04\x0c\x03\x0d\x02\x0e\x01\x0f"

/* ----------------------- *
 * -- utility functions -- *
 * ----------------------- */

/* build extra bits and base tables */
static void build_bits_base (uint8_t *bits, uint16_t *base,
                             int32_t delta, int32_t first) {
  int32_t i, sum;

  /* build bits table */
  for (i = 0; i < delta; ++i) bits[i] = 0;
  for (i = 0; i < 30 - delta; ++i) bits[i + delta] = i / delta;

  /* build base table */
  for (sum = first, i = 0; i < 30; ++i) {
    base[i] = sum;
    sum += 1 << bits[i];
  }
}

/* build the fixed huffman trees */
static void build_fixed_trees (UZLIB_TREE *lt, UZLIB_TREE *dt) {
  int32_t i;

  /* build fixed length tree */
  for (i = 0; i < 7; ++i) lt->table[i] = 0;

  lt->table[7] = 24;
  lt->table[8] = 152;
  lt->table[9] = 112;

  for (i = 0; i < 24; ++i)  lt->trans[i] = 256 + i;
  for (i = 0; i < 144; ++i) lt->trans[24 + i] = i;
  for (i = 0; i < 8; ++i)   lt->trans[24 + 144 + i] = 280 + i;
  for (i = 0; i < 112; ++i) lt->trans[24 + 144 + 8 + i] = 144 + i;

  /* build fixed distance tree */
  for (i = 0; i < 5; ++i)   dt->table[i] = 0;
  dt->table[5] = 32;

  for (i = 0; i < 32; ++i)  dt->trans[i] = i;
}

/* given an array of code lengths, build a tree */
static void build_tree (UZLIB_TREE *t, const uint8_t *lengths, uint32_t num) {
  uint16_t offs[16];
  uint32_t i, sum;

  /* clear code length count table */
  for (i = 0; i < 16; ++i)
    t->table[i] = 0;

  /* scan symbol lengths, and sum code length counts */
  for (i = 0; i < num; ++i)
    t->table[lengths[i]]++;
  t->table[0] = 0;

  /* compute offset table for distribution sort */
  for (sum = 0, i = 0; i < 16; ++i) {
    offs[i] = sum;
    sum += t->table[i];
  }

  /* create code->symbol translation table (symbols sorted by code) */
  for (i = 0; i < num; ++i) {
    if (lengths[i])
      t->trans[offs[lengths[i]]++] = i;
  }
}

/* ---------------------- *
 * -- decode functions -- *
 * ---------------------- */

/* given a data stream and a tree, decode a symbol */
static int32_t decode_symbol (UZLIB_DATA *d, UZLIB_TREE *t) {
  int32_t sum = 0, cur = 0, len = 0;

  /* get more bits while code value is above sum */
  do {
    cur = 2*cur + getbit(d);

    if (++len == SIZE(t->table))
      return UZLIB_DATA_ERROR;

    sum += t->table[len];
    cur -= t->table[len];

  } while (cur >= 0);

  sum += cur;
  if (sum < 0 || sum >= SIZE(t->trans))
    return UZLIB_DATA_ERROR;

  return t->trans[sum];
}

/* given a data stream, decode dynamic trees from it */
static int32_t decode_trees (UZLIB_DATA *d, UZLIB_TREE *lt, UZLIB_TREE *dt) {
  uint8_t lengths[288+32];
  uint32_t hlit, hdist, hclen, hlimit;
  uint32_t i, num, length;

  /* get 5 bits HLIT (257-286) */
  hlit = read_bits(d, 5, 257);

  /* get 5 bits HDIST (1-32) */
  hdist = read_bits(d, 5, 1);

  /* get 4 bits HCLEN (4-19) */
  hclen = read_bits(d, 4, 4);

  for (i = 0; i < 19; ++i) lengths[i] = 0;

  /* read code lengths for code length alphabet */
  for (i = 0; i < hclen; ++i) {
    /* get 3 bits code length (0-7) */
    uint32_t clen = read_bits(d, 3, 0);
    lengths[d->clcidx[i]] = clen;
  }

  /* build code length tree, temporarily use length tree */
  build_tree(lt, lengths, 19);

  /* decode code lengths for the dynamic trees */
  hlimit = hlit + hdist;
  for (num = 0; num < hlimit; ) {
    int32_t sym = decode_symbol(d, lt);
    uint8_t fill_value = 0;
    int32_t lbits, lbase = 3;

    /* error decoding */
    if (sym < 0)
      return sym;

    switch (sym) {
    case 16:
      /* copy previous code length 3-6 times (read 2 bits) */
      fill_value = lengths[num - 1];
      lbits = 2;
      break;
    case 17:
      /* repeat code length 0 for 3-10 times (read 3 bits) */
      lbits = 3;
      break;
    case 18:
      /* repeat code length 0 for 11-138 times (read 7 bits) */
      lbits = 7;
      lbase = 11;
      break;
    default:
      /* values 0-15 represent the actual code lengths */
      lengths[num++] = sym;
      /* continue the for loop */
      continue;
    }

    /* special code length 16-18 are handled here */
    length = read_bits(d, lbits, lbase);
    if (num + length > hlimit)
      return UZLIB_DATA_ERROR;

    for (; length; --length)
      lengths[num++] = fill_value;
  }

  /* build dynamic trees */
  build_tree(lt, lengths, hlit);
  build_tree(dt, lengths + hlit, hdist);

  return UZLIB_OK;
}

/* ----------------------------- *
 * -- block inflate functions -- *
 * ----------------------------- */

/* given a stream and two trees, inflate a block of data */
static int32_t inflate_block_data (UZLIB_DATA *d, UZLIB_TREE *lt, UZLIB_TREE *dt) {
  if (d->curLen == 0) {
    int32_t dist;
    int32_t sym = decode_symbol(d, lt);

    /* literal byte */
    if (sym < 256) {
       DBG_PRINT("huff sym: %02x   %c\n", sym, sym);
       put_byte(d, sym);
       return UZLIB_OK;
    }

    /* end of block */
    if (sym == 256)
       return UZLIB_DONE;

    /* substring from sliding dictionary */
    sym -= 257;
    /* possibly get more bits from length code */
    d->curLen = read_bits(d, d->lengthBits[sym], d->lengthBase[sym]);
    dist = decode_symbol(d, dt);
    /* possibly get more bits from distance code */
    d->lzOffs = read_bits(d, d->distBits[dist], d->distBase[dist]);
    DBG_PRINT("huff dict: -%u for %u\n", d->lzOffs, d->curLen);
  }

  /* copy next byte from dict substring */
  uint8_t b = recall_byte(d, d->lzOffs);
  DBG_PRINT("huff dict byte(%u): -%u -  %02x   %c\n\n",
          d->curLen, d->lzOffs, b, b);
  put_byte(d, b);
  d->curLen--;
  return UZLIB_OK;
}

/* inflate an uncompressed block of data */
static int32_t inflate_uncompressed_block (UZLIB_DATA *d) {
  if (d->curLen == 0) {
    uint32_t length    = get_uint16(d);
    uint32_t invlength = get_uint16(d);

    /* check length */
    if (length != (~invlength & 0x0000ffff))
      return UZLIB_DATA_ERROR;

    /* increment length to properly return UZLIB_DONE below, without
       producing data at the same time */
    d->curLen = length + 1;

    /* make sure we start next block on a byte boundary */
    d->bitcount = 0;
  }

  if (--d->curLen == 0) {
    return UZLIB_DONE;
  }

  put_byte(d, get_byte(d));
  return UZLIB_OK;
}

/* -------------------------- *
 * -- main parse functions -- *
 * -------------------------- */

static int32_t parse_gzip_header(UZLIB_DATA *d) {

  /* check id bytes */
  if (get_byte(d) != 0x1f || get_byte(d) != 0x8b)
    return UZLIB_DATA_ERROR;

  if (get_byte(d) != 8) /* check method is deflate */
    return UZLIB_DATA_ERROR;

  uint8_t flg = get_byte(d);/* get flag byte */

  if (flg & 0xe0)/* check that reserved bits are zero */
    return UZLIB_DATA_ERROR;

  skip_bytes(d, 6);            /* skip rest of base header of 10 bytes */

  if (flg & UZLIB_FEXTRA)            /* skip extra data if present */
     skip_bytes(d, get_uint16(d));

  if (flg & UZLIB_FNAME)             /* skip file name if present */
    skip_bytes(d,0);

  if (flg & UZLIB_FCOMMENT)          /* skip file comment if present */
    skip_bytes(d,0);

  if (flg & UZLIB_FHCRC)             /* ignore header crc if present */
    skip_bytes(d,2);

  return UZLIB_OK;
}


/* inflate next byte of compressed stream */
static int32_t uncompress_stream (UZLIB_DATA *d) {
  while (1) {
    int32_t res;

    /* start a new block */
    if (d->bType == -1) {
      next_blk:
      /* read final block flag */
      d->bFinal = getbit(d);
      /* read block type (2 bits) */
      d->bType = read_bits(d, 2, 0);

      DBG_PRINT("Started new block: type=%d final=%d\n", d->bType, d->bFinal);

      if (d->bType == 1) {
        /* build fixed huffman trees */
        build_fixed_trees(&d->ltree, &d->dtree);
      } else if (d->bType == 2) {
        /* decode trees from stream */
        res = decode_trees(d, &d->ltree, &d->dtree);
        if (res != UZLIB_OK)
          return res;
      }
    }

    /* process current block */
    switch (d->bType) {
    case 0:
      /* decompress uncompressed block */
      res = inflate_uncompressed_block(d);
      break;
    case 1:
    case 2:
      /* decompress block with fixed or dynamic huffman trees.  These */
      /* trees were decoded previously, so it's the same routine for both */
      res = inflate_block_data(d, &d->ltree, &d->dtree);
      break;
    default:
      return UZLIB_DATA_ERROR;
    }

    if (res == UZLIB_DONE && !d->bFinal) {
      /* the block has ended (without producing more data), but we
         can't return without data, so start procesing next block */
      goto next_blk;
    }

    if (res != UZLIB_OK)
      return res;

  }

  return UZLIB_OK;
}

/*
 * This implementation has a different usecase to Paul Sokolovsky's
 * uzlib implementation, in that it is designed to target IoT devices
 * such as the ESP8266.  Here clarity and compact code size is an
 * advantage, but the ESP8266 only has 40-45Kb free heap, and has to
 * process files with an unpacked size of up 256Kb, so a streaming
 * implementation is essential. It has then been tweaked again for
 * sBoot where we have more memory and want to use bigger buffers
 * for performance.
 * 
 *
 * User must provide the following two functions:
 *
 *   uint32_t get_bytes(void *cb_data)
 *     asks the user application to fill the (user supplied) buffer
 *     with data and return the length of data in the buffer
 *   void put_bytes(void *cb_data, uint8_t *decompressed_data, uint32_t length)
 *     passes decompressed data to the user application when the 32k
 *     output buffer is full and (less) when end of file is reached
 * 
 *  Both callbacks pass a user supplied pointer to which the user can
 *  attach a structure to keep track of their source buffer and any
 *  other user data associated with the decompression.
 *  A pointer to the source buffer neds to be passed in to uzlib_inflate
 *  but it does not need to contain data (or specify a length) yet
 *  as the get_bytes callback will be called straight away.
 */
int32_t uzlib_inflate (
     uint32_t (*get_bytes)(void*),
     void (*put_bytes)(void *, uint8_t *, uint),
	 void *cb_data,
	 uint8_t *source) {

  int32_t res;

  /* initialize decompression structure */
  UZLIB_DATA d;

  d.bitcount    = 0;
  d.bFinal      = 0;
  d.bType       = -1;
  d.curLen      = 0;
  d.destSize    = 0;
  d.get_bytes   = get_bytes;
  d.put_bytes   = put_bytes;
  d.cb_data	    = cb_data;
  d.source      = source;
  d.source_len  = 0;
  d.source_pos  = 0;
  d.decomp_pos  = 0;
  d.checksum    = 0xffffffff;

  /* create RAM copy of clcidx byte array */
  ets_memcpy(d.clcidx, CLCIDX_INIT, sizeof(d.clcidx));

  uint32_t checksum;
  uint32_t length;

  /* build extra bits and base tables */
  build_bits_base(d.lengthBits, d.lengthBase, 4, 3);
  build_bits_base(d.distBits, d.distBase, 2, 1);
  d.lengthBits[28] = 0;              /* fix a special case */
  d.lengthBase[28] = 258;

  // do the decompression
  if ((res = parse_gzip_header(&d)) != UZLIB_OK) return res;
  while ((res = uncompress_stream(&d)) == UZLIB_OK) {}
  if (res != UZLIB_DONE) return res;

  // flush remaining output from buffer
  if (d.decomp_pos > 0) push_bytes(&d);
  
  // check checksum and length
  d.checksum ^= 0xffffffff;
  if (get_le_uint32(&d) != d.checksum) return UZLIB_CHKSUM_ERROR;
  if (get_le_uint32(&d) != d.destSize) return UZLIB_LENGTH_ERROR;

  return UZLIB_DONE;
  
}

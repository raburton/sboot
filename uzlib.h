/*
 * uzlib  -  tiny deflate/inflate library (deflate, gzip, zlib)
 *
 * Copyright (c) 2003 by Joergen Ibsen / Jibz
 * All Rights Reserved
 * http://www.ibsensoftware.com/
 *
 * Copyright (c) 2014-2016 by Paul Sokolovsky
 */

#ifndef UZLIB_INFLATE_H
#define UZLIB_INFLATE_H

#include <stdint.h>

// ok status, more data produced
#define UZLIB_OK             0
// end of compressed stream reached
#define UZLIB_DONE           1
#define UZLIB_LENGTH_ERROR  (-2)
#define UZLIB_DATA_ERROR    (-3)
#define UZLIB_CHKSUM_ERROR  (-4)
#define UZLIB_DICT_ERROR    (-5)
#define UZLIB_MEMORY_ERROR  (-6)

// Gzip header codes
#define UZLIB_FTEXT    1
#define UZLIB_FHCRC    2
#define UZLIB_FEXTRA   4
#define UZLIB_FNAME    8
#define UZLIB_FCOMMENT 16

int32_t uzlib_inflate (uint32_t (*)(void *), void (*)(void *, uint8_t *, uint32_t), void *cb_data, uint8_t *);

// Checksum API
// crc is previous value for incremental computation, 0xffffffff initially
// and finally xor with 0xffffffff
uint32_t uzlib_crc32(const uint8_t *data, uint32_t length, uint32_t crc);

#endif /* UZLIB_INFLATE_H */


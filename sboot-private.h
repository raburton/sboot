#ifndef __SBOOT_PRIVATE_H__
#define __SBOOT_PRIVATE_H__

//////////////////////////////////////////////////
// sBoot open source boot loader for ESP8266.
// Copyright 2015-2019 Richard A Burton
// richardaburton@gmail.com
// See license.txt for license terms.
//////////////////////////////////////////////////

#include <sboot.h>
#include <spiffs.h>

#define NOINLINE __attribute__ ((noinline))

#define ROM_MAGIC	   0xe9
#define ROM_MAGIC_NEW1 0xea
#define ROM_MAGIC_NEW2 0x04

#define CHKSUM_INIT 0xef

#define MIN(a,b) (a<b ? a:b)
#define MAX(a,b) (a>b ? a:b)

// flash sector size
#define SECTOR_SIZE 0x1000

// buffer size, must be at least 0x10 (size of rom_header_new structure)
#define BUFFER_SIZE 0x100

// stage2 read chunk maximum size (limit for SPIRead)
#define READ_SIZE SECTOR_SIZE

// esp8266 built in rom functions
extern uint32_t SPIRead(uint32_t addr, void *outptr, uint32_t len);
extern uint32_t SPIEraseSector(int);
extern uint32_t SPIWrite(uint32_t addr, void *inptr, uint32_t len);
extern void ets_printf(const char*, ...);
extern void ets_delay_us(int);
extern void ets_memset(void*, uint8_t, uint32_t);
extern void ets_memcpy(void*, const void*, uint32_t);

// functions we'll call by address
typedef void stage2a(uint32_t);
typedef void usercode(void);

#define TRUE 1
#define FALSE 0

// standard rom header
typedef struct {
	// general rom header
	uint8_t magic;
	uint8_t count;
	uint8_t flags1;
	uint8_t flags2;
	usercode* entry;
} rom_header;

typedef struct {
	uint8_t* address;
	uint32_t length;
} section_header;

// new rom header (irom section first) there is
// another 8 byte header straight afterward the
// standard header
typedef struct {
	// general rom header
	uint8_t magic;
	uint8_t count; // second magic for new header
	uint8_t flags1;
	uint8_t flags2;
	uint32_t entry;
	// new type rom, lib header
	uint32_t add; // zero
	uint32_t len; // length of irom section
} rom_header_new;

// flash write status structure
typedef struct {
	int32_t start_addr;
	int32_t last_sector_erased;
	int32_t extra_count;
	uint8_t extra_bytes[4];
} flash_write_status;

// decompression data structure
typedef struct {
	flash_write_status flasher;
	spiffs_file fd;
	uint8_t source[SECTOR_SIZE];
	uint32_t dry_run;
} decomp_data;

// simple partition info
typedef struct {
	uint32_t boot_offset;
	uint32_t spiffs_offset;
	uint32_t spiffs_size;
} partition_info;

#endif

//////////////////////////////////////////////////
// sBoot open source boot loader for ESP8266.
// Copyright 2015-2019 Richard A Burton
// richardaburton@gmail.com
// See license.txt for license terms.
//////////////////////////////////////////////////

#include <sboot-private.h>
#include <sboot-hex2a.h>
#include <spiffs.h>
#include <uzlib.h>

////////////////////////////////////////////////////////////////
/// This code deals with spiffs integration, including aligned
/// spi reads (we are read only, so no writes or erases needed),
/// plus some functions we do with the fs.

#define LOG_PAGE_SIZE 256

static u8_t spiffs_fds[32*4];
static u8_t spiffs_work_buf[LOG_PAGE_SIZE*2];
static u8_t spiffs_cache_buf[(LOG_PAGE_SIZE+32)*4];

static spiffs fs;

static int32_t my_spi_read(uint32_t addr, uint32_t size, uint8_t *dst) {

	uint32_t aligned = addr & ~3;
	if (addr > aligned) {
		uint32_t c = MIN(4-(addr-aligned), size);
		uint8_t buff[4];
		SPIRead(aligned, buff, sizeof(buff));
		ets_memcpy(dst, buff+sizeof(buff)-c, c);
		addr += c;
		size -= c;
		dst += c;
	}

	if (size > 4) {
		uint32_t c = size & ~3;
		SPIRead(addr, dst, c);
		addr += c;
		size -= c;
		dst += c;
	}

	if (size > 0) {
		uint8_t buff[4];
		SPIRead(addr, buff, sizeof(buff));
		ets_memcpy(dst, buff, size);
	}

	return SPIFFS_OK;
}

static int32_t my_spiffs_mount(partition_info *parts) {
	spiffs_config cfg = {0};
	cfg.phys_size = parts->spiffs_size;
	cfg.phys_addr = parts->spiffs_offset;
	cfg.phys_erase_block = SECTOR_SIZE;
	cfg.log_block_size = SECTOR_SIZE;
	cfg.log_page_size = LOG_PAGE_SIZE;

	cfg.hal_read_f = my_spi_read;
	cfg.hal_write_f = 0;
	cfg.hal_erase_f = 0;

	return SPIFFS_mount(&fs,
	  &cfg,
	  spiffs_work_buf,
	  spiffs_fds,
	  sizeof(spiffs_fds),
	  spiffs_cache_buf,
	  sizeof(spiffs_cache_buf),
	  0);
}

static void list_directory(void) {
	spiffs_DIR d;
	struct spiffs_dirent e;
	struct spiffs_dirent *pe = &e;
	SPIFFS_opendir(&fs, "/", &d);
	ets_printf("\nContents of spiffs filesystem:\n", pe->name, pe->obj_id, pe->size);
	while ((pe = SPIFFS_readdir(&d, pe))) {
		ets_printf("    [id:%04x] size:0x%08x %s\n", pe->obj_id, pe->size, pe->name);
	}
	ets_printf("End of spiffs.\n\n");
	SPIFFS_closedir(&d);
}

////////////////////////////////////////////////////////////////
/// This code deals with aligned (4 byte) writes to the spi
/// flash.
///

// setup the write status struct, based on supplied start address
static void flash_write_init(flash_write_status *status, int32_t start_addr) {
	status->start_addr = start_addr;
	status->extra_count = 0;
	status->last_sector_erased = (start_addr / SECTOR_SIZE) - 1;
}

// function to do the actual writing to flash,
// call repeatedly with more data
static uint32_t flash_write(flash_write_status *status, uint8_t *data, uint32_t len) {
	
	uint8_t *buffer;
	uint32_t lastsect;

	if (data == NULL || len == 0) {
		return TRUE;
	}

	// erase any additional sectors needed by this chunk
	lastsect = ((status->start_addr + status->extra_count + len) - 1) / SECTOR_SIZE;
	while (lastsect > status->last_sector_erased) {
		status->last_sector_erased++;
		SPIEraseSector(status->last_sector_erased);
	}

	// finish any remaining bytes from previous write
	if (status->extra_count > 0) {
		while ((status->extra_count < 4) && (len > 0)) {
			status->extra_bytes[status->extra_count++] = data[0];
			data++;
			len--;
		}
		if (status->extra_count == 4) {
			SPIWrite(status->start_addr, (uint32_t *)((void *)status->extra_bytes), 4);
			status->start_addr += 4;
			status->extra_count = 0;
		}
	}

	if (len > 0) {
		// length must be multiple of 4 to write,
		// save any remaining bytes for next go
		uint32_t extras = len % 4;
		if (extras > 0) {
			status->extra_count = extras;
			len -= extras;
			ets_memcpy(status->extra_bytes, data+len, extras);
		}
	}

	if (len > 0) {
		// write main current chunk
		SPIWrite(status->start_addr, (uint32_t *)((void*)data), len);
		status->start_addr += len;
	}

	return TRUE;
}

// ensure any remaning bytes get written (needed for files not a multiple of 4 bytes)
static uint32_t flash_write_end(flash_write_status *status) {
	if (status->extra_count > 0) {
		// dummy data with which to make up 4 bytes (including existing extras)
		uint32_t data = 0xffffffff;
		return flash_write(status, (uint8_t*)&data, 4-status->extra_count);
	}
	return TRUE;
}

////////////////////////////////////////////////////////////////
/// This code deals with uzlib, for decompression of the OTA
/// image.
///

uint32_t get_source(void *cb_data) {
	decomp_data *decomp = (decomp_data *)cb_data;
	int32_t len = SPIFFS_read(&fs, decomp->fd, decomp->source, sizeof(decomp->source));
	if (len <= 0) {
		ets_printf("spiffs read error %d\n", len);
		return 0;
	}
	return len;
}

void put_bytes(void *cb_data, uint8_t *data, uint32_t len) {
	decomp_data *decomp = (decomp_data *)cb_data;
	if (!decomp->dry_run) flash_write(&decomp->flasher, data, len);
}

////////////////////////////////////////////////////////////////
/// This code is our main code, to process updates and start the
/// application.
/// 

// if you want to use some kind of dynamic partition
// layout replace this function with appropriate code
void get_partitions(partition_info *parts) {
	parts->boot_offset = BOOT_IMAGE_OFFSET;
	parts->spiffs_offset = BOOT_SPIFFS_OFFSET;
	parts->spiffs_size = BOOT_SPIFFS_SIZE;
}

static uint32_t perform_update(partition_info *parts) {

	uint32_t ret = FALSE;
	decomp_data decomp;

	ets_printf("Testing new rom... ");
	// open ota file
	decomp.fd = SPIFFS_open(&fs, BOOT_OTA_FILE, SPIFFS_RDONLY, 0);
	if (decomp.fd < 0) {
		ets_printf("spiffs open error %d\n", decomp.fd);
	} else {
		// dry run to check file decompresses ok
		decomp.dry_run = 1;
		uint32_t res = uzlib_inflate(get_source, put_bytes, &decomp, decomp.source);
		if (res == UZLIB_DONE) {
			// real extraction run
			ets_printf("passed.\nInstalling new rom... ");
			flash_write_init(&decomp.flasher, parts->boot_offset);
			SPIFFS_lseek(&fs, decomp.fd, 0, SPIFFS_SEEK_SET);
			decomp.dry_run = 0;
			uzlib_inflate(get_source, put_bytes, &decomp, decomp.source);
			flash_write_end(&decomp.flasher);
			ets_printf("complete.\n");
			ret = TRUE;
		} else if (res == UZLIB_CHKSUM_ERROR) ets_printf("failed: bad checksum.\n");
		else if (res == UZLIB_LENGTH_ERROR) ets_printf("failed: bad length.\n");
		else ets_printf("failed: 0x%0x\n", res);
		// close ota file
		SPIFFS_close(&fs, decomp.fd);
	}
	return ret;
}

// check if update is available
// returns true if ota file exists and it differs from installed image
static uint32_t need_update(partition_info *parts) {
	uint32_t ret = FALSE;
	uint32_t addr;
	uint32_t read_len;
	uint8_t buffer[SECTOR_SIZE];
	uint32_t ota_len;
	uint32_t ota_crc;
	uint32_t rom_crc = 0xffffffff;
	spiffs_file fd;

	ets_printf("Checking spiffs for update file... ");

	// open ota file
	fd = SPIFFS_open(&fs, BOOT_OTA_FILE, SPIFFS_RDONLY, 0);
	if (fd < 0) {
		if (fd == SPIFFS_ERR_NOT_FOUND) ets_printf("not found.\n");
		else ets_printf("spiffs open error %d\n", fd);
	} else {
		ets_printf("found.\nChecking existing rom... ");
		// read gzip footer (crc & size)
		if (SPIFFS_lseek(&fs, fd, -8, SPIFFS_SEEK_END) >= 0) {
			if (SPIFFS_read(&fs, fd, (u8_t *)buffer, 8) == 8) {
				ota_crc = buffer[0] | (buffer[1]<<8) | (buffer[2]<<16) | (buffer[3]<<24);
				ota_len = buffer[4] | (buffer[5]<<8) | (buffer[6]<<16) | (buffer[7]<<24);

				// crc the installed image
				addr = parts->boot_offset;
				read_len = (ota_len & 3) ? (ota_len | 3) + 1 : ota_len;
				while (read_len > 0) {
					uint32_t read_next = MIN(sizeof(buffer), read_len);
					SPIRead(addr, buffer, read_next);
					rom_crc = uzlib_crc32(buffer, MIN(read_next, ota_len), rom_crc);
					addr += read_next;
					read_len -= read_next;
					ota_len -= read_next;
				}
				rom_crc ^= 0xffffffff;
				ret = (ota_crc != rom_crc);
				if (ret) ets_printf("update needed.\n");
				else  ets_printf("update not needed.\n");
			} else {
				ets_printf("spiffs read error %d\n", SPIFFS_errno(&fs));
			}
		} else {
			ets_printf("spiffs lseek error %d\n", SPIFFS_errno(&fs));
		}

		// close ota file
		SPIFFS_close(&fs, fd);
	}

	return ret;
}

// validate a rom image in flash and find address of rom header
static uint32_t check_image(uint32_t readpos) {

	uint8_t buffer[BUFFER_SIZE];
	uint8_t sectcount;
	uint8_t sectcurrent;
	uint8_t chksum = CHKSUM_INIT;
	uint32_t loop;
	uint32_t remaining;
	uint32_t romaddr;

	rom_header_new *header = (rom_header_new*)buffer;
	section_header *section = (section_header*)buffer;

	if (readpos == 0 || readpos == 0xffffffff) {
		return 0;
	}

	// read rom header
	if (SPIRead(readpos, header, sizeof(rom_header_new)) != 0) {
		return 0;
	}

	// check header type
	if (header->magic == ROM_MAGIC) {
		// old type, no extra header or irom section to skip over
		romaddr = readpos;
		readpos += sizeof(rom_header);
		sectcount = header->count;
	} else if (header->magic == ROM_MAGIC_NEW1 && header->count == ROM_MAGIC_NEW2) {
		// new type, has extra header and irom section first
		romaddr = readpos + header->len + sizeof(rom_header_new);
		// skip the extra header and irom section
		readpos = romaddr;
		// read the normal header that follows
		if (SPIRead(readpos, header, sizeof(rom_header)) != 0) {
			return 0;
		}
		sectcount = header->count;
		readpos += sizeof(rom_header);
	} else {
		return 0;
	}

	// test each section
	for (sectcurrent = 0; sectcurrent < sectcount; sectcurrent++) {

		// read section header
		if (SPIRead(readpos, section, sizeof(section_header)) != 0) {
			return 0;
		}
		readpos += sizeof(section_header);

		// get section address and length
		remaining = section->length;

		while (remaining > 0) {
			// work out how much to read, up to BUFFER_SIZE
			uint32_t readlen = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
			// read the block
			if (SPIRead(readpos, buffer, readlen) != 0) {
				return 0;
			}
			// increment next read position
			readpos += readlen;
			// decrement remaining count
			remaining -= readlen;
			// add to chksum
			for (loop = 0; loop < readlen; loop++) {
				chksum ^= buffer[loop];
			}
		}

	}

	// round up to next 16 and get checksum
	readpos = readpos | 0x0f;
	if (SPIRead(readpos, buffer, 1) != 0) {
		return 0;
	}

	// compare calculated and stored checksums
	if (buffer[0] != chksum) {
		return 0;
	}

	return romaddr;
}

// prevent this function being placed inline with main
// to keep main's stack size as small as possible
// don't mark as static or it'll be optimised out when
// using the assembler stub
uint32_t NOINLINE real_main(void) {

	uint32_t loadAddr;
	partition_info parts;

#if BOOT_DELAY_MICROS
	// delay to slow boot (help see messages when debugging)
	ets_delay_us(BOOT_DELAY_MICROS);
#endif

	ets_printf("\nsBoot v1.0.0 - richardaburton@gmail.com\n");

	// get partition info
	get_partitions(&parts);
	// mount the fs
	int32_t res = my_spiffs_mount(&parts);
	if (res >= 0) {
#if BOOT_LIST_DIRECTORY
		// list contents of spiffs
		list_directory();
#endif
		// check for and perform update from spiffs
		if (need_update(&parts)) perform_update(&parts);
		// unmount the fs
		SPIFFS_unmount(&fs);
	} else {
		ets_printf("spiffs mount error: %d\n", res);
	}

	// check rom image
	loadAddr = check_image(BOOT_IMAGE_OFFSET);
	if (loadAddr == 0) ets_printf("No bootable rom found at 0x%08x.\n", parts.boot_offset);
	else ets_printf("Booting rom at 0x%08x.\n", loadAddr);
	// copy the loader to top of iram
	ets_memcpy((void*)_text_addr, _text_data, _text_len);
	// return address to load from
	return loadAddr;
}

// assembler stub uses no stack space
// works with gcc
void call_user_start(void) {
	__asm volatile (
		"mov a15, a0\n"          // store return addr, hope nobody wanted a15!
		"call0 real_main\n"      // do stuff and find a rom to boot
		"mov a0, a15\n"          // restore return addr
		"bnez a2, 1f\n"          // ?success
		"ret\n"                  // no, return
		"1:\n"                   // yes...
		"movi a3, entry_addr\n"  // get pointer to entry_addr
		"l32i a3, a3, 0\n"       // get value of entry_addr
		"jx a3\n"                // now jump to it
	);
}


//////////////////////////////////////////////////
// sBoot open source boot loader for ESP8266.
// Copyright 2015-2019 Richard A Burton
// richardaburton@gmail.com
// See license.txt for license terms.
//////////////////////////////////////////////////

#include "sboot-private.h"

usercode* NOINLINE load_rom(uint32_t readpos) {
	
	uint8_t sectcount;
	uint8_t *writepos;
	uint32_t remaining;
	usercode* usercode;
	
	rom_header header;
	section_header section;
	
	// read rom header
	SPIRead(readpos, &header, sizeof(rom_header));
	readpos += sizeof(rom_header);

	// create function pointer for entry point
	usercode = header.entry;
	
	// copy all the sections
	for (sectcount = header.count; sectcount > 0; sectcount--) {
		
		// read section header
		SPIRead(readpos, &section, sizeof(section_header));
		readpos += sizeof(section_header);

		// get section address and length
		writepos = section.address;
		remaining = section.length;
		
		while (remaining > 0) {
			// work out how much to read
			uint32_t readlen = (remaining < READ_SIZE) ? remaining : READ_SIZE;
			// read the block
			SPIRead(readpos, writepos, readlen);
			readpos += readlen;
			// increment next write position
			writepos += readlen;
			// decrement remaining count
			remaining -= readlen;
		}
	}

	return usercode;
}

void call_user_start(uint32_t readpos) {
	__asm volatile (
		"mov a15, a0\n"     // store return addr, we already splatted a15!
		"call0 load_rom\n"  // load the rom
		"mov a0, a15\n"     // restore return addr
		"jx a2\n"           // now jump to the rom code
	);
}


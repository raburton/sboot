// Copyright 2015 Richard A Burton
// richardaburton@gmail.com
// See license.txt for license terms.

#include <stdint.h>

void call_user_start(void) {
	uint8_t loop;
	for(loop = 0; loop < 10; loop++) {
		ets_printf("testload\n");
		ets_delay_us(20000);
	}
}

#ifndef __SBOOT_H__
#define __SBOOT_H__

//////////////////////////////////////////////////
// sBoot open source boot loader for ESP8266.
// Copyright 2015-2019 Richard A Burton
// richardaburton@gmail.com
// See license.txt for license terms.
//////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// uncomment to add a boot delay, allows you time to connect
// a terminal before sBoot starts to run and output messages
// value is in microseconds
#define BOOT_DELAY_MICROS 2000000

// uncomment to list the contents of the spiffs on boot
#define BOOT_LIST_DIRECTORY 1

// ota file to check for in spiffs
#define BOOT_OTA_FILE "testload.gz"

// next 3 can be hard coded, or provide an alternative
// get_partitions function in sboot.c
// 
// offset of spiffs in flash
#define BOOT_SPIFFS_OFFSET 0x80000
//
// spiffs size
#define BOOT_SPIFFS_SIZE (1024*100)
//
// offset of rom image in flash
#define BOOT_IMAGE_OFFSET 0xa0000


#ifdef __cplusplus
}
#endif

#endif

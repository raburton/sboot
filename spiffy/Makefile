#
# Makefile for spiffy
#
# Pass in TARGET, BUILD_DIR , SPIFFS_SMING, SPIFFS_BASE
#

HOST_CC ?= gcc
HOST_LD ?= gcc

TARGET 		?= spiffy
BUILD_DIR	?= build
SPIFFS_DIR	?= ../spiffs

INCDIR := -I.. -I$(SPIFFS_DIR)/src
CFLAGS := -O2 -Wall -Wno-unused-value -DSPIFFS_READ_ONLY=0

ifeq ($(V),1)
Q :=
else
Q := @
endif

OBJS := $(addprefix $(BUILD_DIR)/,spiffy.o spiffs_cache.o spiffs_nucleus.o spiffs_hydrogen.o spiffs_gc.o spiffs_check.o)

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	@echo "mkdir $@"
	$(Q) mkdir $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c ../spiffs_config.h
	@echo "CC $<"
	$(Q) $(HOST_CC) $(CFLAGS) $(INCDIR) -c $< -o $@

$(BUILD_DIR)/%.o: $(SPIFFS_DIR)/src/%.c $(SPIFFS_DIR)/src/spiffs.h $(SPIFFS_DIR)/src/spiffs_nucleus.h ../spiffs_config.h
	@echo "CC $<"
	$(Q) $(HOST_CC) $(CFLAGS) $(INCDIR) -c $< -o $@

$(TARGET): $(OBJS)
	@echo "LD $@"
	$(Q) $(HOST_LD) -o $@ $^

clean:
	$(Q) rm -rf $(BUILD_DIR) $(TARGET)


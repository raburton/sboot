#
# Makefile for sBoot
# https://github.com/raburton/sboot
#

ESPTOOL2 ?= ../esptool2/esptool2

SBOOT_BUILD_BASE ?= build
SBOOT_FW_BASE    ?= firmware
SPIFFS_BASE ?= spiffs/src

ifndef XTENSA_BINDIR
CC := xtensa-lx106-elf-gcc
LD := xtensa-lx106-elf-gcc
else
CC := $(addprefix $(XTENSA_BINDIR)/,xtensa-lx106-elf-gcc)
LD := $(addprefix $(XTENSA_BINDIR)/,xtensa-lx106-elf-gcc)
endif

ifeq ($(V),1)
Q :=
else
Q := @
endif

OBJS := $(addprefix $(SBOOT_BUILD_BASE)/,sboot.o spiffs_cache.o spiffs_nucleus.o spiffs_hydrogen.o spiffs_gc.o spiffs_check.o uzlib_inflate.o)

CFLAGS    = -Os -Wpointer-arith -Wundef -Werror -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals -I $(SPIFFS_BASE) -I . -D__ets__ -DICACHE_FLASH
LDFLAGS   = -nostdlib -u call_user_start -Wl,-static
LD_SCRIPT = eagle.app.v6.ld

E2_OPTS = -quiet -bin -boot0

ifeq ($(SPI_SIZE), 256K)
	E2_OPTS += -256
else ifeq ($(SPI_SIZE), 512K)
	E2_OPTS += -512
else ifeq ($(SPI_SIZE), 1M)
	E2_OPTS += -1024
else ifeq ($(SPI_SIZE), 2M)
	E2_OPTS += -2048
else ifeq ($(SPI_SIZE), 2Mb)
	E2_OPTS += -2048b
else ifeq ($(SPI_SIZE), 4M)
	E2_OPTS += -4096
endif
ifeq ($(SPI_MODE), qio)
	E2_OPTS += -qio
else ifeq ($(SPI_MODE), dio)
	E2_OPTS += -dio
else ifeq ($(SPI_MODE), qout)
	E2_OPTS += -qout
else ifeq ($(SPI_MODE), dout)
	E2_OPTS += -dout
endif
ifeq ($(SPI_SPEED), 20)
	E2_OPTS += -20
else ifeq ($(SPI_SPEED), 26)
	E2_OPTS += -26.7
else ifeq ($(SPI_SPEED), 40)
	E2_OPTS += -40
else ifeq ($(SPI_SPEED), 80)
	E2_OPTS += -80
endif

.SECONDARY:

all: $(SBOOT_BUILD_BASE) $(SBOOT_FW_BASE) $(SBOOT_FW_BASE)/sboot.bin $(SBOOT_FW_BASE)/testload.bin

$(SBOOT_BUILD_BASE):
	$(Q) mkdir -p $@

$(SBOOT_FW_BASE):
	$(Q) mkdir -p $@

$(SBOOT_BUILD_BASE)/sboot-stage2a.o: sboot-stage2a.c sboot-private.h sboot.h
	@echo "CC $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(SBOOT_BUILD_BASE)/sboot-stage2a.elf: $(SBOOT_BUILD_BASE)/sboot-stage2a.o
	@echo "LD $@"
	$(Q) $(LD) -Tsboot-stage2a.ld $(LDFLAGS) -Wa,-a=list.lst -Wl,--start-group $^ -Wl,--end-group -o $@

$(SBOOT_BUILD_BASE)/sboot-hex2a.h: $(SBOOT_BUILD_BASE)/sboot-stage2a.elf
	@echo "E2 $@"
	$(Q) $(ESPTOOL2) -quiet -header $< $@ .text

$(SBOOT_BUILD_BASE)/sboot.o: sboot.c sboot-private.h sboot.h $(SBOOT_BUILD_BASE)/sboot-hex2a.h spiffs_config.h uzlib.h
	@echo "CC $<"
	$(Q) $(CC) $(CFLAGS) -I$(SBOOT_BUILD_BASE) -c $< -o $@

$(SBOOT_BUILD_BASE)/%.o: spiffs/src/%.c spiffs/src/spiffs.h spiffs/src/spiffs_nucleus.h spiffs_config.h
	@echo "CC $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(SBOOT_BUILD_BASE)/uzlib_inflate.o: uzlib_inflate.c uzlib.h
	@echo "CC $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(SBOOT_BUILD_BASE)/%.o: %.c %.h
	@echo "CC $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(SBOOT_BUILD_BASE)/%.o: %.c
	@echo "CC $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(SBOOT_BUILD_BASE)/testload.elf: $(SBOOT_BUILD_BASE)/testload.o
	@echo "LD $@"
	$(Q) $(LD) -T$(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $^ -Wl,--end-group -o $@


$(SBOOT_BUILD_BASE)/%.elf: $(OBJS)
	@echo "LD $@"
	$(Q) $(LD) -T$(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $^ -Wl,--end-group -o $@

$(SBOOT_FW_BASE)/%.bin: $(SBOOT_BUILD_BASE)/%.elf
	@echo "E2 $@"
	$(Q) $(ESPTOOL2) $(E2_OPTS) $< $@ .text .rodata

clean:
	@echo "RM $(SBOOT_BUILD_BASE) $(SBOOT_FW_BASE)"
	$(Q) rm -rf $(SBOOT_BUILD_BASE)
	$(Q) rm -rf $(SBOOT_FW_BASE)

